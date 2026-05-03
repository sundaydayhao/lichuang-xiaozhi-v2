#include "esp_xiaozhi_protocol.h"

#include "application.h"
#include "backlight.h"
#include "ble_hid.h"
#include "board.h"
#include "display.h"
#include "esp32_camera.h"
#include "lvgl_theme.h"
#include "ui_display.h"

#include <cstring>
#include <exception>
#include <memory>
#include <string>
#include <utility>
#include <esp_log.h>
#include <esp_mcp_data.h>
#include <esp_mcp_engine.h>
#include <esp_mcp_property.h>
#include <esp_mcp_tool.h>
#include <esp_xiaozhi_info.h>

#define TAG "EspXiaozhiProtocol"

namespace {

void AddMcpTool(esp_mcp_t* mcp, esp_mcp_tool_t* tool) {
    if (mcp == nullptr || tool == nullptr) {
        return;
    }
    esp_err_t ret = esp_mcp_add_tool(mcp, tool);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to add MCP tool: %s", esp_err_to_name(ret));
    }
}

std::string JsonToString(cJSON* json) {
    if (json == nullptr) {
        return "{}";
    }
    char* text = cJSON_PrintUnformatted(json);
    std::string result = text != nullptr ? text : "{}";
    if (text != nullptr) {
        cJSON_free(text);
    }
    return result;
}

esp_mcp_value_t GetDeviceStatusCallback(const esp_mcp_property_list_t*) {
    return esp_mcp_value_create_string(Board::GetInstance().GetDeviceStatusJson().c_str());
}

esp_mcp_value_t SetVolumeCallback(const esp_mcp_property_list_t* properties) {
    int volume = esp_mcp_property_list_get_property_int(properties, "volume");
    if (volume < 0 || volume > 100) {
        return esp_mcp_value_create_bool(false);
    }

    auto* codec = Board::GetInstance().GetAudioCodec();
    codec->SetOutputVolume(volume);
    return esp_mcp_value_create_bool(true);
}

esp_mcp_value_t SetBrightnessCallback(const esp_mcp_property_list_t* properties) {
    int brightness = esp_mcp_property_list_get_property_int(properties, "brightness");
    if (brightness < 0 || brightness > 100) {
        return esp_mcp_value_create_bool(false);
    }

    auto* backlight = Board::GetInstance().GetBacklight();
    if (backlight == nullptr) {
        return esp_mcp_value_create_bool(false);
    }

    backlight->SetBrightness(static_cast<uint8_t>(brightness), true);
    return esp_mcp_value_create_bool(true);
}

esp_mcp_value_t SetThemeCallback(const esp_mcp_property_list_t* properties) {
    const char* theme_name = esp_mcp_property_list_get_property_string(properties, "theme");
    if (theme_name == nullptr) {
        return esp_mcp_value_create_bool(false);
    }

    auto* display = Board::GetInstance().GetDisplay();
    auto* theme = LvglThemeManager::GetInstance().GetTheme(theme_name);
    if (display == nullptr || theme == nullptr) {
        return esp_mcp_value_create_bool(false);
    }

    display->SetTheme(theme);
    return esp_mcp_value_create_bool(true);
}

esp_mcp_value_t TakePhotoCallback(const esp_mcp_property_list_t* properties) {
    const char* question = esp_mcp_property_list_get_property_string(properties, "question");
    if (question == nullptr) {
        question = "";
    }

    auto* camera = Board::GetInstance().GetCamera();
    if (camera == nullptr) {
        return esp_mcp_value_create_string("Camera is not ready");
    }

    try {
        auto* esp32_camera = dynamic_cast<Esp32Camera*>(camera);
        std::unique_ptr<Esp32Camera::CaptureLock> capture_lock;
        if (esp32_camera != nullptr) {
            capture_lock = esp32_camera->CaptureForExplain();
            if (!capture_lock || !capture_lock->IsLocked()) {
                return esp_mcp_value_create_string("Failed to capture photo");
            }
        } else if (!camera->Capture()) {
            return esp_mcp_value_create_string("Failed to capture photo");
        }

        std::string result = camera->Explain(question);
        return esp_mcp_value_create_string(result.c_str());
    } catch (const std::exception& e) {
        ESP_LOGW(TAG, "Failed to take photo: %s", e.what());
        return esp_mcp_value_create_string(e.what());
    }
}

