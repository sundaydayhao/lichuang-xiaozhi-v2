#include "application.h"
#include "board.h"
#include "display.h"
#include "system_info.h"
#include "audio_codec.h"
#include "esp_xiaozhi_protocol.h"
#include "assets/lang_config.h"
#include "assets.h"
#include "boards/bread-compact-ml307camtou/lamp_G.h"
#include "display/ui_display.h"
#include "display/ui_menu_ai_print.h"
#include "settings.h"

#include <cstring>
#include <algorithm>
#include <ctime>
#include <stdexcept>
#include <esp_log.h>
#include <esp_err.h>
#include <cJSON.h>
#include <driver/gpio.h>
#include <arpa/inet.h>
#include <font_awesome.h>
#include <cctype>

#ifdef TAG
#undef TAG
#endif
#define TAG "Application"


static const char* const STATE_STRINGS[] = {
    "unknown",
    "starting",
    "configuring",
    "idle",
    "connecting",
    "listening",
    "speaking",
    "upgrading",
    "activating",
    "audio_testing",
    "fatal_error",
    "invalid_state"
};

namespace {

std::string TrimCopy(const std::string& text) {
    size_t start = 0;
    while (start < text.size() && std::isspace(static_cast<unsigned char>(text[start]))) {
        ++start;
    }

    size_t end = text.size();
    while (end > start && std::isspace(static_cast<unsigned char>(text[end - 1]))) {
        --end;
    }

    return text.substr(start, end - start);
}

std::string FormatAiAlarmDuration(int delay_seconds) {
    const int hours = delay_seconds / 3600;
    const int minutes = (delay_seconds % 3600) / 60;
    const int seconds = delay_seconds % 60;

    std::string text;
    if (hours > 0) {
        text += std::to_string(hours) + "小时";
    }
    if (minutes > 0) {
        text += std::to_string(minutes) + "分钟";
    }
    if (seconds > 0 || text.empty()) {
        text += std::to_string(seconds) + "秒";
    }
    return text;
}

} // namespace

Application::Application() {
    event_group_ = xEventGroupCreate();

#if CONFIG_USE_DEVICE_AEC && CONFIG_USE_SERVER_AEC
#error "CONFIG_USE_DEVICE_AEC and CONFIG_USE_SERVER_AEC cannot be enabled at the same time"
#elif CONFIG_USE_DEVICE_AEC
    aec_mode_ = kAecOnDeviceSide;
#elif CONFIG_USE_SERVER_AEC
    aec_mode_ = kAecOnServerSide;
#else
    aec_mode_ = kAecOff;
#endif

    esp_timer_create_args_t clock_timer_args = {
        .callback = [](void* arg) {
            Application* app = (Application*)arg;
            xEventGroupSetBits(app->event_group_, MAIN_EVENT_CLOCK_TICK);
        },
        .arg = this,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "clock_timer",
        .skip_unhandled_events = true
    };
    esp_timer_create(&clock_timer_args, &clock_timer_handle_);

    esp_timer_create_args_t ai_alarm_repeat_timer_args = {
        .callback = [](void* arg) {
            auto* app = static_cast<Application*>(arg);
            if (app != nullptr) {
                app->Schedule([app]() {
                    app->HandleAiAlarmRepeat();
                });
            }
        },
        .arg = this,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "ai_alarm_repeat",
        .skip_unhandled_events = true,
    };
    esp_timer_create(&ai_alarm_repeat_timer_args, &ai_alarm_repeat_timer_handle_);
}

Application::~Application() {
    if (ai_alarm_repeat_timer_handle_ != nullptr) {
        esp_timer_stop(ai_alarm_repeat_timer_handle_);
        esp_timer_delete(ai_alarm_repeat_timer_handle_);
    }
    if (clock_timer_handle_ != nullptr) {
        esp_timer_stop(clock_timer_handle_);
        esp_timer_delete(clock_timer_handle_);
    }
    vEventGroupDelete(event_group_);
}

void Application::CheckAssetsVersion() {
    auto& board = Board::GetInstance();
    auto display = board.GetDisplay();
    auto& assets = Assets::GetInstance();

    if (!assets.partition_valid()) {
        ESP_LOGW(TAG, "Assets partition is disabled for board %s", BOARD_NAME);
        return;
    }
    
    Settings settings("assets", true);
    // Check if there is a new assets need to be downloaded
    std::string download_url = settings.GetString("download_url");

    if (!download_url.empty()) {
        settings.EraseKey("download_url");

        char message[256];
        snprintf(message, sizeof(message), Lang::Strings::FOUND_NEW_ASSETS, download_url.c_str());
        Alert(Lang::Strings::LOADING_ASSETS, message, "cloud_arrow_down", Lang::Sounds::OGG_UPGRADE);
        
        // Wait for the audio service to be idle for 3 seconds
        vTaskDelay(pdMS_TO_TICKS(3000));
        SetDeviceState(kDeviceStateUpgrading);
        board.SetPowerSaveMode(false);
        display->SetChatMessage("system", Lang::Strings::PLEASE_WAIT);

        bool success = assets.Download(download_url, [display](int progress, size_t speed) -> void {
            std::thread([display, progress, speed]() {
                char buffer[32];
                snprintf(buffer, sizeof(buffer), "%d%% %uKB/s", progress, speed / 1024);
                display->SetChatMessage("system", buffer);
            }).detach();
        });

        board.SetPowerSaveMode(true);
        vTaskDelay(pdMS_TO_TICKS(1000));

        if (!success) {
            Alert(Lang::Strings::ERROR, Lang::Strings::DOWNLOAD_ASSETS_FAILED, "circle_xmark", Lang::Sounds::OGG_EXCLAMATION);
            vTaskDelay(pdMS_TO_TICKS(2000));
            return;
        }
    }

    // Apply assets
    assets.Apply();
    display->SetChatMessage("system", "");
    display->SetEmotion("microchip_ai");
}

