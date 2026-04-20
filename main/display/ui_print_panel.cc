#include "ui_print_panel.h"

#include "application.h"
#include "board.h"
#include "lvgl_display/lvgl_image.h"
#include "lvgl_theme.h"
#include "ui_display.h"

#include <cJSON.h>
#include <esp_heap_caps.h>
#include <esp_log.h>
#include <freertos/idf_additions.h>
#include <jpeg_decoder.h>

#include <chrono>
#include <cctype>
#include <cstring>
#include <memory>
#include <new>
#include <string>
#include <thread>

namespace {

constexpr char kAiPrintTag[] = "AiPrintFeature";
constexpr char kLaimiaoGenerateUrl[] = "https://laimiaoai.com/api/apply/generate_image";
constexpr char kLaimiaoImageQueryUrl[] = "https://laimiaoai.com/api/apply/get_image?prompt_id=";
constexpr char kLaimiaoApiSecret[] =
    "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9."
    "eyJ1aWQiOjIwMjExMjM0OTY4NDI3NjAxOTIsInRlYW1faWQiOjIwMjExMjM4OTMxMjQ3OTY0MTYs"
    "IndvcmtmbG93X2lkIjoyMDIxMTMyOTQxNDA4NzM1MjMyLCJ3b3JrZmxvd19uYW1lIjoiWl9pbWFn"
    "ZV9cdTY1ODdcdTc1MWZcdTU2ZmUiLCJleHAiOjQxMDI0NDQ3OTl9.ovq_-OdbXWF1n2k0r78rC1Que8uzj02lcT90B8pkdlk";
constexpr int kAiPrintImageWidth = 320;
constexpr int kAiPrintImageHeight = 320;
constexpr int kAiPrintHttpTimeoutMs = 15000;
constexpr int kAiPrintPollIntervalMs = 1500;
constexpr int kAiPrintMaxPollAttempts = 20;
constexpr size_t kAiPrintJpegWorkBufferSize = 16 * 1024;
constexpr uint32_t kAiPrintTaskStackSize = 12 * 1024;
constexpr UBaseType_t kAiPrintTaskPriority = 3;
constexpr int kAiPrintOptionId = 7;
struct AiPrintTaskContext {
    UiLcdDisplay* display = nullptr;
    std::string prompt;
};

std::string TrimCopy(const std::string& text) {
    size_t start = 0;
    while (start < text.size() && std::isspace(static_cast<unsigned char>(text[start])) != 0) {
        ++start;
    }

    size_t end = text.size();
    while (end > start && std::isspace(static_cast<unsigned char>(text[end - 1])) != 0) {
        --end;
    }

    return text.substr(start, end - start);
}

std::string BuildJsonBody(const std::string& prompt) {
    cJSON* root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "api_secret", kLaimiaoApiSecret);
    cJSON_AddStringToObject(root, "prompt", prompt.c_str());
    cJSON_AddNumberToObject(root, "width", kAiPrintImageWidth);
    cJSON_AddNumberToObject(root, "height", kAiPrintImageHeight);

    char* json = cJSON_PrintUnformatted(root);
    std::string body = json != nullptr ? json : "";
    if (json != nullptr) {
        cJSON_free(json);
    }
    cJSON_Delete(root);
    return body;
}

std::string BuildHttpError(const char* action, int status_code, const std::string& body) {
    std::string message = action;
    message += "失败，HTTP ";
    message += std::to_string(status_code);
    if (!body.empty()) {
        message += "，响应：";
        message += body;
    }
    return message;
}