UiLcdDisplay* GetUiDisplay() {
    return dynamic_cast<UiLcdDisplay*>(Board::GetInstance().GetDisplay());
}

esp_mcp_value_t RemoteSelfieEnableVoiceControlCallback(const esp_mcp_property_list_t*) {
    auto* display = GetUiDisplay();
    if (display == nullptr || !display->IsRemoteSelfieActive()) {
        return esp_mcp_value_create_string("Remote selfie screen is not active");
    }
    return esp_mcp_value_create_string("remote_selfie_voice_control_ready");
}

esp_mcp_value_t ExecuteRemoteSelfieAction(UiMenuRemoteSelfieAction action, const char* success_text) {
    auto* display = GetUiDisplay();
    if (display == nullptr || !display->IsRemoteSelfieActive()) {
        return esp_mcp_value_create_string("Remote selfie screen is not active");
    }
    if (!display->ExecuteRemoteSelfieAction(action)) {
        return esp_mcp_value_create_string(
            BleHid::GetInstance().IsConnected() ? "Remote selfie action failed" : "Please connect Bluetooth Xiaozhi-HID first");
    }
    return esp_mcp_value_create_string(success_text);
}

esp_mcp_value_t RemoteSelfieSwipeUpCallback(const esp_mcp_property_list_t*) {
    return ExecuteRemoteSelfieAction(UiMenuRemoteSelfieAction::kSwipeUp, "remote_selfie_swipe_up_sent");
}

esp_mcp_value_t RemoteSelfieSwipeDownCallback(const esp_mcp_property_list_t*) {
    return ExecuteRemoteSelfieAction(UiMenuRemoteSelfieAction::kSwipeDown, "remote_selfie_swipe_down_sent");
}

esp_mcp_value_t RemoteSelfieSwipeLeftCallback(const esp_mcp_property_list_t*) {
    return ExecuteRemoteSelfieAction(UiMenuRemoteSelfieAction::kSwipeLeft, "remote_selfie_swipe_left_sent");
}

esp_mcp_value_t RemoteSelfieSwipeRightCallback(const esp_mcp_property_list_t*) {
    return ExecuteRemoteSelfieAction(UiMenuRemoteSelfieAction::kSwipeRight, "remote_selfie_swipe_right_sent");
}

esp_mcp_value_t RemoteSelfieFocusCallback(const esp_mcp_property_list_t*) {
    return ExecuteRemoteSelfieAction(UiMenuRemoteSelfieAction::kFocus, "remote_selfie_focus_sent");
}

esp_mcp_value_t RemoteSelfieTakePhotoCallback(const esp_mcp_property_list_t*) {
    return ExecuteRemoteSelfieAction(UiMenuRemoteSelfieAction::kCapture, "remote_selfie_take_photo_sent");
}

esp_mcp_value_t RemoteSelfieStartRecordingCallback(const esp_mcp_property_list_t*) {
    return ExecuteRemoteSelfieAction(UiMenuRemoteSelfieAction::kCapture, "remote_selfie_start_recording_sent");
}

esp_mcp_value_t RemoteSelfieStopRecordingCallback(const esp_mcp_property_list_t*) {
    return ExecuteRemoteSelfieAction(UiMenuRemoteSelfieAction::kCapture, "remote_selfie_stop_recording_sent");
}