void Application::CheckNewVersion(Ota& ota) {
    const int MAX_RETRY = 10;
    int retry_count = 0;
    int retry_delay = 10; // 初始重试延迟为10秒

    auto& board = Board::GetInstance();
    while (true) {
        SetDeviceState(kDeviceStateActivating);
        auto display = board.GetDisplay();
        display->SetStatus(Lang::Strings::CHECKING_NEW_VERSION);

        if (!ota.CheckVersion()) {
            retry_count++;
            if (retry_count >= MAX_RETRY) {
                ESP_LOGE(TAG, "Too many retries, exit version check");
                return;
            }

            char buffer[256];
            snprintf(buffer, sizeof(buffer), Lang::Strings::CHECK_NEW_VERSION_FAILED, retry_delay, ota.GetCheckVersionUrl().c_str());
            Alert(Lang::Strings::ERROR, buffer, "cloud_slash", Lang::Sounds::OGG_EXCLAMATION);

            ESP_LOGW(TAG, "Check new version failed, retry in %d seconds (%d/%d)", retry_delay, retry_count, MAX_RETRY);
            for (int i = 0; i < retry_delay; i++) {
                vTaskDelay(pdMS_TO_TICKS(1000));
                if (device_state_ == kDeviceStateIdle) {
                    break;
                }
            }
            retry_delay *= 2; // 每次重试后延迟时间翻倍
            continue;
        }
        retry_count = 0;
        retry_delay = 10; // 重置重试延迟时间

        if (ota.HasNewVersion()) {
            if (UpgradeFirmware(ota)) {
                return; // This line will never be reached after reboot
            }
            // If upgrade failed, continue to normal operation (don't break, just fall through)
        }

        // No new version, mark the current version as valid
        ota.MarkCurrentVersionValid();
        if (!ota.HasActivationCode() && !ota.HasActivationChallenge()) {
            xEventGroupSetBits(event_group_, MAIN_EVENT_CHECK_NEW_VERSION_DONE);
            // Exit the loop if done checking new version
            break;
        }

        display->SetStatus(Lang::Strings::ACTIVATION);
        // Activation code is shown to the user and waiting for the user to input
        if (ota.HasActivationCode()) {
            ShowActivationCode(ota.GetActivationCode(), ota.GetActivationMessage());
        }

        // This will block the loop until the activation is done or timeout
        for (int i = 0; i < 10; ++i) {
            ESP_LOGI(TAG, "Activating... %d/%d", i + 1, 10);
            esp_err_t err = ota.Activate();
            if (err == ESP_OK) {
                xEventGroupSetBits(event_group_, MAIN_EVENT_CHECK_NEW_VERSION_DONE);
                break;
            } else if (err == ESP_ERR_TIMEOUT) {
                vTaskDelay(pdMS_TO_TICKS(3000));
            } else {
                vTaskDelay(pdMS_TO_TICKS(10000));
            }
            if (device_state_ == kDeviceStateIdle) {
                break;
            }
        }
    }
}

void Application::ShowActivationCode(const std::string& code, const std::string& message) {
    struct digit_sound {
        char digit;
        const std::string_view& sound;
    };
    static const std::array<digit_sound, 10> digit_sounds{{
        digit_sound{'0', Lang::Sounds::OGG_0},
        digit_sound{'1', Lang::Sounds::OGG_1}, 
        digit_sound{'2', Lang::Sounds::OGG_2},
        digit_sound{'3', Lang::Sounds::OGG_3},
        digit_sound{'4', Lang::Sounds::OGG_4},
        digit_sound{'5', Lang::Sounds::OGG_5},
        digit_sound{'6', Lang::Sounds::OGG_6},
        digit_sound{'7', Lang::Sounds::OGG_7},
        digit_sound{'8', Lang::Sounds::OGG_8},
        digit_sound{'9', Lang::Sounds::OGG_9}
    }};

    // This sentence uses 9KB of SRAM, so we need to wait for it to finish
    Alert(Lang::Strings::ACTIVATION, message.c_str(), "link", Lang::Sounds::OGG_ACTIVATION);

    for (const auto& digit : code) {
        auto it = std::find_if(digit_sounds.begin(), digit_sounds.end(),
            [digit](const digit_sound& ds) { return ds.digit == digit; });
        if (it != digit_sounds.end()) {
            audio_service_.PlaySound(it->sound);
        }
    }
}

void Application::Alert(const char* status, const char* message, const char* emotion, const std::string_view& sound) {
    ESP_LOGW(TAG, "Alert [%s] %s: %s", emotion, status, message);
    auto display = Board::GetInstance().GetDisplay();
    display->SetStatus(status);
    display->SetEmotion(emotion);
    display->SetChatMessage("system", message);
    if (!sound.empty()) {
        audio_service_.PlaySound(sound);
    }
}

void Application::DismissAlert() {
    if (device_state_ == kDeviceStateIdle) {
        auto display = Board::GetInstance().GetDisplay();
        display->SetStatus(Lang::Strings::STANDBY);
        display->SetEmotion("neutral");
        display->SetChatMessage("system", "");
    }
}