void AlignAiPrintMessageBelowEmoji(lv_obj_t* message_label, lv_obj_t* emoji_label,
                                   lv_obj_t* emoji_image) {
    if (message_label == nullptr) {
        return;
    }

    lv_obj_t* anchor = emoji_label;
    if (emoji_image != nullptr && !lv_obj_has_flag(emoji_image, LV_OBJ_FLAG_HIDDEN)) {
        anchor = emoji_image;
    }

    lv_obj_add_flag(message_label, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_clear_flag(message_label, LV_OBJ_FLAG_HIDDEN);
    if (anchor != nullptr) {
        lv_obj_align_to(message_label, anchor, LV_ALIGN_OUT_BOTTOM_MID, 0, 18);
    } else {
        lv_obj_align(message_label, LV_ALIGN_CENTER, 0, 72);
    }
    lv_obj_move_foreground(message_label);
}

bool RequestPromptId(NetworkInterface* network, const std::string& prompt,
                     std::string& prompt_id, std::string& error_message) {
    auto http = network->CreateHttp(2);
    if (http == nullptr) {
        error_message = "无法创建 HTTP 连接";
        return false;
    }

    http->SetTimeout(kAiPrintHttpTimeoutMs);
    http->SetHeader("Content-Type", "application/json");
    http->SetHeader("Accept", "application/json");
    http->SetContent(BuildJsonBody(prompt));

    if (!http->Open("POST", kLaimiaoGenerateUrl)) {
        error_message = "无法连接来秒生图接口";
        http->Close();
        return false;
    }

    const int status_code = http->GetStatusCode();
    const std::string body = http->ReadAll();
    http->Close();

    if (status_code != 200) {
        error_message = BuildHttpError("提交生图请求", status_code, body);
        return false;
    }

    cJSON* root = cJSON_Parse(body.c_str());
    if (root == nullptr) {
        error_message = "生图接口返回了无法解析的 JSON";
        return false;
    }

    cJSON* prompt_id_item = cJSON_GetObjectItem(root, "prompt_id");
    if (cJSON_IsString(prompt_id_item) && prompt_id_item->valuestring[0] != '\0') {
        prompt_id = prompt_id_item->valuestring;
        cJSON_Delete(root);
        return true;
    }

    error_message = "生图接口没有返回 prompt_id";
    cJSON_Delete(root);
    return false;
}

bool PollImageUrl(NetworkInterface* network, const std::string& prompt_id,
                  std::string& image_url, std::string& error_message) {
    for (int attempt = 0; attempt < kAiPrintMaxPollAttempts; ++attempt) {
        auto http = network->CreateHttp(2);
        if (http == nullptr) {
            error_message = "无法创建查询图片结果的 HTTP 连接";
            return false;
        }

        http->SetTimeout(kAiPrintHttpTimeoutMs);
        http->SetHeader("Accept", "application/json");
        const std::string request_url = std::string(kLaimiaoImageQueryUrl) + prompt_id;
        if (!http->Open("GET", request_url)) {
            error_message = "无法查询来秒生图结果";
            http->Close();
            return false;
        }

        const int status_code = http->GetStatusCode();
        const std::string body = http->ReadAll();
        http->Close();

        if (status_code != 200) {
            error_message = BuildHttpError("查询图片结果", status_code, body);
            return false;
        }

        cJSON* root = cJSON_Parse(body.c_str());
        if (root == nullptr) {
            error_message = "查询图片结果时收到了无效 JSON";
            return false;
        }

        cJSON* status = cJSON_GetObjectItem(root, "status");
        if (cJSON_IsString(status) && strcmp(status->valuestring, "success") == 0) {
            cJSON* message = cJSON_GetObjectItem(root, "message");
            cJSON* first = cJSON_IsArray(message) ? cJSON_GetArrayItem(message, 0) : nullptr;
            if (cJSON_IsString(first) && first->valuestring[0] != '\0') {
                image_url = first->valuestring;
                cJSON_Delete(root);
                return true;
            }

            error_message = "图片生成成功，但返回的图片地址为空";
            cJSON_Delete(root);
            return false;
        }

        cJSON_Delete(root);
        std::this_thread::sleep_for(std::chrono::milliseconds(kAiPrintPollIntervalMs));
    }

    error_message = "图片生成超时，请稍后再试";
    return false;
}

bool DownloadImageBytes(NetworkInterface* network, const std::string& image_url,
                        std::string& image_bytes, std::string& error_message) {
    auto http = network->CreateHttp(2);
    if (http == nullptr) {
        error_message = "无法创建图片下载连接";
        return false;
    }

    http->SetTimeout(kAiPrintHttpTimeoutMs);
    if (!http->Open("GET", image_url)) {
        error_message = "无法下载生成后的图片";
        http->Close();
        return false;
    }

    const int status_code = http->GetStatusCode();
    if (status_code != 200) {
        http->Close();
        error_message = BuildHttpError("下载图片", status_code, "");
        return false;
    }

    image_bytes.clear();
    const size_t body_length = http->GetBodyLength();
    if (body_length > 0) {
        image_bytes.reserve(body_length);
    }

    char buffer[1024];
    while (true) {
        const int ret = http->Read(buffer, sizeof(buffer));
        if (ret < 0) {
            http->Close();
            image_bytes.clear();
            error_message = "下载图片时读取响应体失败";
            return false;
        }
        if (ret == 0) {
            break;
        }
        image_bytes.append(buffer, ret);
    }
    http->Close();

    if (image_bytes.empty()) {
        error_message = "图片下载成功，但内容为空";
        return false;
    }

    return true;
}

std::unique_ptr<LvglImage> DecodeJpegToPreviewImage(const std::string& jpeg_bytes) {
    auto* jpeg_input = static_cast<uint8_t*>(AllocateImageBuffer(jpeg_bytes.size()));
    if (jpeg_input == nullptr) {
        return nullptr;
    }
    memcpy(jpeg_input, jpeg_bytes.data(), jpeg_bytes.size());

    auto* jpeg_workbuf = static_cast<uint8_t*>(AllocateImageBuffer(kAiPrintJpegWorkBufferSize));
    if (jpeg_workbuf == nullptr) {
        FreeImageBuffer(jpeg_input);
        return nullptr;
    }

    esp_jpeg_image_cfg_t jpeg_cfg = {};
    jpeg_cfg.indata = jpeg_input;
    jpeg_cfg.indata_size = static_cast<uint32_t>(jpeg_bytes.size());
    jpeg_cfg.out_format = JPEG_IMAGE_FORMAT_RGB565;
    jpeg_cfg.out_scale = JPEG_IMAGE_SCALE_0;
    jpeg_cfg.advanced.working_buffer = jpeg_workbuf;
    jpeg_cfg.advanced.working_buffer_size = kAiPrintJpegWorkBufferSize;

    esp_jpeg_image_output_t output = {};
    if (esp_jpeg_get_image_info(&jpeg_cfg, &output) != ESP_OK || output.output_len == 0) {
        FreeImageBuffer(jpeg_workbuf);
        FreeImageBuffer(jpeg_input);
        return nullptr;
    }

    auto* decoded = static_cast<uint8_t*>(AllocateImageBuffer(output.output_len));
    if (decoded == nullptr) {
        FreeImageBuffer(jpeg_workbuf);
        FreeImageBuffer(jpeg_input);
        return nullptr;
    }

    jpeg_cfg.outbuf = decoded;
    jpeg_cfg.outbuf_size = static_cast<uint32_t>(output.output_len);
    const bool decode_ok = esp_jpeg_decode(&jpeg_cfg, &output) == ESP_OK;
    FreeImageBuffer(jpeg_workbuf);
    FreeImageBuffer(jpeg_input);
    if (!decode_ok) {
        FreeImageBuffer(decoded);
        return nullptr;
    }

    return std::make_unique<LvglAllocatedImage>(
        decoded, output.output_len, output.width, output.height, output.width * 2,
        LV_COLOR_FORMAT_RGB565);
}

} // namespace