esp_mcp_value_t SetAiAlarmCallback(const esp_mcp_property_list_t* properties) {
    int delay_seconds = esp_mcp_property_list_get_property_int(properties, "delay_seconds");
    const char* reminder = esp_mcp_property_list_get_property_string(properties, "reminder");
    if (reminder == nullptr) {
        reminder = "";
    }

    try {
        std::string result = Application::GetInstance().SetAiAlarmTimer(delay_seconds, reminder);
        return esp_mcp_value_create_string(result.c_str());
    } catch (const std::exception& e) {
        ESP_LOGW(TAG, "Failed to set AI alarm: %s", e.what());
        return esp_mcp_value_create_string(e.what());
    }
}

esp_mcp_value_t CancelAiAlarmCallback(const esp_mcp_property_list_t*) {
    std::string result = Application::GetInstance().CancelAiAlarmTimer();
    return esp_mcp_value_create_string(result.c_str());
}

esp_mcp_value_t GetAiAlarmStatusCallback(const esp_mcp_property_list_t*) {
    auto snapshot = Application::GetInstance().GetAiAlarmSnapshot();
    cJSON* json = cJSON_CreateObject();
    if (json == nullptr) {
        return esp_mcp_value_create_bool(false);
    }

    cJSON_AddBoolToObject(json, "active", snapshot.active);
    cJSON_AddBoolToObject(json, "ringing", snapshot.ringing);
    cJSON_AddNumberToObject(json, "remaining_seconds", snapshot.remaining_seconds);
    cJSON_AddNumberToObject(json, "total_seconds", snapshot.total_seconds);
    cJSON_AddNumberToObject(json, "target_time", static_cast<double>(snapshot.target_time));
    cJSON_AddStringToObject(json, "reminder", snapshot.reminder.c_str());
    cJSON_AddNumberToObject(json, "pending_repeats", snapshot.pending_repeats);

    std::string result = JsonToString(json);
    cJSON_Delete(json);
    return esp_mcp_value_create_string(result.c_str());
}