void Application::ToggleChatState() {
    if (device_state_ == kDeviceStateActivating) {
        SetDeviceState(kDeviceStateIdle);
        return;
    } else if (device_state_ == kDeviceStateWifiConfiguring) {
        audio_service_.EnableAudioTesting(true);
        SetDeviceState(kDeviceStateAudioTesting);
        return;
    } else if (device_state_ == kDeviceStateAudioTesting) {
        audio_service_.EnableAudioTesting(false);
        SetDeviceState(kDeviceStateWifiConfiguring);
        return;
    }

    if (!protocol_) {
        ESP_LOGE(TAG, "Protocol not initialized");
        return;
    }

    if (device_state_ == kDeviceStateIdle) {
        Schedule([this]() {
            suppress_server_messages_ = false;
            if (!protocol_->IsAudioChannelOpened()) {
                SetDeviceState(kDeviceStateConnecting);
                if (!protocol_->OpenAudioChannel()) {
                    return;
                }
            }

        //    SetListeningMode(aec_mode_ == kAecOff ? kListeningModeAutoStop : kListeningModeRealtime);
        SetListeningMode(kListeningModeAutoStop);
        });
    } else if (device_state_ == kDeviceStateSpeaking) {
        Schedule([this]() {
            AbortSpeaking(kAbortReasonNone);
        });
    } else if (device_state_ == kDeviceStateListening) {
        Schedule([this]() {
            protocol_->CloseAudioChannel();
        });
    }
}

void Application::StartListening() {
    if (device_state_ == kDeviceStateActivating) {
        SetDeviceState(kDeviceStateIdle);
        return;
    } else if (device_state_ == kDeviceStateWifiConfiguring) {
        audio_service_.EnableAudioTesting(true);
        SetDeviceState(kDeviceStateAudioTesting);
        return;
    }

    if (!protocol_) {
        ESP_LOGE(TAG, "Protocol not initialized");
        return;
    }
    
    if (device_state_ == kDeviceStateIdle) {
        Schedule([this]() {
            suppress_server_messages_ = false;
            if (!protocol_->IsAudioChannelOpened()) {
                SetDeviceState(kDeviceStateConnecting);
                if (!protocol_->OpenAudioChannel()) {
                    return;
                }
            }

            SetListeningMode(kListeningModeManualStop);
        });
    } else if (device_state_ == kDeviceStateSpeaking) {
        Schedule([this]() {
            AbortSpeaking(kAbortReasonNone);
            SetListeningMode(kListeningModeManualStop);
        });
    }
}

void Application::StopListening() {
    if (device_state_ == kDeviceStateAudioTesting) {
        audio_service_.EnableAudioTesting(false);
        SetDeviceState(kDeviceStateWifiConfiguring);
        return;
    }

    const std::array<int, 3> valid_states = {
        kDeviceStateListening,
        kDeviceStateSpeaking,
        kDeviceStateIdle,
    };
    // If not valid, do nothing
    if (std::find(valid_states.begin(), valid_states.end(), device_state_) == valid_states.end()) {
        return;
    }

    Schedule([this]() {
        if (device_state_ == kDeviceStateListening) {
            protocol_->SendStopListening();
            SetDeviceState(kDeviceStateIdle);
        }
    });
}

void Application::ExitChatSession() {
    Schedule([this]() {
        suppress_server_messages_ = false;
        if (protocol_ != nullptr && protocol_->IsAudioChannelOpened()) {
            protocol_->CloseAudioChannel();
            return;
        }

        if (device_state_ == kDeviceStateListening || device_state_ == kDeviceStateSpeaking ||
            device_state_ == kDeviceStateConnecting) {
            SetDeviceState(kDeviceStateIdle);
        }
    });
}