bool UiLcdDisplay::IsAiPrintActive() const {
    return current_feature_option_id_ == kAiPrintOptionId;
}

bool UiLcdDisplay::HandleFeatureSpeechRecognized(const std::string& text) {
    MarkDialogueScenePromptCaptured();
    if (HandleRemoteSelfieSpeechRecognized(text)) {
        return true;
    }
    return UiPrintPanel::HandleSpeechRecognized(*this, text);
}

lv_obj_t* UiPrintPanel::EnsureMessageLabel(UiLcdDisplay& display) {
    if (display.ai_print_message_label_ != nullptr &&
        lv_obj_is_valid(display.ai_print_message_label_)) {
        return display.ai_print_message_label_;
    }

    lv_obj_t* parent = display.ai_screen_ != nullptr ? display.ai_screen_ : display.content_;
    if (parent == nullptr) {
        return nullptr;
    }

    auto* label = lv_label_create(parent);
    lv_obj_set_width(label, display.width_ * 9 / 10);
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(
        label, static_cast<LvglTheme*>(display.GetTheme())->text_color(), 0);
    lv_obj_add_flag(label, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_add_flag(label, LV_OBJ_FLAG_FLOATING);
    lv_obj_clear_flag(label, LV_OBJ_FLAG_CLICKABLE);
    lv_label_set_text(label, "");
    lv_obj_add_flag(label, LV_OBJ_FLAG_HIDDEN);
    display.ai_print_message_label_ = label;
    return label;
}

void UiPrintPanel::SetMessageLabel(UiLcdDisplay& display, const char* text) {
    lv_obj_t* label = EnsureMessageLabel(display);
    if (label == nullptr) {
        return;
    }

    if (text == nullptr || text[0] == '\0') {
        lv_label_set_text(label, "");
        lv_obj_add_flag(label, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    lv_label_set_text(label, text);
    AlignAiPrintMessageBelowEmoji(label, display.emoji_label_, display.emoji_image_);
}

void UiLcdDisplay::ShowAiPrintListeningState() {
    DisplayLockGuard lock(this);
    if (preview_timer_ != nullptr) {
        esp_timer_stop(preview_timer_);
    }
    preview_image_cached_.reset();

    if (preview_image_ != nullptr) {
        lv_obj_clear_flag(preview_image_, LV_OBJ_FLAG_IGNORE_LAYOUT);
        lv_obj_add_flag(preview_image_, LV_OBJ_FLAG_HIDDEN);
    }
    if (feature_background_image_ != nullptr) {
        lv_obj_add_flag(feature_background_image_, LV_OBJ_FLAG_HIDDEN);
    }
    if (emoji_box_ != nullptr) {
        lv_obj_remove_flag(emoji_box_, LV_OBJ_FLAG_HIDDEN);
    }
    if (chat_message_label_ != nullptr && lv_obj_is_valid(chat_message_label_)) {
        lv_label_set_text(chat_message_label_, "");
    }
    UiPrintPanel::SetMessageLabel(*this, "请直接说你想要画的内容\n例如：使用线条风格画一只带围巾的大熊猫");
}

void UiLcdDisplay::ShowAiPrintGeneratingState() {
    DisplayLockGuard lock(this);
    if (preview_timer_ != nullptr) {
        esp_timer_stop(preview_timer_);
    }
    preview_image_cached_.reset();

    if (preview_image_ != nullptr) {
        lv_obj_clear_flag(preview_image_, LV_OBJ_FLAG_IGNORE_LAYOUT);
        lv_obj_add_flag(preview_image_, LV_OBJ_FLAG_HIDDEN);
    }
    if (feature_background_image_ != nullptr) {
        lv_obj_add_flag(feature_background_image_, LV_OBJ_FLAG_HIDDEN);
    }
    if (emoji_box_ != nullptr) {
        lv_obj_remove_flag(emoji_box_, LV_OBJ_FLAG_HIDDEN);
    }
    if (chat_message_label_ != nullptr && lv_obj_is_valid(chat_message_label_)) {
        lv_label_set_text(chat_message_label_, "");
    }
    UiPrintPanel::SetMessageLabel(*this, "正在生成图片，请稍候...");
}

void UiLcdDisplay::ShowAiPrintErrorState(const std::string& message) {
    DisplayLockGuard lock(this);
    if (preview_timer_ != nullptr) {
        esp_timer_stop(preview_timer_);
    }
    preview_image_cached_.reset();

    if (preview_image_ != nullptr) {
        lv_obj_clear_flag(preview_image_, LV_OBJ_FLAG_IGNORE_LAYOUT);
        lv_obj_add_flag(preview_image_, LV_OBJ_FLAG_HIDDEN);
    }
    if (feature_background_image_ != nullptr) {
        lv_obj_add_flag(feature_background_image_, LV_OBJ_FLAG_HIDDEN);
    }
    if (emoji_box_ != nullptr) {
        lv_obj_remove_flag(emoji_box_, LV_OBJ_FLAG_HIDDEN);
    }
    if (chat_message_label_ != nullptr && lv_obj_is_valid(chat_message_label_)) {
        lv_label_set_text(chat_message_label_, "");
    }
    UiPrintPanel::SetMessageLabel(*this, message.c_str());
}

void UiLcdDisplay::ShowAiPrintResultImage(std::unique_ptr<LvglImage> image) {
    if (image == nullptr) {
        ShowAiPrintErrorState("图片生成成功\n但设备解码失败");
        return;
    }

    DisplayLockGuard lock(this);
    if (preview_timer_ != nullptr) {
        esp_timer_stop(preview_timer_);
    }

    preview_image_cached_ = std::move(image);
    if (preview_image_ == nullptr) {
        return;
    }

    auto img_dsc = preview_image_cached_->image_dsc();
    lv_image_set_src(preview_image_, img_dsc);

    const lv_coord_t max_width = width_;
    const lv_coord_t max_height = height_ - 27;
    lv_coord_t scale = 256;
    if (img_dsc->header.w > 0 && img_dsc->header.h > 0) {
        const lv_coord_t scale_w = (max_width * 256) / img_dsc->header.w;
        const lv_coord_t scale_h = (max_height * 256) / img_dsc->header.h;
        scale = std::min(scale_w, scale_h);
        if (scale > 256) {
            scale = 256;
        }
        if (scale <= 0) {
            scale = 1;
        }
    }

    lv_image_set_scale(preview_image_, scale);
    lv_obj_add_flag(preview_image_, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_align(preview_image_, LV_ALIGN_CENTER, 0, 0);
    lv_obj_remove_flag(preview_image_, LV_OBJ_FLAG_HIDDEN);

    if (feature_background_image_ != nullptr) {
        lv_obj_add_flag(feature_background_image_, LV_OBJ_FLAG_HIDDEN);
    }
    if (emoji_box_ != nullptr) {
        lv_obj_add_flag(emoji_box_, LV_OBJ_FLAG_HIDDEN);
    }
    HideAiPrintMessageLabel();
    if (chat_message_label_ != nullptr && lv_obj_is_valid(chat_message_label_)) {
        lv_label_set_text(chat_message_label_, "");
    }
}

bool UiPrintPanel::HandleSpeechRecognized(UiLcdDisplay& display, const std::string& text) {
    if (!display.IsAiPrintActive()) {
        return false;
    }

    const std::string prompt = BuildPrompt(text);
    if (prompt.empty()) {
        Application::GetInstance().Schedule([display_ptr = &display]() {
            if (display_ptr->IsAiPrintActive()) {
                display_ptr->ShowAiPrintErrorState("没有识别到绘图内容\n请再试一次");
            }
        });
        return true;
    }

    StartGeneration(display, prompt);
    return true;
}

std::string UiPrintPanel::BuildPrompt(const std::string& text) {
    std::string prompt = TrimCopy(text);
    if (prompt.empty()) {
        return "";
    }

    if (prompt.find("画") == std::string::npos &&
        prompt.find("生成") == std::string::npos &&
        prompt.find("图片") == std::string::npos &&
        prompt.find("图") == std::string::npos) {
        prompt += "，生成一张适合儿童观看的插画";
    }

    return prompt;
}

void UiPrintPanel::StartGeneration(UiLcdDisplay& display, const std::string& prompt) {
    bool expected = false;
    if (!display.ai_print_generation_running_.compare_exchange_strong(expected, true)) {
        Application::GetInstance().Schedule([display_ptr = &display]() {
            if (display_ptr->IsAiPrintActive()) {
                display_ptr->ShowAiPrintErrorState("上一张图片还在生成\n请稍候再试");
            }
        });
        return;
    }

    Application::GetInstance().Schedule([display_ptr = &display]() {
        if (!display_ptr->IsAiPrintActive()) {
            display_ptr->ai_print_generation_running_ = false;
            return;
        }
        display_ptr->ShowAiPrintGeneratingState();
    });

    auto* context = new (std::nothrow) AiPrintTaskContext{
        .display = &display,
        .prompt = prompt,
    };
    if (context == nullptr) {
        display.ai_print_generation_running_ = false;
        Application::GetInstance().Schedule([display_ptr = &display]() {
            if (display_ptr->IsAiPrintActive()) {
                display_ptr->ShowAiPrintErrorState("启动生图任务失败\n内存不足");
            }
        });
        return;
    }

    BaseType_t task_ret = pdFAIL;
#if CONFIG_SPIRAM
    task_ret = xTaskCreatePinnedToCoreWithCaps(
        [](void* arg) {
            std::unique_ptr<AiPrintTaskContext> holder(static_cast<AiPrintTaskContext*>(arg));
            if (holder != nullptr && holder->display != nullptr) {
                UiPrintPanel::RunTask(*holder->display, holder->prompt);
            }
            vTaskDelete(nullptr);
        },
        "ai_print",
        kAiPrintTaskStackSize,
        context,
        kAiPrintTaskPriority,
        nullptr,
        tskNO_AFFINITY,
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#else
    task_ret = xTaskCreatePinnedToCore(
        [](void* arg) {
            std::unique_ptr<AiPrintTaskContext> holder(static_cast<AiPrintTaskContext*>(arg));
            if (holder != nullptr && holder->display != nullptr) {
                UiPrintPanel::RunTask(*holder->display, holder->prompt);
            }
            vTaskDelete(nullptr);
        },
        "ai_print",
        kAiPrintTaskStackSize,
        context,
        kAiPrintTaskPriority,
        nullptr,
        tskNO_AFFINITY);
#endif

    if (task_ret != pdPASS) {
        delete context;
        display.ai_print_generation_running_ = false;
        Application::GetInstance().Schedule([display_ptr = &display]() {
            if (display_ptr->IsAiPrintActive()) {
                display_ptr->ShowAiPrintErrorState("启动生图任务失败\n任务创建失败");
            }
        });
    }
}

void UiPrintPanel::RunTask(UiLcdDisplay& display, const std::string& prompt) {
    std::string error_message;
    std::string prompt_id;
    std::string image_url;
    std::string image_bytes;

    for (int i = 0; i < 20; ++i) {
        if (Application::GetInstance().CanEnterSleepMode()) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    NetworkInterface* network = Board::GetInstance().GetNetwork();
    if (network == nullptr) {
        error_message = "网络未就绪，无法调用来秒生图接口";
    } else if (!RequestPromptId(network, prompt, prompt_id, error_message)) {
        ESP_LOGE(kAiPrintTag, "AI print generate request failed: %s", error_message.c_str());
    } else if (!PollImageUrl(network, prompt_id, image_url, error_message)) {
        ESP_LOGE(kAiPrintTag, "AI print poll image failed: %s", error_message.c_str());
    } else if (!DownloadImageBytes(network, image_url, image_bytes, error_message)) {
        ESP_LOGE(kAiPrintTag, "AI print download image failed: %s", error_message.c_str());
    }

    if (!error_message.empty()) {
        Application::GetInstance().Schedule([display_ptr = &display, error_message]() {
            display_ptr->ai_print_generation_running_ = false;
            if (display_ptr->IsAiPrintActive()) {
                display_ptr->ShowAiPrintErrorState(error_message);
            }
        });
        return;
    }

    auto image_data = std::make_shared<std::string>(std::move(image_bytes));
    Application::GetInstance().Schedule([display_ptr = &display, image_data]() {
        display_ptr->ai_print_generation_running_ = false;
        if (!display_ptr->IsAiPrintActive()) {
            return;
        }

        auto image = DecodeJpegToPreviewImage(*image_data);
        if (image == nullptr) {
            display_ptr->ShowAiPrintErrorState("图片生成成功\n但设备解码失败");
            return;
        }

        display_ptr->ShowAiPrintResultImage(std::move(image));
        if (Application::GetInstance().GetDeviceState() == kDeviceStateIdle) {
            Application::GetInstance().ToggleChatState();
        }
    });
}