esp_mcp_t* CreateMcpEngine() {
    esp_mcp_t* mcp = nullptr;
    if (esp_mcp_create(&mcp) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create MCP engine");
        return nullptr;
    }

    esp_mcp_tool_t* tool = esp_mcp_tool_create(
        "self.get_device_status",
        "Get device status including audio, screen, camera, and network information",
        GetDeviceStatusCallback);
    AddMcpTool(mcp, tool);

    tool = esp_mcp_tool_create(
        "self.audio_speaker.set_volume",
        "Set audio speaker volume from 0 to 100",
        SetVolumeCallback);
    esp_mcp_tool_add_property(tool, esp_mcp_property_create_with_range("volume", 0, 100));
    AddMcpTool(mcp, tool);

    tool = esp_mcp_tool_create(
        "self.screen.set_brightness",
        "Set screen brightness from 0 to 100",
        SetBrightnessCallback);
    esp_mcp_tool_add_property(tool, esp_mcp_property_create_with_range("brightness", 0, 100));
    AddMcpTool(mcp, tool);

    tool = esp_mcp_tool_create(
        "self.screen.set_theme",
        "Set screen theme. Supported values are light and dark",
        SetThemeCallback);
    esp_mcp_tool_add_property(tool, esp_mcp_property_create_with_string("theme", "light"));
    AddMcpTool(mcp, tool);

    tool = esp_mcp_tool_create(
        "self.camera.take_photo",
        "Take a photo and explain it. Use this after the user asks you to see something",
        TakePhotoCallback);
    esp_mcp_tool_add_property(tool, esp_mcp_property_create_with_string("question", ""));
    AddMcpTool(mcp, tool);

    tool = esp_mcp_tool_create(
        "self.remote_selfie.enable_voice_control",
        "Enable voice control guidance for remote selfie mode",
        RemoteSelfieEnableVoiceControlCallback);
    AddMcpTool(mcp, tool);

    tool = esp_mcp_tool_create(
        "self.remote_selfie.swipe_up",
        "Swipe the connected phone screen upward in remote selfie mode",
        RemoteSelfieSwipeUpCallback);
    AddMcpTool(mcp, tool);

    tool = esp_mcp_tool_create(
        "self.remote_selfie.swipe_down",
        "Swipe the connected phone screen downward in remote selfie mode",
        RemoteSelfieSwipeDownCallback);
    AddMcpTool(mcp, tool);

    tool = esp_mcp_tool_create(
        "self.remote_selfie.swipe_left",
        "Swipe the connected phone screen left in remote selfie mode",
        RemoteSelfieSwipeLeftCallback);
    AddMcpTool(mcp, tool);

    tool = esp_mcp_tool_create(
        "self.remote_selfie.swipe_right",
        "Swipe the connected phone screen right in remote selfie mode",
        RemoteSelfieSwipeRightCallback);
    AddMcpTool(mcp, tool);

    tool = esp_mcp_tool_create(
        "self.remote_selfie.focus",
        "Tap the center focus point on the connected phone screen in remote selfie mode",
        RemoteSelfieFocusCallback);
    AddMcpTool(mcp, tool);

    tool = esp_mcp_tool_create(
        "self.remote_selfie.take_photo",
        "Press the phone camera shutter once in remote selfie mode to take a photo",
        RemoteSelfieTakePhotoCallback);
    AddMcpTool(mcp, tool);

    tool = esp_mcp_tool_create(
        "self.remote_selfie.start_recording",
        "Press the phone camera shutter once in remote selfie mode to start recording",
        RemoteSelfieStartRecordingCallback);
    AddMcpTool(mcp, tool);

    tool = esp_mcp_tool_create(
        "self.remote_selfie.stop_recording",
        "Press the phone camera shutter once in remote selfie mode to stop recording",
        RemoteSelfieStopRecordingCallback);
    AddMcpTool(mcp, tool);

    tool = esp_mcp_tool_create(
        "self.ai_alarm.set_timer",
        "Set a countdown AI alarm with delay_seconds and reminder",
        SetAiAlarmCallback);
    esp_mcp_tool_add_property(tool, esp_mcp_property_create_with_range("delay_seconds", 1, 24 * 60 * 60));
    esp_mcp_tool_add_property(tool, esp_mcp_property_create_with_string("reminder", ""));
    AddMcpTool(mcp, tool);

    tool = esp_mcp_tool_create(
        "self.ai_alarm.cancel_timer",
        "Cancel the currently running AI alarm timer",
        CancelAiAlarmCallback);
    AddMcpTool(mcp, tool);

    tool = esp_mcp_tool_create(
        "self.ai_alarm.get_status",
        "Get the current AI alarm timer status",
        GetAiAlarmStatusCallback);
    AddMcpTool(mcp, tool);

    return mcp;
}

int ToSdkListeningMode(ListeningMode mode) {
    switch (mode) {
    case kListeningModeRealtime:
        return ESP_XIAOZHI_CHAT_LISTENING_MODE_REALTIME;
    case kListeningModeManualStop:
        return ESP_XIAOZHI_CHAT_LISTENING_MODE_MANUAL;
    case kListeningModeAutoStop:
    default:
        return ESP_XIAOZHI_CHAT_LISTENING_MODE_AUTO;
    }
}

esp_xiaozhi_chat_abort_speaking_reason_t ToSdkAbortReason(AbortReason reason) {
    switch (reason) {
    case kAbortReasonWakeWordDetected:
        return ESP_XIAOZHI_CHAT_ABORT_SPEAKING_REASON_WAKE_WORD_DETECTED;
    case kAbortReasonNone:
    default:
        return ESP_XIAOZHI_CHAT_ABORT_SPEAKING_REASON_STOP_LISTENING;
    }
}

} // namespace

EspXiaozhiProtocol::EspXiaozhiProtocol() {
    server_sample_rate_ = audio_.sample_rate;
    server_frame_duration_ = audio_.frame_duration;
}

EspXiaozhiProtocol::~EspXiaozhiProtocol() {
    if (chat_ != 0) {
        esp_xiaozhi_chat_stop(chat_);
        esp_xiaozhi_chat_deinit(chat_);
        chat_ = 0;
    }
    if (event_registered_) {
        esp_event_handler_unregister(ESP_XIAOZHI_CHAT_EVENTS, ESP_EVENT_ANY_ID, SystemEventHandler);
        event_registered_ = false;
    }
}