void Application::Start() {
    auto& board = Board::GetInstance();
    SetDeviceState(kDeviceStateStarting);

    /* Setup the display */
    auto display = board.GetDisplay();

    // Print board name/version info
    display->SetChatMessage("system", SystemInfo::GetUserAgent().c_str());

    /* Setup the audio service */
    auto codec = board.GetAudioCodec();
    audio_service_.Initialize(codec);
    audio_service_.Start();

    AudioServiceCallbacks callbacks;
    callbacks.on_send_queue_available = [this]() {
        xEventGroupSetBits(event_group_, MAIN_EVENT_SEND_AUDIO);
    };
    callbacks.on_wake_word_detected = [this](const std::string& wake_word) {
        xEventGroupSetBits(event_group_, MAIN_EVENT_WAKE_WORD_DETECTED);
    };
    callbacks.on_vad_change = [this](bool speaking) {
        xEventGroupSetBits(event_group_, MAIN_EVENT_VAD_CHANGE);
    };
    audio_service_.SetCallbacks(callbacks);

    // Start the main event loop task with priority 3
    xTaskCreate([](void* arg) {
        ((Application*)arg)->MainEventLoop();
        vTaskDelete(NULL);
    }, "main_event_loop", 2048 * 6, this, 3, &main_event_loop_task_handle_);

    /* Start the clock timer to update the status bar */
    esp_timer_start_periodic(clock_timer_handle_, 1000000);

    /* Wait for the network to be ready */
    board.StartNetwork();

    // Update the status bar immediately to show the network state
    display->UpdateStatusBar(true);

    // Check for new assets version
    CheckAssetsVersion();

    // Check for firmware updates and activation.
    Ota ota;
    CheckNewVersion(ota);

    // Initialize the protocol
    display->SetStatus(Lang::Strings::LOADING_PROTOCOL);

    protocol_ = std::make_unique<EspXiaozhiProtocol>();

    protocol_->OnConnected([this]() {
        DismissAlert();
    });

    protocol_->OnNetworkError([this](const std::string& message) {
        last_error_message_ = message;
        xEventGroupSetBits(event_group_, MAIN_EVENT_ERROR);
    });
    protocol_->OnIncomingAudio([this](std::unique_ptr<AudioStreamPacket> packet) {
        //ESP_LOGI(TAG, "OnIncomingAudio: device_state=%d, packet_size=%u", 
        //         device_state_, packet->payload.size());
        
        if (device_state_ == kDeviceStateSpeaking) {
        //    ESP_LOGI(TAG, "Pushing packet to decode queue");
            audio_service_.PushPacketToDecodeQueue(std::move(packet));
        } else {
        //    ESP_LOGW(TAG, "Device state is not Speaking (%d), dropping audio packet", device_state_);
        }
    });
    protocol_->OnAudioChannelOpened([this, codec, &board]() {
        board.SetPowerSaveMode(false);
        if (protocol_->server_sample_rate() != codec->output_sample_rate()) {
            ESP_LOGW(TAG, "Server sample rate %d does not match device output sample rate %d, resampling may cause distortion",
                protocol_->server_sample_rate(), codec->output_sample_rate());
        }
    });
    protocol_->OnAudioChannelClosed([this, &board]() {
        board.SetPowerSaveMode(true);
        Schedule([this]() {
            auto display = Board::GetInstance().GetDisplay();
            auto* ui_display = dynamic_cast<UiLcdDisplay*>(display);
            if (!(suppress_server_messages_ && ui_display != nullptr &&
                  UiMenuAiPrint::IsActive(*ui_display))) {
                display->SetChatMessage("system", "");
            }
            SetDeviceState(kDeviceStateIdle);
        });
    });
    protocol_->OnIncomingJson([this, display](const cJSON* root) {
        // Parse JSON data
        auto type = cJSON_GetObjectItem(root, "type");
        if (strcmp(type->valuestring, "tts") == 0) {
            if (suppress_server_messages_) {
                ESP_LOGI(TAG, "Ignoring TTS while local feature is handling the request");
                return;
            }
            auto state = cJSON_GetObjectItem(root, "state");
            if (strcmp(state->valuestring, "start") == 0) {
                Schedule([this]() {
                    aborted_ = false;
                    if (device_state_ == kDeviceStateIdle || device_state_ == kDeviceStateListening) {
                        SetDeviceState(kDeviceStateSpeaking);
                    }
                });
            } else if (strcmp(state->valuestring, "stop") == 0) {
                Schedule([this]() {
                    if (device_state_ == kDeviceStateSpeaking) {
                        if (listening_mode_ == kListeningModeManualStop) {
                            SetDeviceState(kDeviceStateIdle);
                        } else {
                            SetDeviceState(kDeviceStateListening);
                        }
                    }
                });
            } else if (strcmp(state->valuestring, "sentence_start") == 0) {
                // 如果还没有切换到Speaking状态，现在切换（服务器可能只发送sentence_start而不发送start）
                Schedule([this]() {
                    if (device_state_ == kDeviceStateIdle || device_state_ == kDeviceStateListening) {
                    //if (device_state_ != kDeviceStateSpeaking) {
                        aborted_ = false;
                        SetDeviceState(kDeviceStateSpeaking);
                    }
                });
                auto text = cJSON_GetObjectItem(root, "text");
                if (cJSON_IsString(text)) {
                    ESP_LOGI(TAG, "<< %s", text->valuestring);
                    Schedule([this, display, message = std::string(text->valuestring)]() {
                        display->SetChatMessage("assistant", message.c_str());
                    });
                }
            }
        } else if (strcmp(type->valuestring, "stt") == 0) {
            if (suppress_server_messages_) {
                ESP_LOGI(TAG, "Ignoring STT while local feature request is active");
                return;
            }
            auto text = cJSON_GetObjectItem(root, "text");
            if (cJSON_IsString(text)) {
                ESP_LOGI(TAG, ">> %s", text->valuestring);

                std::string content(text->valuestring);
                auto* ui_display = dynamic_cast<UiLcdDisplay*>(display);
                if (ui_display != nullptr && ui_display->HandleFeatureSpeechRecognized(content)) {
                    suppress_server_messages_ = true;
                    Schedule([this]() {
                        if (protocol_ != nullptr && protocol_->IsAudioChannelOpened()) {
                            protocol_->CloseAudioChannel();
                        } else {
                            SetDeviceState(kDeviceStateIdle);
                        }
                    });
                    return;
                }

                // 本地关键词直达舵机：先动再说
                Schedule([content]() {
                    // 解析“按摩X分钟/分”指令（优先处理，支持阿拉伯数字与常见中文数字）
                    size_t pos_a = content.find("按摩");
                    size_t pos_m = content.find("分钟");
                    if (pos_m == std::string::npos) pos_m = content.find("分");
                    if (pos_a != std::string::npos && pos_m != std::string::npos && pos_m > pos_a) {
                        // 取出区间 [pos_a, pos_m) 做解析
                        std::string seg = content.substr(pos_a, pos_m - pos_a);

                        auto parse_arabic = [&](const std::string& s)->int {
                            int value = 0; int digits = 0;
                            for (char c : s) {
                                if (c >= '0' && c <= '9') {
                                    value = value * 10 + (c - '0');
                                    digits++;
                                }
                            }
                            return digits > 0 ? value : 0;
                        };
                        auto contains = [&](const std::string& s, const char* pat)->bool {
                            return s.find(pat) != std::string::npos;
                        };
                        auto digit_of = [&](const std::string& s)->int {
                            if (contains(seg, "九")) return 9;
                            if (contains(seg, "八")) return 8;
                            if (contains(seg, "七")) return 7;
                            if (contains(seg, "六")) return 6;
                            if (contains(seg, "五")) return 5;
                            if (contains(seg, "四")) return 4;
                            if (contains(seg, "三")) return 3;
                            if (contains(seg, "二") || contains(seg, "两")) return 2;
                            if (contains(seg, "一")) return 1;
                            if (contains(seg, "零")) return 0;
                            return -1;
                        };
                        auto parse_chinese = [&](const std::string& s)->int {
                            // 支持：一/二/.../九；十/二十/三十；十X/二十X/三十X 等常见口语
                            // 简化处理：通过“十”的左右片段判定十位与个位
                            size_t pos_t = s.find("十");
                            if (pos_t != std::string::npos) {
                                std::string left = s.substr(0, pos_t);
                                std::string right = s.substr(pos_t + std::string("十").size());
                                int tens = digit_of(left);
                                if (tens < 0) tens = 1; // “十X” 等价于 “一十X”
                                int ones = digit_of(right);
                                if (ones < 0) ones = 0;
                                return tens * 10 + ones;
                            }
                            // 没有“十”，尝试单个中文数字
                            int single = digit_of(s);
                            return single >= 0 ? single : 0;
                        };

                        int minutes = parse_arabic(seg);
                        if (minutes == 0) {
                            minutes = parse_chinese(seg);
                        }
                        if (minutes > 0) {
                            Green_Lamp::StartMassageWithMinutes(minutes);
                            return;
                        }
                    }

                    if (content.find("开始按摩") != std::string::npos) {
                        Green_Lamp::StartMassage();
                    } else if (content.find("结束按摩") != std::string::npos) {
                        Green_Lamp::StopMassage();
                    } else if (content.find("加一档") != std::string::npos) {
                        Green_Lamp::IncreaseLevel();
                    } else if (content.find("减一档") != std::string::npos) {
                        Green_Lamp::DecreaseLevel();
                    }
                });

                Schedule([this, display, message = std::move(content)]() {
                    display->SetChatMessage("user", message.c_str());
                });
            }
        } else if (strcmp(type->valuestring, "llm") == 0) {
            if (suppress_server_messages_) {
                ESP_LOGI(TAG, "Ignoring LLM metadata while local feature is handling the request");
                return;
            }
            auto emotion = cJSON_GetObjectItem(root, "emotion");
            if (cJSON_IsString(emotion)) {
                Schedule([this, display, emotion_str = std::string(emotion->valuestring)]() {
                    display->SetEmotion(emotion_str.c_str());
                });
            }
        } else if (strcmp(type->valuestring, "system") == 0) {
            auto command = cJSON_GetObjectItem(root, "command");
            if (cJSON_IsString(command)) {
                ESP_LOGI(TAG, "System command: %s", command->valuestring);
                if (strcmp(command->valuestring, "reboot") == 0) {
                    // Do a reboot if user requests a OTA update
                    Schedule([this]() {
                        Reboot();
                    });
                } else {
                    ESP_LOGW(TAG, "Unknown system command: %s", command->valuestring);
                }
            }
        } else if (strcmp(type->valuestring, "alert") == 0) {
            auto status = cJSON_GetObjectItem(root, "status");
            auto message = cJSON_GetObjectItem(root, "message");
            auto emotion = cJSON_GetObjectItem(root, "emotion");
            if (cJSON_IsString(status) && cJSON_IsString(message) && cJSON_IsString(emotion)) {
                Alert(status->valuestring, message->valuestring, emotion->valuestring, Lang::Sounds::OGG_VIBRATION);
            } else {
                ESP_LOGW(TAG, "Alert command requires status, message and emotion");
            }
#if CONFIG_RECEIVE_CUSTOM_MESSAGE
        } else if (strcmp(type->valuestring, "custom") == 0) {
            auto payload = cJSON_GetObjectItem(root, "payload");
            ESP_LOGI(TAG, "Received custom message: %s", cJSON_PrintUnformatted(root));
            if (cJSON_IsObject(payload)) {
                Schedule([this, display, payload_str = std::string(cJSON_PrintUnformatted(payload))]() {
                    display->SetChatMessage("system", payload_str.c_str());
                });
            } else {
                ESP_LOGW(TAG, "Invalid custom message format: missing payload");
            }
#endif
        } else {
            ESP_LOGW(TAG, "Unknown message type: %s", type->valuestring);
        }
    });
    bool protocol_started = protocol_->Start();

    SystemInfo::PrintHeapStats();
    SetDeviceState(kDeviceStateIdle);

    if (protocol_started) {
        std::string message = std::string(Lang::Strings::VERSION) + ota.GetCurrentVersion();
        display->ShowNotification(message.c_str());
        display->SetChatMessage("system", "");
        // Play the success sound to indicate the device is ready
        audio_service_.PlaySound(Lang::Sounds::OGG_SUCCESS);
    }
}

// Add a async task to MainLoop
void Application::Schedule(std::function<void()> callback) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        main_tasks_.push_back(std::move(callback));
    }
    xEventGroupSetBits(event_group_, MAIN_EVENT_SCHEDULE);
}

// The main event loop controls chat state and protocol I/O.
// Other tasks should use Schedule to touch chat state from this task.
void Application::MainEventLoop() {
    while (true) {
        auto bits = xEventGroupWaitBits(event_group_, MAIN_EVENT_SCHEDULE |
            MAIN_EVENT_SEND_AUDIO |
            MAIN_EVENT_WAKE_WORD_DETECTED |
            MAIN_EVENT_VAD_CHANGE |
            MAIN_EVENT_CLOCK_TICK |
            MAIN_EVENT_ERROR, pdTRUE, pdFALSE, portMAX_DELAY);

        if (bits & MAIN_EVENT_ERROR) {
            SetDeviceState(kDeviceStateIdle);
            Alert(Lang::Strings::ERROR, last_error_message_.c_str(), "circle_xmark", Lang::Sounds::OGG_EXCLAMATION);
             // 显示错误信息3秒后重启
             vTaskDelay(pdMS_TO_TICKS(3000));
             ESP_LOGI(TAG, "Network error occurred, rebooting for reconfiguration...");
             Reboot();
        }

        if (bits & MAIN_EVENT_SEND_AUDIO) {
            while (auto packet = audio_service_.PopPacketFromSendQueue()) {
                if (protocol_ && !protocol_->SendAudio(std::move(packet))) {
                    break;
                }
            }
        }

        if (bits & MAIN_EVENT_WAKE_WORD_DETECTED) {
            OnWakeWordDetected();
        }

        if (bits & MAIN_EVENT_VAD_CHANGE) {
            if (device_state_ == kDeviceStateListening) {
                auto led = Board::GetInstance().GetLed();
                led->OnStateChanged();
            }
        }

        if (bits & MAIN_EVENT_SCHEDULE) {
            std::unique_lock<std::mutex> lock(mutex_);
            auto tasks = std::move(main_tasks_);
            lock.unlock();
            for (auto& task : tasks) {
                task();
            }
        }

        if (bits & MAIN_EVENT_CLOCK_TICK) {
            clock_ticks_++;
            auto display = Board::GetInstance().GetDisplay();
            display->UpdateStatusBar();
            CheckAiAlarmOnClockTick();
        
            // Print the debug info every 10 seconds
            if (clock_ticks_ % 10 == 0) {
                // SystemInfo::PrintTaskCpuUsage(pdMS_TO_TICKS(1000));
                // SystemInfo::PrintTaskList();
                SystemInfo::PrintHeapStats();
            }
        }
    }
}