bool EspXiaozhiProtocol::Start() {
    esp_err_t ret = esp_event_handler_register(
        ESP_XIAOZHI_CHAT_EVENTS, ESP_EVENT_ANY_ID, SystemEventHandler, this);
    if (ret != ESP_OK) {
        SetError(std::string("Failed to register esp_xiaozhi event handler: ") + esp_err_to_name(ret));
        return false;
    }
    event_registered_ = true;

    esp_xiaozhi_chat_info_t info = {};
    ret = esp_xiaozhi_chat_get_info(&info);
    if (ret != ESP_OK) {
        SetError(std::string("Failed to get esp_xiaozhi info: ") + esp_err_to_name(ret));
        return false;
    }

    esp_mcp_t* mcp = CreateMcpEngine();
    esp_xiaozhi_chat_config_t config = ESP_XIAOZHI_CHAT_DEFAULT_CONFIG();
    config.audio_type = ESP_XIAOZHI_CHAT_AUDIO_TYPE_OPUS;
    config.audio_callback = AudioCallback;
    config.audio_callback_ctx = this;
    config.event_callback = ChatEventCallback;
    config.event_callback_ctx = this;
    config.mcp_engine = mcp;
    config.owns_mcp_engine = true;
    config.has_mqtt_config = info.has_mqtt_config;
    config.has_websocket_config = info.has_websocket_config;

    ret = esp_xiaozhi_chat_init(&config, &chat_);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to init esp_xiaozhi chat: %s", esp_err_to_name(ret));
        if (mcp != nullptr) {
            esp_mcp_destroy(mcp);
        }
        esp_xiaozhi_chat_free_info(&info);
        SetError(std::string("Failed to init esp_xiaozhi chat: ") + esp_err_to_name(ret));
        return false;
    }

    ret = esp_xiaozhi_chat_start(chat_);
    esp_xiaozhi_chat_free_info(&info);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start esp_xiaozhi chat: %s", esp_err_to_name(ret));
        SetError(std::string("Failed to start esp_xiaozhi chat: ") + esp_err_to_name(ret));
        return false;
    }

    return true;
}

bool EspXiaozhiProtocol::OpenAudioChannel() {
    if (chat_ == 0) {
        return false;
    }

    esp_err_t ret = esp_xiaozhi_chat_open_audio_channel(chat_, &audio_, nullptr, 0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open audio channel: %s", esp_err_to_name(ret));
        SetError(std::string("Failed to open audio channel: ") + esp_err_to_name(ret));
        return false;
    }

    audio_channel_opened_ = true;
    if (on_audio_channel_opened_ != nullptr) {
        on_audio_channel_opened_();
    }
    return true;
}

void EspXiaozhiProtocol::CloseAudioChannel() {
    if (chat_ == 0) {
        return;
    }

    esp_err_t ret = esp_xiaozhi_chat_close_audio_channel(chat_);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to close audio channel: %s", esp_err_to_name(ret));
    }

    if (audio_channel_opened_) {
        audio_channel_opened_ = false;
        if (on_audio_channel_closed_ != nullptr) {
            on_audio_channel_closed_();
        }
    }
}

bool EspXiaozhiProtocol::IsAudioChannelOpened() const {
    return audio_channel_opened_;
}

bool EspXiaozhiProtocol::SendAudio(std::unique_ptr<AudioStreamPacket> packet) {
    if (chat_ == 0 || packet == nullptr || packet->payload.empty()) {
        return false;
    }

    esp_err_t ret = esp_xiaozhi_chat_send_audio_data(
        chat_, reinterpret_cast<const char*>(packet->payload.data()), packet->payload.size());
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to send audio: %s", esp_err_to_name(ret));
        return false;
    }
    return true;
}