void Application::OnWakeWordDetected() {
    if (!protocol_) {
        return;
    }

    if (device_state_ == kDeviceStateIdle) {
        suppress_server_messages_ = false;
        audio_service_.EncodeWakeWord();

        if (!protocol_->IsAudioChannelOpened()) {
            SetDeviceState(kDeviceStateConnecting);
            if (!protocol_->OpenAudioChannel()) {
                audio_service_.EnableWakeWordDetection(true);
                return;
            }
        }

        auto wake_word = audio_service_.GetLastWakeWord();
        ESP_LOGI(TAG, "Wake word detected: %s", wake_word.c_str());
#if CONFIG_USE_AFE_WAKE_WORD || CONFIG_USE_CUSTOM_WAKE_WORD
        // Encode and send the wake word data to the server
        while (auto packet = audio_service_.PopWakeWordPacket()) {
            protocol_->SendAudio(std::move(packet));
        }
        // Set the chat state to wake word detected
        protocol_->SendWakeWordDetected(wake_word);
        //SetListeningMode(aec_mode_ == kAecOff ? kListeningModeAutoStop : kListeningModeRealtime);
        SetListeningMode(kListeningModeAutoStop);
#else
      //  SetListeningMode(aec_mode_ == kAecOff ? kListeningModeAutoStop : kListeningModeRealtime);
        SetListeningMode(kListeningModeAutoStop);
        // Play the pop up sound to indicate the wake word is detected
        audio_service_.PlaySound(Lang::Sounds::OGG_POPUP);
#endif
    } else if (device_state_ == kDeviceStateSpeaking) {
        AbortSpeaking(kAbortReasonWakeWordDetected);
    } else if (device_state_ == kDeviceStateActivating) {
        SetDeviceState(kDeviceStateIdle);
    }
}

void Application::AbortSpeaking(AbortReason reason) {
    ESP_LOGI(TAG, "Abort speaking");
    aborted_ = true;
    if (protocol_) {
        protocol_->SendAbortSpeaking(reason);
    }
}

void Application::SetListeningMode(ListeningMode mode) {
    listening_mode_ = mode;
    SetDeviceState(kDeviceStateListening);
}

void Application::ApplyAudioBehaviorForState(DeviceState state) {
    if (audio_service_suspended_for_feature_) {
        return;
    }

    switch (state) {
    case kDeviceStateUnknown:
    case kDeviceStateIdle:
        audio_service_.EnableVoiceProcessing(false);
        audio_service_.EnableWakeWordDetection(true);
        break;
    case kDeviceStateListening:
        if (!audio_service_.IsAudioProcessorRunning()) {
            if (protocol_) {
                protocol_->SendStartListening(listening_mode_);
            }
            audio_service_.EnableVoiceProcessing(true);
            audio_service_.EnableWakeWordDetection(false);
        }
        break;
    case kDeviceStateSpeaking:
        if (listening_mode_ != kListeningModeRealtime) {
            audio_service_.EnableVoiceProcessing(false);
#if CONFIG_USE_AFE_WAKE_WORD
            audio_service_.EnableWakeWordDetection(true);
#else
            audio_service_.EnableWakeWordDetection(false);
#endif
        }
        audio_service_.ResetDecoder();
        break;
    default:
        break;
    }
}

void Application::SetDeviceState(DeviceState state) {
    if (device_state_ == state) {
        return;
    }
    
    clock_ticks_ = 0;
    auto previous_state = device_state_;
    device_state_ = state;
    ESP_LOGI(TAG, "STATE: %s", STATE_STRINGS[device_state_]);

    // Send the state change event
    DeviceStateEventManager::GetInstance().PostStateChangeEvent(previous_state, state);

    auto& board = Board::GetInstance();
    auto display = board.GetDisplay();
    auto led = board.GetLed();
    led->OnStateChanged();
    switch (state) {
        case kDeviceStateUnknown:
        case kDeviceStateIdle:
            display->SetStatus(Lang::Strings::STANDBY);
            display->SetEmotion("neutral");
            ApplyAudioBehaviorForState(state);
            break;
        case kDeviceStateConnecting:
            display->SetStatus(Lang::Strings::CONNECTING);
            display->SetEmotion("neutral");
            display->SetChatMessage("system", "");
            break;
        case kDeviceStateListening:
            display->SetStatus(Lang::Strings::LISTENING);
            display->SetEmotion("neutral");
            ApplyAudioBehaviorForState(state);
            break;
        case kDeviceStateSpeaking:
            display->SetStatus(Lang::Strings::SPEAKING);
            ApplyAudioBehaviorForState(state);
            break;
        default:
            // Do nothing
            break;
    }
}

void Application::SuspendAudioForFeature() {
    if (audio_service_suspended_for_feature_) {
        return;
    }

    ESP_LOGI(TAG, "Suspending audio service for feature page");
    audio_service_suspended_for_feature_ = true;
    audio_service_.Stop();
    vTaskDelay(pdMS_TO_TICKS(200));
}

void Application::ResumeAudioAfterFeature() {
    if (!audio_service_suspended_for_feature_) {
        return;
    }

    ESP_LOGI(TAG, "Resuming audio service after feature page");
    audio_service_.Start();
    vTaskDelay(pdMS_TO_TICKS(50));
    audio_service_suspended_for_feature_ = false;
    ApplyAudioBehaviorForState(device_state_);
}

void Application::Reboot() {
    ESP_LOGI(TAG, "Rebooting...");
    // Disconnect the audio channel
    if (protocol_ && protocol_->IsAudioChannelOpened()) {
        protocol_->CloseAudioChannel();
    }
    protocol_.reset();
    audio_service_.Stop();

    vTaskDelay(pdMS_TO_TICKS(1000));
    esp_restart();
}

bool Application::UpgradeFirmware(Ota& ota, const std::string& url) {
    auto& board = Board::GetInstance();
    auto display = board.GetDisplay();
    
    // Use provided URL or get from OTA object
    std::string upgrade_url = url.empty() ? ota.GetFirmwareUrl() : url;
    std::string version_info = url.empty() ? ota.GetFirmwareVersion() : "(Manual upgrade)";
    
    // Close audio channel if it's open
    if (protocol_ && protocol_->IsAudioChannelOpened()) {
        ESP_LOGI(TAG, "Closing audio channel before firmware upgrade");
        protocol_->CloseAudioChannel();
    }
    ESP_LOGI(TAG, "Starting firmware upgrade from URL: %s", upgrade_url.c_str());
    
    Alert(Lang::Strings::OTA_UPGRADE, Lang::Strings::UPGRADING, "download", Lang::Sounds::OGG_UPGRADE);
    vTaskDelay(pdMS_TO_TICKS(3000));

    SetDeviceState(kDeviceStateUpgrading);
    
    std::string message = std::string(Lang::Strings::NEW_VERSION) + version_info;
    display->SetChatMessage("system", message.c_str());

    board.SetPowerSaveMode(false);
    audio_service_.Stop();
    vTaskDelay(pdMS_TO_TICKS(1000));

    bool upgrade_success = ota.StartUpgradeFromUrl(upgrade_url, [display](int progress, size_t speed) {
        std::thread([display, progress, speed]() {
            char buffer[32];
            snprintf(buffer, sizeof(buffer), "%d%% %uKB/s", progress, speed / 1024);
            display->SetChatMessage("system", buffer);
        }).detach();
    });

    if (!upgrade_success) {
        // Upgrade failed, restart audio service and continue running
        ESP_LOGE(TAG, "Firmware upgrade failed, restarting audio service and continuing operation...");
        audio_service_.Start(); // Restart audio service
        board.SetPowerSaveMode(true); // Restore power save mode
        Alert(Lang::Strings::ERROR, Lang::Strings::UPGRADE_FAILED, "circle_xmark", Lang::Sounds::OGG_EXCLAMATION);
        vTaskDelay(pdMS_TO_TICKS(3000));
        return false;
    } else {
        // Upgrade success, reboot immediately
        ESP_LOGI(TAG, "Firmware upgrade successful, rebooting...");
        display->SetChatMessage("system", "Upgrade successful, rebooting...");
        vTaskDelay(pdMS_TO_TICKS(1000)); // Brief pause to show message
        Reboot();
        return true;
    }
}

void Application::WakeWordInvoke(const std::string& wake_word) {
    if (device_state_ == kDeviceStateIdle) {
        suppress_server_messages_ = false;
        ToggleChatState();
        Schedule([this, wake_word]() {
            if (protocol_) {
                protocol_->SendWakeWordDetected(wake_word); 
            }
        }); 
    } else if (device_state_ == kDeviceStateSpeaking) {
        Schedule([this]() {
            AbortSpeaking(kAbortReasonNone);
        });
    } else if (device_state_ == kDeviceStateListening) {   
        Schedule([this]() {
            if (protocol_) {
                protocol_->CloseAudioChannel();
            }
        });
    }
}

bool Application::CanEnterSleepMode() {
    if (HasAiAlarmRunning()) {
        return false;
    }

    if (device_state_ != kDeviceStateIdle) {
        return false;
    }

    if (protocol_ && protocol_->IsAudioChannelOpened()) {
        return false;
    }

    if (!audio_service_.IsIdle()) {
        return false;
    }

    // Now it is safe to enter sleep mode
    return true;
}

std::string Application::SetAiAlarmTimer(int delay_seconds, const std::string& reminder) {
    if (delay_seconds <= 0) {
        throw std::runtime_error("delay_seconds must be greater than 0");
    }

    const std::string trimmed_reminder = TrimCopy(reminder);
    const std::string normalized_reminder = trimmed_reminder.empty() ? "时间到了" : trimmed_reminder;
    const time_t now = time(nullptr);

    {
        std::lock_guard<std::mutex> lock(ai_alarm_mutex_);
        StopAiAlarmRepeatTimerLocked();
        ai_alarm_active_ = true;
        ai_alarm_ringing_ = false;
        ai_alarm_target_time_ = now + delay_seconds;
        ai_alarm_initial_seconds_ = delay_seconds;
        ai_alarm_reminder_ = normalized_reminder;
        ai_alarm_repeat_remaining_ = 0;
    }

    return "已设置AI闹钟，" + FormatAiAlarmDuration(delay_seconds) + "后提醒你：" + normalized_reminder;
}

std::string Application::CancelAiAlarmTimer() {
    std::lock_guard<std::mutex> lock(ai_alarm_mutex_);
    if (!ai_alarm_active_ && !ai_alarm_ringing_) {
        return "当前没有正在运行的AI闹钟";
    }

    ClearAiAlarmLocked();
    return "已取消当前AI闹钟";
}