void EspXiaozhiProtocol::SendWakeWordDetected(const std::string& wake_word) {
    if (chat_ == 0 || wake_word.empty()) {
        return;
    }
    esp_err_t ret = esp_xiaozhi_chat_send_wake_word(chat_, wake_word.c_str());
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to send wake word: %s", esp_err_to_name(ret));
    }
}

void EspXiaozhiProtocol::SendStartListening(ListeningMode mode) {
    if (chat_ == 0) {
        return;
    }
    esp_err_t ret = esp_xiaozhi_chat_send_start_listening(chat_, ToSdkListeningMode(mode));
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to send start listening: %s", esp_err_to_name(ret));
    }
}

void EspXiaozhiProtocol::SendStopListening() {
    if (chat_ == 0) {
        return;
    }
    esp_err_t ret = esp_xiaozhi_chat_send_stop_listening(chat_);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to send stop listening: %s", esp_err_to_name(ret));
    }
}

void EspXiaozhiProtocol::SendAbortSpeaking(AbortReason reason) {
    if (chat_ == 0) {
        return;
    }
    esp_err_t ret = esp_xiaozhi_chat_send_abort_speaking(chat_, ToSdkAbortReason(reason));
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to abort speaking: %s", esp_err_to_name(ret));
    }
}

void EspXiaozhiProtocol::HandleAudio(const uint8_t* data, int len) {
    if (data == nullptr || len <= 0 || on_incoming_audio_ == nullptr) {
        return;
    }

    auto packet = std::make_unique<AudioStreamPacket>();
    packet->sample_rate = audio_.sample_rate;
    packet->frame_duration = audio_.frame_duration;
    packet->timestamp = 0;
    packet->payload.assign(data, data + len);
    on_incoming_audio_(std::move(packet));
}

void EspXiaozhiProtocol::HandleChatEvent(esp_xiaozhi_chat_event_t event, void* event_data) {
    switch (event) {
    case ESP_XIAOZHI_CHAT_EVENT_CHAT_SPEECH_STARTED:
        break;
    case ESP_XIAOZHI_CHAT_EVENT_CHAT_SPEECH_STOPPED:
        break;
    case ESP_XIAOZHI_CHAT_EVENT_CHAT_TTS_STATE: {
        auto* tts = static_cast<esp_xiaozhi_chat_tts_state_t*>(event_data);
        if (tts == nullptr) {
            break;
        }
        switch (tts->state) {
        case ESP_XIAOZHI_CHAT_TTS_STATE_START:
            EmitTtsState("start");
            break;
        case ESP_XIAOZHI_CHAT_TTS_STATE_STOP:
            EmitTtsState("stop");
            break;
        case ESP_XIAOZHI_CHAT_TTS_STATE_SENTENCE_START:
            EmitTtsState("sentence_start", tts->text);
            break;
        }
        break;
    }
    case ESP_XIAOZHI_CHAT_EVENT_CHAT_TEXT: {
        auto* text_data = static_cast<esp_xiaozhi_chat_text_data_t*>(event_data);
        if (text_data == nullptr || text_data->text == nullptr) {
            break;
        }
        if (text_data->role == ESP_XIAOZHI_CHAT_TEXT_ROLE_USER) {
            EmitChatText("user", text_data->text);
        }
        break;
    }
    case ESP_XIAOZHI_CHAT_EVENT_CHAT_EMOJI: {
        const char* emotion = static_cast<const char*>(event_data);
        if (emotion == nullptr) {
            break;
        }
        cJSON* root = cJSON_CreateObject();
        cJSON_AddStringToObject(root, "type", "llm");
        cJSON_AddStringToObject(root, "emotion", emotion);
        EmitJson(root);
        break;
    }
    case ESP_XIAOZHI_CHAT_EVENT_CHAT_SYSTEM_CMD: {
        const char* command = static_cast<const char*>(event_data);
        if (command == nullptr) {
            break;
        }
        cJSON* root = cJSON_CreateObject();
        cJSON_AddStringToObject(root, "type", "system");
        cJSON_AddStringToObject(root, "command", command);
        EmitJson(root);
        break;
    }
    case ESP_XIAOZHI_CHAT_EVENT_CHAT_ERROR: {
        auto* info = static_cast<esp_xiaozhi_chat_error_info_t*>(event_data);
        std::string message = "esp_xiaozhi chat error";
        if (info != nullptr) {
            message += ": ";
            message += esp_err_to_name(info->code);
            if (info->source != nullptr) {
                message += " (";
                message += info->source;
                message += ")";
            }
        }
        SetError(message);
        break;
    }
    default:
        break;
    }
}