Application::AiAlarmSnapshot Application::GetAiAlarmSnapshot() const {
    std::lock_guard<std::mutex> lock(ai_alarm_mutex_);

    AiAlarmSnapshot snapshot;
    snapshot.active = ai_alarm_active_;
    snapshot.ringing = ai_alarm_ringing_;
    snapshot.total_seconds = ai_alarm_initial_seconds_;
    snapshot.target_time = ai_alarm_target_time_;
    snapshot.reminder = ai_alarm_reminder_;
    snapshot.pending_repeats = ai_alarm_repeat_remaining_;

    if (ai_alarm_active_) {
        const time_t now = time(nullptr);
        snapshot.remaining_seconds = std::max(0, static_cast<int>(ai_alarm_target_time_ - now));
    } else {
        snapshot.remaining_seconds = 0;
    }

    return snapshot;
}

bool Application::HasAiAlarmRunning() const {
    std::lock_guard<std::mutex> lock(ai_alarm_mutex_);
    return ai_alarm_active_ || ai_alarm_ringing_;
}

void Application::CheckAiAlarmOnClockTick() {
    bool should_trigger = false;
    {
        std::lock_guard<std::mutex> lock(ai_alarm_mutex_);
        should_trigger = ai_alarm_active_ && !ai_alarm_ringing_ && time(nullptr) >= ai_alarm_target_time_;
    }

    if (should_trigger) {
        TriggerAiAlarm();
    }
}

void Application::TriggerAiAlarm() {
    std::string reminder;
    {
        std::lock_guard<std::mutex> lock(ai_alarm_mutex_);
        if (!ai_alarm_active_ || ai_alarm_ringing_) {
            return;
        }

        ai_alarm_active_ = false;
        ai_alarm_ringing_ = true;
        ai_alarm_repeat_remaining_ = 5;
        reminder = ai_alarm_reminder_;
    }

    ESP_LOGI(TAG, "AI alarm triggered: %s", reminder.c_str());

    if (audio_service_suspended_for_feature_) {
        ResumeAudioAfterFeature();
    }

    if (protocol_ != nullptr && protocol_->IsAudioChannelOpened()) {
        protocol_->CloseAudioChannel();
    }

    auto display = Board::GetInstance().GetDisplay();
    const std::string notification = reminder.empty() ? "AI闹钟时间到了" : ("提醒：" + reminder);
    display->ShowNotification(notification.c_str(), 4000);

    HandleAiAlarmRepeat();

    std::lock_guard<std::mutex> lock(ai_alarm_mutex_);
    if (ai_alarm_ringing_ && ai_alarm_repeat_remaining_ > 0 && ai_alarm_repeat_timer_handle_ != nullptr) {
        esp_timer_stop(ai_alarm_repeat_timer_handle_);
        ESP_ERROR_CHECK(esp_timer_start_periodic(ai_alarm_repeat_timer_handle_, 2500 * 1000));
    }
}

void Application::HandleAiAlarmRepeat() {
    std::string reminder;
    bool clear_after_play = false;
    {
        std::lock_guard<std::mutex> lock(ai_alarm_mutex_);
        if (!ai_alarm_ringing_) {
            StopAiAlarmRepeatTimerLocked();
            return;
        }

        if (ai_alarm_repeat_remaining_ <= 0) {
            ClearAiAlarmLocked();
            return;
        }

        reminder = ai_alarm_reminder_;
        --ai_alarm_repeat_remaining_;
        clear_after_play = ai_alarm_repeat_remaining_ <= 0;
        if (clear_after_play) {
            StopAiAlarmRepeatTimerLocked();
        }
    }

    if (protocol_ != nullptr && protocol_->IsAudioChannelOpened()) {
        protocol_->CloseAudioChannel();
    }

    PlaySound(Lang::Sounds::OGG_POPUP);
    auto display = Board::GetInstance().GetDisplay();
    const std::string notification = reminder.empty() ? "AI闹钟时间到了" : ("提醒：" + reminder);
    display->ShowNotification(notification.c_str(), 2500);

    if (clear_after_play) {
        std::lock_guard<std::mutex> lock(ai_alarm_mutex_);
        ClearAiAlarmLocked();
    }
}

void Application::StopAiAlarmRepeatTimerLocked() {
    if (ai_alarm_repeat_timer_handle_ == nullptr) {
        return;
    }

    esp_err_t err = esp_timer_stop(ai_alarm_repeat_timer_handle_);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGW(TAG, "Failed to stop AI alarm repeat timer: %s", esp_err_to_name(err));
    }
}

void Application::ClearAiAlarmLocked() {
    StopAiAlarmRepeatTimerLocked();
    ai_alarm_active_ = false;
    ai_alarm_ringing_ = false;
    ai_alarm_target_time_ = 0;
    ai_alarm_initial_seconds_ = 0;
    ai_alarm_reminder_.clear();
    ai_alarm_repeat_remaining_ = 0;
}

void Application::SetAecMode(AecMode mode) {
    aec_mode_ = mode;
    Schedule([this]() {
        auto& board = Board::GetInstance();
        auto display = board.GetDisplay();
        switch (aec_mode_) {
        case kAecOff:
            audio_service_.EnableDeviceAec(false);
            display->ShowNotification(Lang::Strings::RTC_MODE_OFF);
            break;
        case kAecOnServerSide:
            audio_service_.EnableDeviceAec(false);
            display->ShowNotification(Lang::Strings::RTC_MODE_ON);
            break;
        case kAecOnDeviceSide:
            audio_service_.EnableDeviceAec(true);
            display->ShowNotification(Lang::Strings::RTC_MODE_ON);
            break;
        }

        // If the AEC mode is changed, close the audio channel
        if (protocol_ && protocol_->IsAudioChannelOpened()) {
            protocol_->CloseAudioChannel();
        }
    });
}

void Application::PlaySound(const std::string_view& sound) {
    audio_service_.PlaySound(sound);
}