void EspXiaozhiProtocol::HandleSystemEvent(int32_t event_id, void*) {
    switch (event_id) {
    case ESP_XIAOZHI_CHAT_EVENT_CONNECTED:
        if (on_connected_ != nullptr) {
            on_connected_();
        }
        break;
    case ESP_XIAOZHI_CHAT_EVENT_DISCONNECTED:
        if (audio_channel_opened_) {
            audio_channel_opened_ = false;
            if (on_audio_channel_closed_ != nullptr) {
                on_audio_channel_closed_();
            }
        }
        if (on_disconnected_ != nullptr) {
            on_disconnected_();
        }
        break;
    case ESP_XIAOZHI_CHAT_EVENT_AUDIO_CHANNEL_OPENED:
        if (!audio_channel_opened_) {
            audio_channel_opened_ = true;
            if (on_audio_channel_opened_ != nullptr) {
                on_audio_channel_opened_();
            }
        }
        break;
    case ESP_XIAOZHI_CHAT_EVENT_AUDIO_CHANNEL_CLOSED:
    case ESP_XIAOZHI_CHAT_EVENT_SERVER_GOODBYE:
        if (audio_channel_opened_) {
            audio_channel_opened_ = false;
            if (on_audio_channel_closed_ != nullptr) {
                on_audio_channel_closed_();
            }
        }
        break;
    default:
        break;
    }
}

void EspXiaozhiProtocol::EmitJson(cJSON* root) {
    if (root == nullptr) {
        return;
    }

    if (on_incoming_json_ != nullptr) {
        on_incoming_json_(root);
    }
    cJSON_Delete(root);
}

void EspXiaozhiProtocol::EmitTtsState(const char* state, const char* text) {
    cJSON* root = cJSON_CreateObject();
    if (root == nullptr) {
        return;
    }
    cJSON_AddStringToObject(root, "type", "tts");
    cJSON_AddStringToObject(root, "state", state);
    if (text != nullptr) {
        cJSON_AddStringToObject(root, "text", text);
    }
    EmitJson(root);
}

void EspXiaozhiProtocol::EmitChatText(const char* role, const char* text) {
    if (role == nullptr || text == nullptr || strcmp(role, "user") != 0) {
        return;
    }

    cJSON* root = cJSON_CreateObject();
    if (root == nullptr) {
        return;
    }

    cJSON_AddStringToObject(root, "type", "stt");
    cJSON_AddStringToObject(root, "text", text);
    EmitJson(root);
}

void EspXiaozhiProtocol::AudioCallback(const uint8_t* data, int len, void* ctx) {
    auto* protocol = static_cast<EspXiaozhiProtocol*>(ctx);
    if (protocol != nullptr) {
        protocol->HandleAudio(data, len);
    }
}

void EspXiaozhiProtocol::ChatEventCallback(esp_xiaozhi_chat_event_t event, void* event_data, void* ctx) {
    auto* protocol = static_cast<EspXiaozhiProtocol*>(ctx);
    if (protocol != nullptr) {
        protocol->HandleChatEvent(event, event_data);
    }
}

void EspXiaozhiProtocol::SystemEventHandler(
    void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {
    (void)event_base;
    auto* protocol = static_cast<EspXiaozhiProtocol*>(arg);
    if (protocol != nullptr) {
        protocol->HandleSystemEvent(event_id, event_data);
    }
}
