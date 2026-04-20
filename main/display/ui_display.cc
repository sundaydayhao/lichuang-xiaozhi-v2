#include "ui_display.h"

#include "application.h"
#include "ui_print_panel.h"
#include "assets.h"
#include "ble_hid.h"
#include "board.h"
#include "lvgl_theme.h"
#include "settings.h"
#include "system_info.h"
#include "assets/lang_config.h"
#include "widgets/list/lv_list.h"

#include <at_modem.h>
#include <esp_app_desc.h>
#include <esp_err.h>
#include <esp_heap_caps.h>
#include <esp_log.h>
#include <font_awesome.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <wifi_station.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <functional>
#include <initializer_list>
#include <memory>
#include <sstream>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

namespace {

const char* TAG = "CompactDisplay";
constexpr int kStatusBarHeight = 27;
constexpr int kMenuIconOriginalSize = 48;
constexpr int kMenuSwipeThreshold = 14;
constexpr int kMenuButtonHitPadding = 8;
constexpr int kVisionOptionId = 4;
constexpr int kEncyclopediaOptionId = 2;
constexpr int kOralPracticeOptionId = 3;
constexpr int kAiMassageOptionId = 5;
constexpr int kRemoteSelfieOptionId = 6;
constexpr int kAiPrintOptionId = 7;
constexpr int kWordDictationOptionId = 8;
constexpr int kFaceReadingOptionId = 9;
constexpr int kMagicMirrorOptionId = 10;
constexpr int kAiAlarmOptionId = 1;
constexpr int kSettingsOptionId = 12;
constexpr int kEncyclopediaPromptTimeoutSeconds = 10;
constexpr uint64_t kDialogueSceneTimerPeriodUs = 200 * 1000;
constexpr int kBackTargetMainMenu = 0;
constexpr int kBackTargetSettings = 1;
constexpr int kBackTargetCamera = 2;
constexpr int kBackTargetGallery = 3;
constexpr int kBackTargetWordDictation = 4;
constexpr char kRemoteSelfieDeviceName[] = "Xiaozhi-HID";
constexpr uint32_t kRemoteSelfiePanelColor = 0xF2F2F2;
constexpr uint32_t kRemoteSelfieInactiveColor = 0x979797;
constexpr uint32_t kRemoteSelfieActiveColor = 0x39A856;

lv_dir_t GetGestureDirection() {
    lv_indev_t* indev = lv_indev_active();
    return indev ? lv_indev_get_gesture_dir(indev) : LV_DIR_NONE;
}

std::string EscapeJsonString(const std::string& input) {
    std::string escaped;
    escaped.reserve(input.size() + 16);
    for (char ch : input) {
        switch (ch) {
        case '\\':
            escaped += "\\\\";
            break;
        case '"':
            escaped += "\\\"";
            break;
        case '\n':
            escaped += "\\n";
            break;
        case '\r':
            escaped += "\\r";
            break;
        case '\t':
            escaped += "\\t";
            break;
        default:
            escaped += ch;
            break;
        }
    }
    return escaped;
}

lv_obj_t* TakeValidObject(lv_obj_t*& object) {
    if (object != nullptr && lv_obj_is_valid(object)) {
        return object;
    }

    object = nullptr;
    return nullptr;
}

bool IsPersistentContentOverlay(lv_obj_t* object) {
    if (object == nullptr || !lv_obj_is_valid(object)) {
        return false;
    }

    const void* user_data = lv_obj_get_user_data(object);
    return user_data != nullptr && strcmp(static_cast<const char*>(user_data), "preview_overlay") == 0;
}

} // namespace

UiLcdDisplay::UiLcdDisplay(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_handle_t panel,
                           int width, int height, int offset_x, int offset_y,
                           bool mirror_x, bool mirror_y, bool swap_xy)
    : SpiLcdDisplay(panel_io, panel, width, height, offset_x, offset_y, mirror_x, mirror_y, swap_xy) {
    esp_timer_create_args_t camera_preview_timer_args = {
        .callback = [](void* arg) {
            auto* display = static_cast<UiLcdDisplay*>(arg);
            if (display != nullptr && display->camera_preview_callback_) {
                display->camera_preview_callback_();
            }
        },
        .arg = this,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "camera_preview_timer",
        .skip_unhandled_events = true,
    };
    ESP_ERROR_CHECK(esp_timer_create(&camera_preview_timer_args, &camera_preview_timer_));
    esp_timer_create_args_t dialogue_scene_timer_args = {
        .callback = [](void* arg) {
            auto* display = static_cast<UiLcdDisplay*>(arg);
            if (display != nullptr) {
                display->UpdateDialogueScenePrompt();
            }
        },
        .arg = this,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "dialogue_scene_timer",
        .skip_unhandled_events = true,
    };
    ESP_ERROR_CHECK(esp_timer_create(&dialogue_scene_timer_args, &dialogue_scene_timer_));
    InitializeAlarmSupport();
    InitializeWordDictationSupport();
    InitializeSleepMusicSupport();

    InitializeMainMenu();
}

UiLcdDisplay::~UiLcdDisplay() {
    StopCameraPreview();
    ExitDialogueScene(false);
    if (camera_preview_timer_ != nullptr) {
        esp_timer_delete(camera_preview_timer_);
        camera_preview_timer_ = nullptr;
    }
    if (dialogue_scene_timer_ != nullptr) {
        esp_timer_delete(dialogue_scene_timer_);
        dialogue_scene_timer_ = nullptr;
    }
    DeinitializeSleepMusicSupport();
    DeinitializeWordDictationSupport();
    DeinitializeAlarmSupport();
    BleHid::GetInstance().OnConnectionStateChanged({});
}

void UiLcdDisplay::SetEmotion(const char* emotion) {
    if (current_dialogue_scene_option_id_ == kMagicMirrorOptionId) {
        return;
    }

    SpiLcdDisplay::SetEmotion(emotion);
}

void UiLcdDisplay::SetChatMessage(const char* role, const char* content) {
    if (content != nullptr && content[0] != '\0') {
        if (current_dialogue_scene_option_id_ == kMagicMirrorOptionId) {
            SwitchMagicMirrorToChatMode();
        }
        if (current_dialogue_scene_option_id_ == kEncyclopediaOptionId &&
            role != nullptr && strcmp(role, "user") == 0) {
            MarkDialogueScenePromptCaptured();
        }
    }

    SpiLcdDisplay::SetChatMessage(role, content);
}

void UiLcdDisplay::SetPreviewImage(std::unique_ptr<LvglImage> image) {
    if (image != nullptr && current_dialogue_scene_option_id_ == kMagicMirrorOptionId) {
        SwitchMagicMirrorToChatMode();
    }

    SpiLcdDisplay::SetPreviewImage(std::move(image));
}

void UiLcdDisplay::SetCameraPreviewCallback(std::function<void()> callback) {
    camera_preview_callback_ = std::move(callback);
}

const char* UiLcdDisplay::GetMenuText(int option_id) const {
    switch (option_id) {
    case 1:
        return "AI闹钟";
    case 2:
        return "百科问答";
    case 3:
        return "口语陪练";
    case 4:
        return "拍识万物";
    case 5:
        return "AI按摩";
    case 6:
        return "遥控自拍";
    case 7:
        return "AI打印";
    case 8:
        return "单词听写";
    case 9:
        return "看相";
    case 10:
        return "魔镜美妆";
    case 11:
        return "助眠音乐";
    case 12:
        return "设置";
    default:
        return "";
    }
}

const char* UiLcdDisplay::GetMenuIconAssetName(int option_id) const {
    switch (option_id) {
    case 1:
        return "menu_ai_alarm.png";
    case 2:
        return "menu_encyclopedia.png";
    case 3:
        return "menu_oral_practice.png";
    case 4:
        return "menu_vision.png";
    case 5:
        return "menu_ai_massage.png";
    case 6:
        return "menu_remote_selfie.png";
    case 7:
        return "menu_ai_print.png";
    case 8:
        return "menu_word_dictation.png";
    case 9:
        return "menu_face_reading.png";
    case 10:
        return "menu_magic_mirror.png";
    case 11:
        return "menu_sleep_music.png";
    case 12:
        return "menu_settings.png";
    default:
        return nullptr;
    }
}

const char* UiLcdDisplay::GetFeatureBackgroundAssetName(int option_id) const {
    switch (option_id) {
    case 2:
        return "bg_encyclopedia.png";
    case 3:
        return "bg_oral_practice.png";
    case 5:
        return "bg_ai_massage.png";
    case 8:
        return "bg_word_dictation.png";
    case 9:
        return "bg_face_reading.png";
    default:
        return nullptr;
    }
}

bool UiLcdDisplay::IsDialogueSceneOption(int option_id) const {
    switch (option_id) {
    case kAiAlarmOptionId:
    case kEncyclopediaOptionId:
    case kOralPracticeOptionId:
    case kAiMassageOptionId:
    case kAiPrintOptionId:
    case kFaceReadingOptionId:
    case kMagicMirrorOptionId:
        return true;
    default:
        return false;
    }
}

std::string UiLcdDisplay::BuildRemoteSelfieScenePayload() const {
    const std::string language = GetSelectedLanguageCode();
    const std::string instructions =
        "You are a remote selfie assistant for controlling a phone camera over Bluetooth HID. "
        "Always reply in the current reply_language. "
        "Only use these remote selfie tools while the remote selfie screen is active. "
        "When the user says 语音使能 or asks to enable voice control, call self.remote_selfie.enable_voice_control. "
        "When the user asks to swipe up, call self.remote_selfie.swipe_up. "
        "When the user asks to swipe down, call self.remote_selfie.swipe_down. "
        "When the user asks to swipe left, call self.remote_selfie.swipe_left. "
        "When the user asks to swipe right, call self.remote_selfie.swipe_right. "
        "When the user asks to focus, tap the screen, or tap the center button, call self.remote_selfie.focus. "
        "When the user asks to take a photo, call self.remote_selfie.take_photo. "
        "When the user asks to start recording, call self.remote_selfie.start_recording. "
        "When the user asks to stop or end recording, call self.remote_selfie.stop_recording. "
        "If a tool fails, tell the user to connect Bluetooth device Xiaozhi-HID first. "
        "Keep replies short and action-oriented.";

    std::ostringstream oss;
    oss << "{\"jsonrpc\":\"2.0\",\"method\":\"notifications/scene_changed\",\"params\":{"
        << "\"scene\":\"remote_selfie\","
        << "\"language\":\"" << language << "\","
        << "\"reply_language\":\"" << language << "\","
        << "\"input_language\":\"" << language << "\","
        << "\"mode\":\"ble_camera_remote\","
        << "\"instructions\":\"" << EscapeJsonString(instructions) << "\"}}";
    return oss.str();
}

std::string UiLcdDisplay::BuildDialogueScenePayload(int option_id) const {
    const char* scene = nullptr;
    const char* mode = nullptr;
    std::string instructions;
    std::string language = GetSelectedLanguageCode();

    switch (option_id) {
    case kAiAlarmOptionId:
        scene = "ai_alarm";
        mode = "countdown_alarm";
        instructions =
            "You are an AI alarm assistant. "
            "Always reply in the current reply_language. "
            "When the user asks for a relative timer or reminder such as "
            "'15分钟后提醒我上课', call self.ai_alarm.set_timer. "
            "Convert the duration into delay_seconds. "
            "Put the short reminder content into reminder. "
            "When the user asks to cancel, stop, or clear the timer, call "
            "self.ai_alarm.cancel_timer. "
            "When the user asks whether a timer is active, call "
            "self.ai_alarm.get_status. "
            "Do not claim a timer is set unless the tool call succeeds. "
            "After a tool call, reply briefly and clearly.";
        break;
    case kEncyclopediaOptionId:
        scene = "encyclopedia";
        mode = "knowledge_guide";
        instructions =
            "You are a friendly encyclopedia guide for children. "
            "Always reply in the current reply_language. "
            "Answer general knowledge questions clearly, safely, and in a child-friendly way. "
            "Keep answers concise first, then expand only if the user asks.";
        break;
    case kOralPracticeOptionId:
        scene = "oral_practice";
        language = "en-US";
        mode = "spoken_english_practice";
        instructions =
            "You are an English conversation coach. "
            "Reply in natural English only. "
            "Keep the conversation focused on spoken English practice. "
            "Correct mistakes briefly and gently. "
            "If the user does not speak English, ask them to continue in English.";
        break;
    case kAiMassageOptionId:
        scene = "ai_massage";
        mode = "massage_assistant";
        instructions =
            "You are a gentle AI massage and relaxation assistant. "
            "Always reply in the current reply_language. "
            "Offer simple, safe, non-medical massage and relaxation guidance. "
            "Keep the tone calm and supportive, with short step-by-step suggestions. "
            "Focus on light pressure, stretching, and comfort reminders. "
            "Do not claim diagnosis or treatment, and tell the user to stop if anything hurts.";
        break;
    case kAiPrintOptionId:
        scene = "ai_print";
        mode = "photo_print_assistant";
        instructions =
            "You are an AI photo print assistant for children. "
            "Always reply in the current reply_language. "
            "First help the user choose a style such as 动漫, 卡通, 手绘, or 黑白线稿. "
            "When the user wants to use the camera, call self.camera.take_photo. "
            "After seeing the photo, describe how it would look in the chosen style. "
            "If real printing hardware is unavailable, clearly say that only style "
            "planning and preview advice are available.";
        break;
    case kFaceReadingOptionId:
        scene = "fortune";
        mode = "entertainment_face_reading";
        instructions =
            "You are an entertainment-only face reading assistant. "
            "Always reply in the current reply_language. "
            "If the user asks about face, palm, tongue, or appearance-based fun analysis, "
            "call self.camera.take_photo. "
            "Be playful and light. Clearly state that the result is for entertainment only "
            "and is not medical, legal, or factual fortune advice.";
        break;
    case kMagicMirrorOptionId:
        scene = "makeup";
        mode = "beauty_mirror";
        instructions =
            "You are a friendly makeup and grooming assistant. "
            "Always reply in the current reply_language. "
            "When the user wants appearance feedback, call self.camera.take_photo. "
            "Start with kind compliments, then give practical makeup, skincare, or styling "
            "suggestions suitable for the person in view. "
            "Keep the tone positive and child-safe.";
        break;
    default:
        return "";
    }

    std::ostringstream oss;
    oss << "{\"jsonrpc\":\"2.0\",\"method\":\"notifications/scene_changed\",\"params\":{"
        << "\"scene\":\"" << scene << "\","
        << "\"language\":\"" << language << "\","
        << "\"reply_language\":\"" << language << "\","
        << "\"input_language\":\"" << language << "\","
        << "\"mode\":\"" << mode << "\","
        << "\"instructions\":\"" << EscapeJsonString(instructions) << "\"}}";
    return oss.str();
}

const char* UiLcdDisplay::GetAboutTitle() const {
    return "关于本机";
}

const char* UiLcdDisplay::GetAboutInfoTitle(int index) const {
    static const char* kTexts[kAboutInfoItemCount] = {
        "固件版本",
        "绑定验证码",
        "服务器地址",
        "WiFi MAC",
        "4G IMEI",
        "IP地址",
        "UUID",
    };
    return (index >= 0 && index < static_cast<int>(kAboutInfoItemCount)) ? kTexts[index] : "";
}

std::string UiLcdDisplay::GetFirmwareVersionForDisplay() const {
    const esp_app_desc_t* app_desc = esp_app_get_description();
    if (app_desc == nullptr || app_desc->version[0] == '\0') {
        return "unknown";
    }
    return app_desc->version;
}

std::string UiLcdDisplay::GetActivationCodeForDisplay() const {
    return "暂无";
}

std::string UiLcdDisplay::GetServerAddressForDisplay() const {
    Settings wifi_settings("wifi", false);
    std::string url = wifi_settings.GetString("ota_url");
    if (!url.empty()) {
        return url;
    }

    Settings websocket_settings("websocket", false);
    url = websocket_settings.GetString("url");
    return url.empty() ? "未配置" : url;
}

std::string UiLcdDisplay::GetWifiMacAddressForDisplay() const {
    std::string mac = SystemInfo::GetMacAddress();
    return mac.empty() ? "unknown" : mac;
}

std::string UiLcdDisplay::Get4gImeiForDisplay() const {
    auto* modem = dynamic_cast<AtModem*>(Board::GetInstance().GetNetwork());
    if (modem == nullptr) {
        return "未配置";
    }

    std::string imei = modem->GetImei();
    return imei.empty() ? "未配置" : imei;
}

std::string UiLcdDisplay::GetIpAddressForDisplay() const {
    const std::string& ip = WifiStation::GetInstance().GetIpAddress();
    return ip.empty() ? "未配置" : ip;
}

std::string UiLcdDisplay::GetUuidForDisplay() const {
    std::string uuid = Board::GetInstance().GetUuid();
    return uuid.empty() ? "unknown" : uuid;
}


const lv_image_dsc_t* UiLcdDisplay::LoadAssetImage(const char* asset_name) {
    if (asset_name == nullptr || asset_name[0] == '\0') {
        return nullptr;
    }

    auto& image = asset_image_cache_[asset_name];
    if (image == nullptr) {
        void* ptr = nullptr;
        size_t size = 0;
        if (!Assets::GetInstance().GetAssetData(asset_name, ptr, size)) {
            ESP_LOGE(TAG, "Failed to load UI asset: %s", asset_name);
            asset_image_cache_.erase(asset_name);
            return nullptr;
        }
        image = CreateCachedDecodedImage(ptr, size);
        if (image == nullptr) {
            ESP_LOGE(TAG, "Failed to decode UI asset into PSRAM cache: %s", asset_name);
            asset_image_cache_.erase(asset_name);
            return nullptr;
        }
    }

    return image->image_dsc();
}

void UiLcdDisplay::RefreshMenuIconImages() {
    for (size_t i = 0; i < kMenuItemCount; ++i) {
        if (menu_button_icon_images_[i] == nullptr || !lv_obj_is_valid(menu_button_icon_images_[i])) {
            continue;
        }

        const lv_image_dsc_t* icon = LoadAssetImage(GetMenuIconAssetName(static_cast<int>(i) + 1));
        if (icon != nullptr) {
            lv_image_set_src(menu_button_icon_images_[i], icon);
        }
    }
}

void UiLcdDisplay::ApplyFeatureVisual(int option_id) {
    current_feature_option_id_ = option_id;

    DisplayLockGuard lock(this);
    if (preview_timer_ != nullptr) {
        esp_timer_stop(preview_timer_);
    }
    preview_image_cached_.reset();
    UpdateVisionAiBadgeVisibility();
    HideAiPrintMessageLabel();

    lv_obj_t* chat_label = TakeValidObject(chat_message_label_);
    if (chat_label != nullptr) {
        lv_obj_clear_flag(chat_label, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(chat_label, LV_OBJ_FLAG_IGNORE_LAYOUT);
    }

    if (feature_background_image_ == nullptr) {
        return;
    }

    const lv_image_dsc_t* background = LoadAssetImage(GetFeatureBackgroundAssetName(option_id));
    if (background == nullptr) {
        suppress_emotion_display_ = false;
        lv_obj_add_flag(feature_background_image_, LV_OBJ_FLAG_HIDDEN);
        EnsureEmotionWidgetsLocked();
        if (preview_image_ != nullptr) {
            lv_obj_clear_flag(preview_image_, LV_OBJ_FLAG_IGNORE_LAYOUT);
            lv_obj_add_flag(preview_image_, LV_OBJ_FLAG_HIDDEN);
        }
        if (chat_label != nullptr) {
            lv_label_set_text(chat_label, GetMenuText(option_id));
        }
        return;
    }

    if (chat_label != nullptr) {
        lv_label_set_text(chat_label, "");
    }

    suppress_emotion_display_ = true;
    lv_image_set_src(feature_background_image_, background);
    DestroyEmotionWidgetsLocked();
    if (preview_image_ != nullptr) {
        lv_obj_clear_flag(preview_image_, LV_OBJ_FLAG_IGNORE_LAYOUT);
        lv_obj_add_flag(preview_image_, LV_OBJ_FLAG_HIDDEN);
    }
    lv_obj_remove_flag(feature_background_image_, LV_OBJ_FLAG_HIDDEN);
}

void UiLcdDisplay::ClearFeatureVisual() {
    current_feature_option_id_ = 0;
    magic_mirror_chat_started_ = false;
    suppress_emotion_display_ = false;

    DisplayLockGuard lock(this);
    if (preview_timer_ != nullptr) {
        esp_timer_stop(preview_timer_);
    }
    preview_image_cached_.reset();
    UpdateVisionAiBadgeVisibility();
    HideAiPrintMessageLabel();

    lv_obj_t* chat_label = TakeValidObject(chat_message_label_);
    if (chat_label != nullptr) {
        lv_obj_clear_flag(chat_label, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(chat_label, LV_OBJ_FLAG_IGNORE_LAYOUT);
    }

    if (feature_background_image_ != nullptr) {
        lv_obj_add_flag(feature_background_image_, LV_OBJ_FLAG_HIDDEN);
    }
    if (preview_image_ != nullptr) {
        lv_obj_clear_flag(preview_image_, LV_OBJ_FLAG_IGNORE_LAYOUT);
        lv_obj_add_flag(preview_image_, LV_OBJ_FLAG_HIDDEN);
    }
    EnsureEmotionWidgetsLocked();
    if (chat_label != nullptr) {
        lv_label_set_text(chat_label, "");
    }
    if (feature_overlay_label_ != nullptr) {
        lv_label_set_text(feature_overlay_label_, "");
        lv_obj_add_flag(feature_overlay_label_, LV_OBJ_FLAG_HIDDEN);
    }
    if (magic_mirror_intro_container_ != nullptr) {
        lv_obj_add_flag(magic_mirror_intro_container_, LV_OBJ_FLAG_HIDDEN);
    }
}

void UiLcdDisplay::HideAiPrintMessageLabel() {
    lv_obj_t* label = TakeValidObject(ai_print_message_label_);
    if (label == nullptr) {
        return;
    }

    lv_label_set_text(label, "");
    lv_obj_add_flag(label, LV_OBJ_FLAG_HIDDEN);
}

void UiLcdDisplay::InitializeMagicMirrorIntro() {
    if (ai_screen_ == nullptr || magic_mirror_intro_container_ != nullptr) {
        return;
    }

    auto* lvgl_theme = static_cast<LvglTheme*>(GetTheme());

    magic_mirror_intro_container_ = lv_obj_create(ai_screen_);
    lv_obj_set_size(magic_mirror_intro_container_, LV_HOR_RES, LV_SIZE_CONTENT);
    lv_obj_align(magic_mirror_intro_container_, LV_ALIGN_CENTER, 0, 14);
    lv_obj_add_flag(magic_mirror_intro_container_, LV_OBJ_FLAG_FLOATING);
    lv_obj_add_flag(magic_mirror_intro_container_, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_clear_flag(magic_mirror_intro_container_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(magic_mirror_intro_container_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_scrollbar_mode(magic_mirror_intro_container_, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_bg_opa(magic_mirror_intro_container_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(magic_mirror_intro_container_, 0, 0);
    lv_obj_set_style_pad_all(magic_mirror_intro_container_, 0, 0);
    lv_obj_set_style_pad_row(magic_mirror_intro_container_, 12, 0);
    lv_obj_set_flex_flow(magic_mirror_intro_container_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(magic_mirror_intro_container_,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);

    magic_mirror_intro_emoji_ = lv_image_create(magic_mirror_intro_container_);
    if (auto emojis = lvgl_theme->emoji_collection(); emojis != nullptr) {
        if (const LvglImage* emoji = emojis->GetEmojiImage("loving"); emoji != nullptr) {
            lv_image_set_src(magic_mirror_intro_emoji_, emoji->image_dsc());
        }
    }

    magic_mirror_intro_text_ = lv_label_create(magic_mirror_intro_container_);
    lv_obj_set_width(magic_mirror_intro_text_, std::min(width_ - 48, 220));
    lv_label_set_long_mode(magic_mirror_intro_text_, LV_LABEL_LONG_WRAP);
    lv_label_set_text(magic_mirror_intro_text_,
                      "打开摄像头后可根据镜头内的人脸妆容提出建议、赞美");
    lv_obj_set_style_text_align(magic_mirror_intro_text_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(magic_mirror_intro_text_, lvgl_theme->text_font()->font(), 0);
    lv_obj_set_style_text_color(magic_mirror_intro_text_, lv_color_hex(0x2B2B2B), 0);
    lv_obj_set_style_bg_color(magic_mirror_intro_text_, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_bg_opa(magic_mirror_intro_text_, LV_OPA_90, 0);
    lv_obj_set_style_radius(magic_mirror_intro_text_, 16, 0);
    lv_obj_set_style_pad_hor(magic_mirror_intro_text_, 14, 0);
    lv_obj_set_style_pad_ver(magic_mirror_intro_text_, 10, 0);
    lv_obj_set_style_border_width(magic_mirror_intro_text_, 0, 0);
    lv_obj_set_style_shadow_width(magic_mirror_intro_text_, 14, 0);
    lv_obj_set_style_shadow_color(magic_mirror_intro_text_, lv_color_hex(0x000000), 0);
    lv_obj_set_style_shadow_opa(magic_mirror_intro_text_, LV_OPA_10, 0);

    lv_obj_add_flag(magic_mirror_intro_container_, LV_OBJ_FLAG_HIDDEN);
}

void UiLcdDisplay::ShowMagicMirrorIntro() {
    DisplayLockGuard lock(this);
    InitializeMagicMirrorIntro();
    if (magic_mirror_intro_container_ == nullptr) {
        return;
    }

    if (gif_controller_) {
        gif_controller_->Stop();
        gif_controller_.reset();
    }
    if (emoji_image_ != nullptr) {
        lv_obj_add_flag(emoji_image_, LV_OBJ_FLAG_HIDDEN);
    }
    if (emoji_label_ != nullptr) {
        lv_obj_add_flag(emoji_label_, LV_OBJ_FLAG_HIDDEN);
    }

    lv_obj_remove_flag(magic_mirror_intro_container_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(magic_mirror_intro_container_);
}

void UiLcdDisplay::HideMagicMirrorIntro() {
    DisplayLockGuard lock(this);
    if (magic_mirror_intro_container_ != nullptr) {
        lv_obj_add_flag(magic_mirror_intro_container_, LV_OBJ_FLAG_HIDDEN);
    }
}

void UiLcdDisplay::SwitchMagicMirrorToChatMode() {
    if (magic_mirror_chat_started_) {
        return;
    }

    magic_mirror_chat_started_ = true;
    HideMagicMirrorIntro();
}

void UiLcdDisplay::SetDialogueSceneOverlayText(const char* text) {
    if (feature_overlay_label_ == nullptr) {
        return;
    }

    DisplayLockGuard lock(this);
    if (text == nullptr || text[0] == '\0') {
        lv_label_set_text(feature_overlay_label_, "");
        lv_obj_add_flag(feature_overlay_label_, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    lv_label_set_text(feature_overlay_label_, text);
    lv_obj_clear_flag(feature_overlay_label_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(feature_overlay_label_);
}

void UiLcdDisplay::ClearAiSceneMessages() {
    DisplayLockGuard lock(this);
    if (content_ == nullptr) {
        chat_message_label_ = nullptr;
        return;
    }

    for (int32_t i = static_cast<int32_t>(lv_obj_get_child_cnt(content_)) - 1; i >= 0; --i) {
        lv_obj_t* child = lv_obj_get_child(content_, i);
        if (IsPersistentContentOverlay(child)) {
            continue;
        }
        lv_obj_del(child);
    }
    chat_message_label_ = nullptr;
}

void UiLcdDisplay::MarkDialogueScenePromptCaptured() {
    if (current_dialogue_scene_option_id_ != kEncyclopediaOptionId || dialogue_scene_prompt_captured_) {
        return;
    }

    dialogue_scene_prompt_captured_ = true;
    dialogue_scene_deadline_us_ = 0;
    SetDialogueSceneOverlayText("");
    if (dialogue_scene_timer_ != nullptr) {
        esp_timer_stop(dialogue_scene_timer_);
    }
}

void UiLcdDisplay::UpdateDialogueScenePrompt() {
    if (current_dialogue_scene_option_id_ != kEncyclopediaOptionId || dialogue_scene_prompt_captured_) {
        return;
    }

    const int64_t now_us = esp_timer_get_time();
    int seconds_remaining =
        static_cast<int>((dialogue_scene_deadline_us_ - now_us + 999999) / 1000000);
    if (seconds_remaining < 0) {
        seconds_remaining = 0;
    }

    char buffer[16];
    snprintf(buffer, sizeof(buffer), "%ds", seconds_remaining);
    SetDialogueSceneOverlayText(buffer);

    if (now_us < dialogue_scene_deadline_us_) {
        return;
    }

    if (dialogue_scene_timer_ != nullptr) {
        esp_timer_stop(dialogue_scene_timer_);
    }
    ExitDialogueScene(true);
    ReturnToMainMenu();
}

void UiLcdDisplay::ExitDialogueScene(bool exit_chat_session) {
    const bool was_active = current_dialogue_scene_option_id_ != 0;
    current_dialogue_scene_option_id_ = 0;
    magic_mirror_chat_started_ = false;
    dialogue_scene_deadline_us_ = 0;
    dialogue_scene_prompt_captured_ = false;

    if (dialogue_scene_timer_ != nullptr) {
        esp_timer_stop(dialogue_scene_timer_);
    }
    SetDialogueSceneOverlayText("");
    HideMagicMirrorIntro();

    if (!was_active) {
        return;
    }

    Application::GetInstance().SetSceneContextPayload("");
    if (exit_chat_session) {
        Application::GetInstance().ExitChatSession();
    }
}

void UiLcdDisplay::EnterDialogueScene(int option_id) {
    current_dialogue_scene_option_id_ = option_id;
    magic_mirror_chat_started_ = false;
    dialogue_scene_prompt_captured_ = false;
    dialogue_scene_deadline_us_ = 0;

    ClearAiSceneMessages();
    if (option_id == kAiAlarmOptionId) {
        ApplyFeatureVisual(option_id);
        ShowAlarmPanel();
    } else if (option_id == kAiPrintOptionId) {
        ApplyFeatureVisual(option_id);
        ShowAiPrintListeningState();
    } else {
        ApplyFeatureVisual(option_id);
    }

    const std::string payload = BuildDialogueScenePayload(option_id);
    Application::GetInstance().SetSceneContextPayload(payload);

    if (option_id == kEncyclopediaOptionId) {
        dialogue_scene_deadline_us_ =
            esp_timer_get_time() + static_cast<int64_t>(kEncyclopediaPromptTimeoutSeconds) * 1000 * 1000;
        SetDialogueSceneOverlayText("10s");
        if (dialogue_scene_timer_ != nullptr) {
            esp_timer_stop(dialogue_scene_timer_);
            ESP_ERROR_CHECK(esp_timer_start_periodic(dialogue_scene_timer_, kDialogueSceneTimerPeriodUs));
        }
    } else {
        SetDialogueSceneOverlayText("");
    }

    if (option_id == kMagicMirrorOptionId) {
        ShowMagicMirrorIntro();
    } else {
        HideMagicMirrorIntro();
    }

    Application::GetInstance().ToggleChatState();
}

void UiLcdDisplay::InitializeMainMenu() {
    DisplayLockGuard lock(this);

    auto* lvgl_theme = static_cast<LvglTheme*>(GetTheme());
    ai_screen_ = lv_screen_active();

    feature_background_image_ = lv_image_create(container_);
    lv_obj_set_size(feature_background_image_, LV_HOR_RES, LV_VER_RES);
    lv_obj_add_flag(feature_background_image_, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_add_flag(feature_background_image_, LV_OBJ_FLAG_FLOATING);
    lv_image_set_inner_align(feature_background_image_, LV_IMAGE_ALIGN_STRETCH);
    lv_obj_set_style_bg_opa(feature_background_image_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(feature_background_image_, 0, 0);
    lv_obj_set_pos(feature_background_image_, 0, 0);
    lv_obj_add_flag(feature_background_image_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_to_index(feature_background_image_, 0);

    feature_overlay_label_ = lv_label_create(ai_screen_);
    lv_obj_set_width(feature_overlay_label_, LV_SIZE_CONTENT);
    lv_obj_align(feature_overlay_label_, LV_ALIGN_BOTTOM_MID, 0, -26);
    lv_obj_add_flag(feature_overlay_label_, LV_OBJ_FLAG_FLOATING);
    lv_obj_add_flag(feature_overlay_label_, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_clear_flag(feature_overlay_label_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(feature_overlay_label_, lv_color_hex(0x111111), 0);
    lv_obj_set_style_bg_opa(feature_overlay_label_, LV_OPA_60, 0);
    lv_obj_set_style_radius(feature_overlay_label_, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_text_color(feature_overlay_label_, lv_color_white(), 0);
    lv_obj_set_style_pad_hor(feature_overlay_label_, 10, 0);
    lv_obj_set_style_pad_ver(feature_overlay_label_, 4, 0);
    lv_label_set_text(feature_overlay_label_, "");
    lv_obj_add_flag(feature_overlay_label_, LV_OBJ_FLAG_HIDDEN);

    vision_ai_badge_ = lv_obj_create(ai_screen_);
    lv_obj_set_size(vision_ai_badge_, 42, 42);
    lv_obj_align(vision_ai_badge_, LV_ALIGN_BOTTOM_LEFT, 12, -14);
    lv_obj_add_flag(vision_ai_badge_, LV_OBJ_FLAG_FLOATING);
    lv_obj_add_flag(vision_ai_badge_, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_clear_flag(vision_ai_badge_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(vision_ai_badge_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(vision_ai_badge_, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_radius(vision_ai_badge_, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(vision_ai_badge_, lv_color_hex(0x111111), 0);
    lv_obj_set_style_bg_opa(vision_ai_badge_, LV_OPA_70, 0);
    lv_obj_set_style_border_width(vision_ai_badge_, 1, 0);
    lv_obj_set_style_border_color(vision_ai_badge_, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_border_opa(vision_ai_badge_, LV_OPA_30, 0);
    lv_obj_set_style_pad_all(vision_ai_badge_, 0, 0);
    lv_obj_set_style_shadow_width(vision_ai_badge_, 12, 0);
    lv_obj_set_style_shadow_color(vision_ai_badge_, lv_color_hex(0x000000), 0);
    lv_obj_set_style_shadow_opa(vision_ai_badge_, LV_OPA_20, 0);
    lv_obj_add_flag(vision_ai_badge_, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t* vision_ai_badge_label = lv_label_create(vision_ai_badge_);
    lv_label_set_text(vision_ai_badge_label, "Ai");
    lv_obj_center(vision_ai_badge_label);
    lv_obj_set_style_text_font(vision_ai_badge_label, lvgl_theme->text_font()->font(), 0);
    lv_obj_set_style_text_color(vision_ai_badge_label, lv_color_white(), 0);
    lv_obj_set_style_text_letter_space(vision_ai_badge_label, 1, 0);

    lv_obj_add_event_cb(ai_screen_, OnAiScreenGesture, LV_EVENT_GESTURE, this);
    lv_obj_clear_flag(ai_screen_, LV_OBJ_FLAG_GESTURE_BUBBLE);
    InitializeMagicMirrorIntro();

    menu_screen_ = lv_obj_create(nullptr);
    lv_obj_set_style_bg_color(menu_screen_, lvgl_theme->background_color(), 0);
    lv_obj_set_style_border_width(menu_screen_, 0, 0);
    lv_obj_set_style_pad_all(menu_screen_, 0, 0);

    menu_page_container_ = lv_obj_create(menu_screen_);
    lv_obj_set_size(menu_page_container_, LV_HOR_RES, LV_VER_RES);
    lv_obj_set_style_bg_color(menu_page_container_, lvgl_theme->background_color(), 0);
    lv_obj_set_style_border_width(menu_page_container_, 0, 0);
    lv_obj_set_style_pad_all(menu_page_container_, 0, 0);
    lv_obj_set_flex_flow(menu_page_container_, LV_FLEX_FLOW_ROW);
    lv_obj_set_scroll_snap_x(menu_page_container_, LV_SCROLL_SNAP_CENTER);
    lv_obj_set_scroll_dir(menu_page_container_, LV_DIR_HOR);
    lv_obj_set_scrollbar_mode(menu_page_container_, LV_SCROLLBAR_MODE_OFF);

    menu_page1_ = lv_obj_create(menu_page_container_);
    lv_obj_set_size(menu_page1_, LV_HOR_RES, LV_VER_RES);
    lv_obj_set_style_bg_color(menu_page1_, lvgl_theme->background_color(), 0);
    lv_obj_set_style_border_width(menu_page1_, 0, 0);
    lv_obj_set_style_pad_all(menu_page1_, 0, 0);
    lv_obj_set_scrollbar_mode(menu_page1_, LV_SCROLLBAR_MODE_OFF);

    menu_page2_ = lv_obj_create(menu_page_container_);
    lv_obj_set_size(menu_page2_, LV_HOR_RES, LV_VER_RES);
    lv_obj_set_style_bg_color(menu_page2_, lvgl_theme->background_color(), 0);
    lv_obj_set_style_border_width(menu_page2_, 0, 0);
    lv_obj_set_style_pad_all(menu_page2_, 0, 0);
    lv_obj_set_scrollbar_mode(menu_page2_, LV_SCROLLBAR_MODE_OFF);

    const int btn_size_w = 72;
    const int btn_size_h = 72;
    const int btn_spacing_x = 4;
    const int btn_spacing_y = 28;
    const int start_y = 50;
    const int menu_columns = 3;
    const int total_menu_width =
        menu_columns * btn_size_w + (menu_columns - 1) * btn_spacing_x;
    const int start_x = std::max(0, (static_cast<int>(LV_HOR_RES) - total_menu_width) / 2);

    for (size_t i = 0; i < kMenuItemCount; ++i) {
        lv_obj_t* parent_page = (i < 6) ? menu_page1_ : menu_page2_;

        menu_buttons_[i] = lv_btn_create(parent_page);
        lv_obj_set_size(menu_buttons_[i], btn_size_w, btn_size_h);

        int page_index = (i < 6) ? static_cast<int>(i) : static_cast<int>(i - 6);
        int x = start_x + (page_index % 3) * (btn_size_w + btn_spacing_x);
        int y = start_y + (page_index / 3) * (btn_size_h + btn_spacing_y);
        lv_obj_set_pos(menu_buttons_[i], x, y);
        lv_obj_set_style_pad_all(menu_buttons_[i], 0, 0);
        lv_obj_set_style_pad_hor(menu_buttons_[i], 0, 0);
        lv_obj_set_style_pad_ver(menu_buttons_[i], 0, 0);
        lv_obj_set_style_border_width(menu_buttons_[i], 0, 0);
        lv_obj_set_style_bg_opa(menu_buttons_[i], LV_OPA_0, 0);
        lv_obj_set_style_bg_opa(menu_buttons_[i], LV_OPA_0, LV_STATE_PRESSED);
        lv_obj_set_style_bg_opa(menu_buttons_[i], LV_OPA_0, LV_STATE_FOCUSED);
        lv_obj_set_style_shadow_width(menu_buttons_[i], 0, 0);
        lv_obj_set_style_outline_width(menu_buttons_[i], 0, 0);
        lv_obj_set_ext_click_area(menu_buttons_[i], kMenuButtonHitPadding);
        lv_obj_add_flag(menu_buttons_[i], LV_OBJ_FLAG_GESTURE_BUBBLE);
        lv_obj_add_flag(menu_buttons_[i], LV_OBJ_FLAG_SCROLL_CHAIN_HOR);
        lv_obj_add_flag(menu_buttons_[i], LV_OBJ_FLAG_PRESS_LOCK);

        lv_obj_t* img = lv_image_create(menu_buttons_[i]);
        menu_button_icon_images_[i] = img;
        lv_obj_add_flag(img, LV_OBJ_FLAG_EVENT_BUBBLE);
        lv_obj_align(img, LV_ALIGN_TOP_MID, 0, 0);

        menu_button_labels_[i] = lv_label_create(menu_buttons_[i]);
        lv_obj_set_width(menu_button_labels_[i], btn_size_w);
        lv_obj_set_height(menu_button_labels_[i], btn_size_h - kMenuIconOriginalSize);
        lv_label_set_long_mode(menu_button_labels_[i], LV_LABEL_LONG_WRAP);
        lv_obj_align(menu_button_labels_[i], LV_ALIGN_TOP_MID, 0, kMenuIconOriginalSize);
        lv_label_set_text(menu_button_labels_[i], GetMenuText(static_cast<int>(i) + 1));
        lv_obj_set_style_text_align(menu_button_labels_[i], LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_font(menu_button_labels_[i], lvgl_theme->text_font()->font(), 0);
        lv_obj_set_style_text_color(menu_button_labels_[i], lv_color_black(), 0);
        lv_obj_add_flag(menu_button_labels_[i], LV_OBJ_FLAG_EVENT_BUBBLE);

        menu_button_data_[i] = MenuButtonData{this, static_cast<int>(i) + 1};
        lv_obj_set_user_data(menu_buttons_[i], &menu_button_data_[i]);
        lv_obj_add_event_cb(menu_buttons_[i], OnMenuButtonClicked, LV_EVENT_ALL, nullptr);
    }
    RefreshMenuIconImages();

    MoveStatusBarToScreen(menu_screen_);
    InitializeAlarmPanel();
    ai_back_button_data_ = BackButtonData{this, kBackTargetMainMenu};
    ai_back_button_ = CreateBackButton(ai_screen_, &ai_back_button_data_);
    UpdateVisionAiBadgeVisibility();
    lv_screen_load(menu_screen_);
}

void UiLcdDisplay::UpdateVisionAiBadgeVisibility() {
    if (vision_ai_badge_ == nullptr) {
        return;
    }

    if (current_feature_option_id_ == kVisionOptionId) {
        lv_obj_remove_flag(vision_ai_badge_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(vision_ai_badge_);
    } else {
        lv_obj_add_flag(vision_ai_badge_, LV_OBJ_FLAG_HIDDEN);
    }
}

void UiLcdDisplay::InitializeCameraScreen() {
    DisplayLockGuard lock(this);

    if (camera_screen_ != nullptr) {
        return;
    }

    auto* lvgl_theme = static_cast<LvglTheme*>(GetTheme());

    camera_screen_ = lv_obj_create(nullptr);
    lv_obj_set_style_bg_color(camera_screen_, lv_color_white(), 0);
    lv_obj_set_style_border_width(camera_screen_, 0, 0);
    lv_obj_set_style_pad_all(camera_screen_, 0, 0);
    lv_obj_set_scrollbar_mode(camera_screen_, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(camera_screen_, LV_OBJ_FLAG_SCROLLABLE);

    camera_card_ = lv_obj_create(camera_screen_);
    lv_obj_set_size(camera_card_, LV_HOR_RES - 4, LV_VER_RES - 4);
    lv_obj_align(camera_card_, LV_ALIGN_TOP_MID, 0, 2);
    lv_obj_set_style_bg_color(camera_card_, lv_color_hex(kRemoteSelfiePanelColor), 0);
    lv_obj_set_style_border_width(camera_card_, 0, 0);
    lv_obj_set_style_radius(camera_card_, 26, 0);
    lv_obj_set_style_pad_all(camera_card_, 0, 0);
    lv_obj_set_scrollbar_mode(camera_card_, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(camera_card_, LV_OBJ_FLAG_SCROLLABLE);

    auto create_icon_button = [&](lv_coord_t width, lv_coord_t height, lv_align_t align, lv_coord_t x, lv_coord_t y) {
        lv_obj_t* button = lv_btn_create(camera_card_);
        lv_obj_set_size(button, width, height);
        lv_obj_align(button, align, x, y);
        lv_obj_set_style_bg_opa(button, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(button, 0, 0);
        lv_obj_set_style_shadow_width(button, 0, 0);
        lv_obj_set_style_radius(button, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(button, lv_color_white(), LV_STATE_PRESSED);
        lv_obj_set_style_bg_opa(button, LV_OPA_40, LV_STATE_PRESSED);
        lv_obj_clear_flag(button, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_ext_click_area(button, 8);
        return button;
    };

    auto create_camera_shape = [](lv_obj_t* parent, lv_coord_t width, lv_coord_t height, lv_align_t align,
                                  lv_coord_t x, lv_coord_t y, lv_coord_t radius) {
        lv_obj_t* shape = lv_obj_create(parent);
        lv_obj_set_size(shape, width, height);
        lv_obj_align(shape, align, x, y);
        lv_obj_set_style_pad_all(shape, 0, 0);
        lv_obj_set_style_border_width(shape, 0, 0);
        lv_obj_set_style_shadow_width(shape, 0, 0);
        lv_obj_set_style_radius(shape, radius, 0);
        lv_obj_clear_flag(shape, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_remove_flag(shape, LV_OBJ_FLAG_CLICKABLE);
        return shape;
    };

    const RemoteSelfieAction direction_actions[kCameraDirectionButtonCount] = {
        RemoteSelfieAction::kSwipeUp,
        RemoteSelfieAction::kSwipeDown,
        RemoteSelfieAction::kSwipeLeft,
        RemoteSelfieAction::kSwipeRight,
    };
    const lv_align_t direction_aligns[kCameraDirectionButtonCount] = {
        LV_ALIGN_TOP_MID,
        LV_ALIGN_TOP_MID,
        LV_ALIGN_TOP_MID,
        LV_ALIGN_TOP_MID,
    };
    const lv_coord_t direction_x[kCameraDirectionButtonCount] = {0, 0, -44, 44};
    const lv_coord_t direction_y[kCameraDirectionButtonCount] = {26, 114, 70, 70};

    for (size_t i = 0; i < kCameraDirectionButtonCount; ++i) {
        camera_control_data_[i] = CameraControlData{this, direction_actions[i]};
        camera_direction_buttons_[i] = create_icon_button(52, 52, direction_aligns[i], direction_x[i], direction_y[i]);
        lv_obj_set_user_data(camera_direction_buttons_[i], &camera_control_data_[i]);
        lv_obj_add_event_cb(camera_direction_buttons_[i], OnCameraControlButtonClicked, LV_EVENT_CLICKED, nullptr);
        lv_obj_add_event_cb(camera_direction_buttons_[i], OnCameraDirectionButtonDraw, LV_EVENT_DRAW_MAIN, nullptr);
    }

    camera_control_data_[4] = CameraControlData{this, RemoteSelfieAction::kFocus};
    camera_focus_btn_ = create_icon_button(36, 36, LV_ALIGN_TOP_MID, 0, 78);
    lv_obj_set_style_bg_opa(camera_focus_btn_, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_opa(camera_focus_btn_, LV_OPA_80, LV_STATE_PRESSED);
    lv_obj_set_user_data(camera_focus_btn_, &camera_control_data_[4]);
    lv_obj_add_event_cb(camera_focus_btn_, OnCameraControlButtonClicked, LV_EVENT_CLICKED, nullptr);

    camera_shutter_btn_ = create_icon_button(72, 44, LV_ALIGN_TOP_MID, 0, 158);
    lv_obj_set_style_bg_opa(camera_shutter_btn_, LV_OPA_20, LV_STATE_PRESSED);
    lv_obj_set_user_data(camera_shutter_btn_, this);
    lv_obj_add_event_cb(camera_shutter_btn_, OnCameraActionButtonClicked, LV_EVENT_CLICKED, nullptr);
    camera_shutter_body_ = create_camera_shape(camera_shutter_btn_, 50, 30, LV_ALIGN_BOTTOM_MID, 0, -1, 10);
    camera_shutter_top_ = create_camera_shape(camera_shutter_btn_, 20, 8, LV_ALIGN_TOP_MID, -8, 5, 4);
    camera_shutter_lens_ring_ = create_camera_shape(camera_shutter_body_, 22, 22, LV_ALIGN_CENTER, 0, 0, LV_RADIUS_CIRCLE);
    lv_obj_set_style_bg_opa(camera_shutter_lens_ring_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(camera_shutter_lens_ring_, 3, 0);
    lv_obj_set_style_border_color(camera_shutter_lens_ring_, lv_color_white(), 0);
    camera_shutter_flash_dot_ = create_camera_shape(camera_shutter_body_, 7, 7, LV_ALIGN_RIGHT_MID, -8, -6, LV_RADIUS_CIRCLE);
    lv_obj_set_style_bg_color(camera_shutter_flash_dot_, lv_color_white(), 0);

    camera_ble_status_label_ = lv_label_create(camera_card_);
    lv_obj_set_width(camera_ble_status_label_, LV_HOR_RES - 28);
    lv_obj_align(camera_ble_status_label_, LV_ALIGN_TOP_MID, 0, 204);
    lv_obj_set_style_text_align(camera_ble_status_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(camera_ble_status_label_, lv_color_black(), 0);
    lv_obj_set_style_text_font(camera_ble_status_label_, lvgl_theme->text_font()->font(), 0);
    lv_label_set_text(camera_ble_status_label_, "");

    camera_back_button_data_ = BackButtonData{this, kBackTargetMainMenu};
    CreateBackButton(camera_screen_, &camera_back_button_data_);
    camera_ble_connected_ = false;
    lv_label_set_text(camera_ble_status_label_, "请连接蓝牙Xiaozhi-HID");
    UpdateRemoteSelfieControlStyles();
}

void UiLcdDisplay::InitializeGallery() {
    DisplayLockGuard lock(this);

    if (gallery_screen_ != nullptr) {
        return;
    }

    gallery_screen_ = lv_obj_create(nullptr);
    lv_obj_set_style_bg_color(gallery_screen_, lv_color_white(), 0);
    lv_obj_set_style_border_width(gallery_screen_, 0, 0);
    lv_obj_set_style_pad_all(gallery_screen_, 0, 0);
    gallery_list_ = lv_list_create(gallery_screen_);
    lv_obj_set_size(gallery_list_, LV_HOR_RES, LV_VER_RES - kStatusBarHeight);
    lv_obj_align(gallery_list_, LV_ALIGN_TOP_MID, 0, kStatusBarHeight);
    lv_obj_set_style_bg_color(gallery_list_, lv_color_white(), 0);
    lv_obj_set_style_border_width(gallery_list_, 0, 0);
    lv_obj_set_style_pad_all(gallery_list_, 0, 0);
    gallery_back_button_data_ = BackButtonData{this, kBackTargetCamera};
    CreateBackButton(gallery_screen_, &gallery_back_button_data_);
}

void UiLcdDisplay::InitializeAboutScreen() {
    DisplayLockGuard lock(this);

    if (about_screen_ != nullptr) {
        return;
    }

    auto* lvgl_theme = static_cast<LvglTheme*>(GetTheme());

    about_screen_ = lv_obj_create(nullptr);
    lv_obj_set_style_bg_color(about_screen_, lv_color_white(), 0);
    lv_obj_set_style_border_width(about_screen_, 0, 0);
    lv_obj_set_style_pad_all(about_screen_, 0, 0);
    lv_obj_set_scrollbar_mode(about_screen_, LV_SCROLLBAR_MODE_OFF);

    about_title_label_ = lv_label_create(about_screen_);
    lv_label_set_text(about_title_label_, GetAboutTitle());
    lv_obj_align(about_title_label_, LV_ALIGN_TOP_MID, 0, 40);
    lv_obj_set_style_text_font(about_title_label_, lvgl_theme->text_font()->font(), 0);
    lv_obj_set_style_text_color(about_title_label_, lv_color_black(), 0);

    about_content_ = lv_obj_create(about_screen_);
    lv_obj_set_size(about_content_, LV_HOR_RES, LV_VER_RES - 84);
    lv_obj_align(about_content_, LV_ALIGN_TOP_MID, 0, 84);
    lv_obj_set_style_bg_color(about_content_, lv_color_white(), 0);
    lv_obj_set_style_border_width(about_content_, 0, 0);
    lv_obj_set_style_pad_left(about_content_, 16, 0);
    lv_obj_set_style_pad_right(about_content_, 16, 0);
    lv_obj_set_style_pad_top(about_content_, 0, 0);
    lv_obj_set_style_pad_bottom(about_content_, 16, 0);
    lv_obj_set_scroll_dir(about_content_, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(about_content_, LV_SCROLLBAR_MODE_OFF);

    for (int i = 0; i < static_cast<int>(kAboutInfoItemCount); ++i) {
        about_info_title_labels_[i] = lv_label_create(about_content_);
        lv_label_set_text(about_info_title_labels_[i], GetAboutInfoTitle(i));
        lv_obj_set_width(about_info_title_labels_[i], LV_HOR_RES - 32);
        lv_obj_set_style_text_color(about_info_title_labels_[i], lv_color_hex(0x666666), 0);
        lv_obj_set_style_text_font(about_info_title_labels_[i], lvgl_theme->text_font()->font(), 0);

        about_info_value_labels_[i] = lv_label_create(about_content_);
        lv_obj_set_width(about_info_value_labels_[i], LV_HOR_RES - 32);
        lv_label_set_long_mode(about_info_value_labels_[i], LV_LABEL_LONG_WRAP);
        lv_obj_set_style_text_color(about_info_value_labels_[i], lv_color_black(), 0);
        lv_obj_set_style_text_font(about_info_value_labels_[i], lvgl_theme->text_font()->font(), 0);

        if (i < static_cast<int>(kAboutInfoItemCount) - 1) {
            about_info_separator_labels_[i] = lv_obj_create(about_content_);
            lv_obj_set_size(about_info_separator_labels_[i], LV_HOR_RES - 32, 1);
            lv_obj_set_style_bg_color(about_info_separator_labels_[i], lv_color_hex(0xD9D9D9), 0);
            lv_obj_set_style_bg_opa(about_info_separator_labels_[i], LV_OPA_COVER, 0);
            lv_obj_set_style_border_width(about_info_separator_labels_[i], 0, 0);
            lv_obj_set_style_radius(about_info_separator_labels_[i], 0, 0);
            lv_obj_clear_flag(about_info_separator_labels_[i], LV_OBJ_FLAG_CLICKABLE);
        }
    }

    about_back_button_data_ = BackButtonData{this, kBackTargetSettings};
    CreateBackButton(about_screen_, &about_back_button_data_);
}

bool UiLcdDisplay::ShouldUseTransparentStatusBar(lv_obj_t* target_screen) {
    if (target_screen != ai_screen_) {
        return false;
    }

    if (alarm_panel_ != nullptr && !lv_obj_is_valid(alarm_panel_)) {
        ResetAlarmPanelObjects();
        return true;
    }

    if (alarm_panel_ != nullptr && !lv_obj_has_flag(alarm_panel_, LV_OBJ_FLAG_HIDDEN)) {
        return false;
    }

    return true;
}

void UiLcdDisplay::UpdateStatusBarAppearance(lv_obj_t* target_screen) {
    if (status_bar_ == nullptr) {
        return;
    }

    auto* lvgl_theme = static_cast<LvglTheme*>(GetTheme());
    const bool transparent = ShouldUseTransparentStatusBar(target_screen);
    const lv_opa_t background_opa = transparent ? LV_OPA_TRANSP : LV_OPA_50;
    const lv_color_t background_color = lvgl_theme->background_color();
    const lv_color_t text_color = lvgl_theme->text_color();

    lv_obj_set_style_bg_color(status_bar_, background_color, 0);
    lv_obj_set_style_bg_opa(status_bar_, background_opa, 0);
    lv_obj_set_style_text_color(status_bar_, text_color, 0);

    if (network_label_ != nullptr) {
        lv_obj_set_style_text_color(network_label_, text_color, 0);
    }
    if (status_label_ != nullptr) {
        lv_obj_set_style_text_color(status_label_, text_color, 0);
    }
    if (notification_label_ != nullptr) {
        lv_obj_set_style_text_color(notification_label_, text_color, 0);
    }
    if (mute_label_ != nullptr) {
        lv_obj_set_style_text_color(mute_label_, text_color, 0);
    }
    if (battery_label_ != nullptr) {
        lv_obj_set_style_text_color(battery_label_, text_color, 0);
    }
}

void UiLcdDisplay::MoveStatusBarToScreen(lv_obj_t* target_screen) {
    if (status_bar_ == nullptr || target_screen == nullptr) {
        return;
    }

    if (target_screen == ai_screen_ && container_ != nullptr) {
        lv_obj_set_parent(status_bar_, container_);
        lv_obj_add_flag(status_bar_, LV_OBJ_FLAG_FLOATING);
        lv_obj_add_flag(status_bar_, LV_OBJ_FLAG_IGNORE_LAYOUT);
    } else {
        lv_obj_set_parent(status_bar_, target_screen);
        lv_obj_remove_flag(status_bar_, LV_OBJ_FLAG_FLOATING);
        lv_obj_remove_flag(status_bar_, LV_OBJ_FLAG_IGNORE_LAYOUT);
    }

    UpdateStatusBarAppearance(target_screen);
    lv_obj_clear_flag(status_bar_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_align(status_bar_, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_move_foreground(status_bar_);

    if (content_ != nullptr) {
        lv_obj_update_layout(status_bar_);
        const lv_coord_t reserved_top = (target_screen == ai_screen_) ? lv_obj_get_height(status_bar_) : 0;
        lv_obj_set_style_pad_top(content_, reserved_top, 0);
    }

    if (target_screen == ai_screen_ && feature_background_image_ != nullptr) {
        lv_obj_set_size(feature_background_image_, LV_HOR_RES, LV_VER_RES);
        lv_obj_set_pos(feature_background_image_, 0, 0);
        lv_obj_move_to_index(feature_background_image_, 0);
    }
}

lv_obj_t* UiLcdDisplay::CreateBackButton(lv_obj_t* parent, BackButtonData* data, bool dark_surface) {
    if (parent == nullptr || data == nullptr) {
        return nullptr;
    }

    auto* lvgl_theme = static_cast<LvglTheme*>(GetTheme());
    lv_obj_t* back_btn = lv_btn_create(parent);
    lv_obj_set_size(back_btn, 60, 40);
    lv_obj_align(back_btn, LV_ALIGN_TOP_LEFT, -18, 34);
    lv_obj_set_style_radius(back_btn, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(back_btn, 1, 0);
    lv_obj_set_style_border_color(back_btn, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_pad_all(back_btn, 0, 0);
    lv_obj_set_style_shadow_width(back_btn, dark_surface ? 0 : 16, 0);
    lv_obj_set_style_shadow_opa(back_btn, dark_surface ? LV_OPA_0 : LV_OPA_20, 0);
    lv_obj_set_style_shadow_color(back_btn, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_color(back_btn, lv_color_hex(0x1C2430), 0);
    lv_obj_set_style_bg_opa(back_btn, LV_OPA_70, 0);
    lv_obj_set_style_bg_color(back_btn, lv_color_hex(0x111822), LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(back_btn, LV_OPA_80, LV_STATE_PRESSED);
    lv_obj_add_flag(back_btn, LV_OBJ_FLAG_FLOATING);
    lv_obj_set_user_data(back_btn, data);
    lv_obj_add_event_cb(back_btn, OnBackButtonClicked, LV_EVENT_CLICKED, nullptr);

    lv_obj_t* label = lv_label_create(back_btn);
    lv_label_set_text(label, "<");
    lv_obj_align(label, LV_ALIGN_CENTER, 6, -1);
    lv_obj_set_style_text_font(label, lvgl_theme->text_font()->font(), 0);
    lv_obj_set_style_text_color(label, lv_color_white(), 0);
    lv_obj_set_style_text_letter_space(label, 0, 0);

    lv_obj_move_foreground(back_btn);
    return back_btn;
}

void UiLcdDisplay::StartCameraPreview(int interval_ms) {
    if (camera_preview_timer_ == nullptr || !camera_preview_callback_) {
        return;
    }

    esp_timer_stop(camera_preview_timer_);
    camera_preview_callback_();
    ESP_ERROR_CHECK(esp_timer_start_periodic(camera_preview_timer_, static_cast<uint64_t>(interval_ms) * 1000));
}

void UiLcdDisplay::StopCameraPreview() {
    if (camera_preview_timer_ != nullptr) {
        esp_timer_stop(camera_preview_timer_);
    }
}

void UiLcdDisplay::ShowAboutScreen() {
    if (about_screen_ == nullptr) {
        InitializeAboutScreen();
    }

    RefreshAboutScreenTexts();
    DisplayLockGuard lock(this);
    if (about_content_ != nullptr) {
        lv_obj_scroll_to_y(about_content_, 0, LV_ANIM_OFF);
    }
    lv_screen_load(about_screen_);
    MoveStatusBarToScreen(about_screen_);
}

void UiLcdDisplay::RefreshAboutScreenTexts() {
    if (about_title_label_ == nullptr || about_content_ == nullptr) {
        return;
    }

    DisplayLockGuard lock(this);
    lv_label_set_text(about_title_label_, GetAboutTitle());

    const std::array<std::string, kAboutInfoItemCount> values = {
        GetFirmwareVersionForDisplay(),
        GetActivationCodeForDisplay(),
        GetServerAddressForDisplay(),
        GetWifiMacAddressForDisplay(),
        Get4gImeiForDisplay(),
        GetIpAddressForDisplay(),
        GetUuidForDisplay(),
    };

    int y = 0;
    for (int i = 0; i < static_cast<int>(kAboutInfoItemCount); ++i) {
        if (about_info_title_labels_[i] == nullptr || about_info_value_labels_[i] == nullptr) {
            continue;
        }

        lv_label_set_text(about_info_title_labels_[i], GetAboutInfoTitle(i));
        lv_label_set_text(about_info_value_labels_[i], values[i].c_str());
        lv_obj_update_layout(about_content_);

        lv_obj_set_pos(about_info_title_labels_[i], 0, y);
        y += lv_obj_get_height(about_info_title_labels_[i]) + 4;

        lv_obj_set_pos(about_info_value_labels_[i], 0, y);
        y += lv_obj_get_height(about_info_value_labels_[i]) + 10;

        if (i < static_cast<int>(kAboutInfoItemCount) - 1 &&
            about_info_separator_labels_[i] != nullptr) {
            lv_obj_set_pos(about_info_separator_labels_[i], 0, y);
            y += 13;
        }
    }

    lv_obj_update_layout(about_content_);
}

void UiLcdDisplay::OnMenuOptionClicked(int option_id) {
    ESP_LOGI(TAG, "Menu option %d clicked", option_id);

    if (Application::GetInstance().GetDeviceState() == kDeviceStateFatalError) {
        ESP_LOGW(TAG, "Ignoring menu option %d while fatal error recovery is in progress", option_id);
        return;
    }

    if (option_id < 1 || option_id > static_cast<int>(kMenuItemCount)) {
        return;
    }

    SetPreviewPresentationMode(
        option_id == kVisionOptionId ? PreviewPresentationMode::kOverlay
                                     : PreviewPresentationMode::kChatBubble);

    if (current_dialogue_scene_option_id_ != 0) {
        ExitDialogueScene(true);
    }

    if (option_id == kRemoteSelfieOptionId) {
        if (camera_screen_ == nullptr) {
            InitializeCameraScreen();
        }
        remote_selfie_active_ = true;
        Application::GetInstance().SetSceneContextPayload(BuildRemoteSelfieScenePayload());
        auto& ble_hid = BleHid::GetInstance();
        ble_hid.SetScreenActive(true);
        bool ble_ready = ble_hid.Initialize();
        if (!ble_ready) {
            ESP_LOGW(TAG, "BLE HID init failed with audio active, retrying after suspending audio");
            SuspendAudioForRemoteSelfie();
            ble_ready = ble_hid.Initialize();
        }
        ble_hid.OnConnectionStateChanged([this](bool /*connected*/) {
            RequestCameraBleStatusRefresh();
        });
        if (ble_ready) {
            ResumeAudioAfterRemoteSelfie();
            UpdateCameraBleStatus(ble_hid.IsConnected());
        } else {
            ResumeAudioAfterRemoteSelfie();
            ShowNotification("蓝牙启动失败，请返回重试", 2000);
            DisplayLockGuard lock(this);
            lv_label_set_text(camera_ble_status_label_, "蓝牙启动失败，请返回重试");
        }

        HideAlarmPanel();
        ClearFeatureVisual();
        gallery_swipe_enabled_ = false;
        StopCameraPreview();
        DisplayLockGuard lock(this);
        lv_screen_load(camera_screen_);
        MoveStatusBarToScreen(camera_screen_);
        return;
    }

    if (option_id == kSettingsOptionId) {
        gallery_swipe_enabled_ = false;
        StopCameraPreview();
        HideAlarmPanel();
        ClearFeatureVisual();
        ShowSettingsScreen();
        return;
    }

    if (option_id == kWordDictationOptionId) {
        gallery_swipe_enabled_ = false;
        StopCameraPreview();
        HideAlarmPanel();
        ClearFeatureVisual();
        ShowWordDictationTextbookScreen();
        return;
    }

    if (option_id == 11) {
        gallery_swipe_enabled_ = false;
        StopCameraPreview();
        HideAlarmPanel();
        ClearFeatureVisual();
        ShowSleepMusicScreen();
        return;
    }

    if (lv_screen_active() == camera_screen_) {
        CleanupRemoteSelfie();
        ResumeAudioAfterRemoteSelfie();
    }

    if (IsDialogueSceneOption(option_id)) {
        gallery_swipe_enabled_ = false;
        StopCameraPreview();
        HideAlarmPanel();
        EnterDialogueScene(option_id);
        {
            DisplayLockGuard lock(this);
            lv_screen_load(ai_screen_);
            MoveStatusBarToScreen(ai_screen_);
        }
        return;
    }

    ApplyFeatureVisual(option_id);
    if (option_id == kAiAlarmOptionId) {
        ShowAlarmPanel();
    } else {
        HideAlarmPanel();
    }

    {
        DisplayLockGuard lock(this);
        lv_screen_load(ai_screen_);
        MoveStatusBarToScreen(ai_screen_);
    }

    gallery_swipe_enabled_ = (option_id == kVisionOptionId);
    if (gallery_swipe_enabled_) {
        StartCameraPreview(100);
    } else {
        StopCameraPreview();
    }

    if (option_id == kAiPrintOptionId) {
        ShowAiPrintListeningState();
    }
}

void UiLcdDisplay::ReturnToMainMenu() {
    ExitDialogueScene(true);
    SetPreviewPresentationMode(PreviewPresentationMode::kChatBubble);
    if (lv_screen_active() == camera_screen_) {
        CleanupRemoteSelfie();
        ResumeAudioAfterRemoteSelfie();
    }

    StopWordDictationPlayback();
    StopSleepMusicPlayback(false);
    word_dictation_view_ = WordDictationView::kHidden;
    gallery_swipe_enabled_ = false;
    StopCameraPreview();
    HideAlarmPanel();
    ClearFeatureVisual();
    {
        DisplayLockGuard lock(this);
        lv_screen_load(menu_screen_);
        MoveStatusBarToScreen(menu_screen_);
    }
}

void UiLcdDisplay::ReturnToCameraScreen() {
    gallery_swipe_enabled_ = true;
    SetPreviewPresentationMode(PreviewPresentationMode::kOverlay);
    HideAlarmPanel();
    ApplyFeatureVisual(kVisionOptionId);
    {
        DisplayLockGuard lock(this);
        lv_screen_load(ai_screen_);
        MoveStatusBarToScreen(ai_screen_);
    }
    StartCameraPreview(200);
}

bool UiLcdDisplay::IsRemoteSelfieActive() const {
    return remote_selfie_active_.load();
}

void UiLcdDisplay::UpdateRemoteSelfieControlStyles() {
    const lv_color_t control_color = lv_color_hex(
        camera_ble_connected_ ? kRemoteSelfieActiveColor : kRemoteSelfieInactiveColor);

    for (auto* button : camera_direction_buttons_) {
        if (button != nullptr) {
            lv_obj_invalidate(button);
        }
    }

    if (camera_focus_btn_ != nullptr) {
        lv_obj_set_style_bg_color(camera_focus_btn_, control_color, 0);
        lv_obj_set_style_bg_color(camera_focus_btn_, control_color, LV_STATE_PRESSED);
    }

    if (camera_shutter_body_ != nullptr) {
        lv_obj_set_style_bg_color(camera_shutter_body_, control_color, 0);
    }

    if (camera_shutter_top_ != nullptr) {
        lv_obj_set_style_bg_color(camera_shutter_top_, control_color, 0);
    }

    if (camera_shutter_lens_ring_ != nullptr) {
        lv_obj_set_style_border_color(camera_shutter_lens_ring_, lv_color_white(), 0);
    }

    if (camera_shutter_flash_dot_ != nullptr) {
        lv_obj_set_style_bg_color(camera_shutter_flash_dot_, lv_color_white(), 0);
    }
}

bool UiLcdDisplay::ExecuteRemoteSelfieAction(RemoteSelfieAction action) {
    auto& ble_hid = BleHid::GetInstance();
    bool success = false;

    switch (action) {
    case RemoteSelfieAction::kSwipeUp:
        success = ble_hid.SendSwipeUp();
        break;
    case RemoteSelfieAction::kSwipeDown:
        success = ble_hid.SendSwipeDown();
        break;
    case RemoteSelfieAction::kSwipeLeft:
        success = ble_hid.SendSwipeLeft();
        break;
    case RemoteSelfieAction::kSwipeRight:
        success = ble_hid.SendSwipeRight();
        break;
    case RemoteSelfieAction::kFocus:
        success = ble_hid.SendMouseClick();
        break;
    case RemoteSelfieAction::kCapture:
        success = ble_hid.SendVolumeDown();
        break;
    }

    if (!success) {
        ESP_LOGW(TAG, "BLE HID not connected, remote selfie action=%d failed", static_cast<int>(action));
    }

    return success;
}

void UiLcdDisplay::ExecuteCameraAction() {
    ExecuteRemoteSelfieAction(RemoteSelfieAction::kCapture);
}

void UiLcdDisplay::SuspendAudioForRemoteSelfie() {
    if (remote_selfie_audio_suspended_) {
        return;
    }

    Application::GetInstance().SuspendAudioForFeature();
    remote_selfie_audio_suspended_ = true;
}

void UiLcdDisplay::ResumeAudioAfterRemoteSelfie() {
    if (!remote_selfie_audio_suspended_) {
        return;
    }

    Application::GetInstance().ResumeAudioAfterFeature();
    remote_selfie_audio_suspended_ = false;
}

void UiLcdDisplay::CleanupRemoteSelfie() {
    remote_selfie_active_ = false;
    auto& app = Application::GetInstance();
    app.SetSceneContextPayload("");
    app.ExitChatSession();

    auto& ble_hid = BleHid::GetInstance();
    ble_hid.OnConnectionStateChanged({});
    ble_hid.SetScreenActive(false);
    ble_hid.Deinitialize();
    camera_ble_connected_ = false;
}

void UiLcdDisplay::RequestCameraBleStatusRefresh() {
    if (camera_ble_status_label_ == nullptr) {
        return;
    }

    if (!Lock(5)) {
        return;
    }

    lv_async_call(OnCameraBleStatusAsync, this);
    Unlock();
}

void UiLcdDisplay::UpdateCameraBleStatus(bool connected) {
    camera_ble_connected_ = connected;
    if (camera_ble_status_label_ == nullptr) {
        return;
    }

    char status_text[64];
    snprintf(status_text,
        sizeof(status_text),
        connected ? "已连接蓝牙%s" : "请连接蓝牙%s",
        kRemoteSelfieDeviceName);

    DisplayLockGuard lock(this);
    lv_label_set_text(camera_ble_status_label_, status_text);
    UpdateRemoteSelfieControlStyles();
}

void UiLcdDisplay::OnCameraBleStatusAsync(void* user_data) {
    auto* display = static_cast<UiLcdDisplay*>(user_data);
    if (display == nullptr) {
        return;
    }

    display->UpdateCameraBleStatus(BleHid::GetInstance().IsConnected());
}

void UiLcdDisplay::ShowGallery() {
    current_photo_image_.reset();
    current_photo_img_ = nullptr;
    StopCameraPreview();

    if (gallery_screen_ == nullptr) {
        InitializeGallery();
    }

    RefreshGalleryList();
    DisplayLockGuard lock(this);
    lv_screen_load(gallery_screen_);
    MoveStatusBarToScreen(gallery_screen_);
}

void UiLcdDisplay::RefreshGalleryList() {
    if (gallery_list_ == nullptr) {
        return;
    }

    DisplayLockGuard lock(this);

    lv_obj_clean(gallery_list_);
    photo_files_.clear();

    DIR* dir = opendir("/storage");
    if (dir == nullptr) {
        ESP_LOGE(TAG, "Failed to open /storage directory");
        lv_list_add_text(gallery_list_, "暂无照片");
        return;
    }

    dirent* entry = nullptr;
    while ((entry = readdir(dir)) != nullptr) {
        if (entry->d_type != DT_REG) {
            continue;
        }

        const char* name = entry->d_name;
        size_t len = strlen(name);
        if (len > 4 && strcmp(name + len - 4, ".raw") == 0) {
            photo_files_.emplace_back(name);
        }
    }
    closedir(dir);

    if (photo_files_.empty()) {
        lv_list_add_text(gallery_list_, "暂无照片");
        return;
    }

    std::sort(photo_files_.begin(), photo_files_.end(), std::greater<std::string>());

    for (const auto& photo_file : photo_files_) {
        std::string display_name = photo_file;
        size_t dot_pos = display_name.find_last_of('.');
        if (dot_pos != std::string::npos) {
            display_name = display_name.substr(0, dot_pos);
        }

        lv_obj_t* btn = lv_list_add_button(gallery_list_, LV_SYMBOL_RIGHT, display_name.c_str());
        lv_obj_set_style_text_color(btn, lv_color_black(), 0);

        char* filename_copy = static_cast<char*>(malloc(photo_file.size() + 1));
        if (filename_copy == nullptr) {
            continue;
        }
        strcpy(filename_copy, photo_file.c_str());
        lv_obj_set_user_data(btn, filename_copy);
        lv_obj_add_event_cb(btn, FreeUserDataOnDelete, LV_EVENT_DELETE, nullptr);
        lv_obj_add_event_cb(btn, OnGalleryItemClicked, LV_EVENT_CLICKED, this);
        lv_obj_add_event_cb(btn, OnGalleryItemLongPressed, LV_EVENT_LONG_PRESSED, this);
    }
}

void UiLcdDisplay::ShowPhotoViewer(const char* filename) {
    DisplayLockGuard lock(this);

    if (photo_viewer_screen_ == nullptr) {
        photo_viewer_screen_ = lv_obj_create(nullptr);
        lv_obj_set_style_bg_color(photo_viewer_screen_, lv_color_black(), 0);
        lv_obj_set_style_border_width(photo_viewer_screen_, 0, 0);
        lv_obj_set_style_pad_all(photo_viewer_screen_, 0, 0);
        photo_viewer_container_ = lv_obj_create(photo_viewer_screen_);
        lv_obj_set_size(photo_viewer_container_, LV_HOR_RES, LV_VER_RES);
        lv_obj_set_style_bg_color(photo_viewer_container_, lv_color_black(), 0);
        lv_obj_set_style_border_width(photo_viewer_container_, 0, 0);
        lv_obj_set_style_pad_all(photo_viewer_container_, 0, 0);
        photo_viewer_back_button_data_ = BackButtonData{this, kBackTargetGallery};
        CreateBackButton(photo_viewer_screen_, &photo_viewer_back_button_data_, true);
    }

    current_photo_image_.reset();
    current_photo_img_ = nullptr;

    lv_obj_clean(photo_viewer_container_);
    current_photo_img_ = lv_image_create(photo_viewer_container_);
    lv_obj_align(current_photo_img_, LV_ALIGN_CENTER, 0, 0);

    char filepath[128];
    snprintf(filepath, sizeof(filepath), "/storage/%s", filename);
    LoadRawImage(current_photo_img_, filepath);

    lv_screen_load(photo_viewer_screen_);
    MoveStatusBarToScreen(photo_viewer_screen_);
}

void UiLcdDisplay::ShowDeleteConfirmDialog(const char* filename) {
    DisplayLockGuard lock(this);

    lv_obj_t* mbox = lv_msgbox_create(nullptr);
    lv_msgbox_add_title(mbox, "Delete Photo");

    std::string display_name = filename;
    size_t dot_pos = display_name.find_last_of('.');
    if (dot_pos != std::string::npos) {
        display_name = display_name.substr(0, dot_pos);
    }
    std::string msg_text = "确定要删除照片\n\"" + display_name + "\" 吗？";
    lv_msgbox_add_text(mbox, msg_text.c_str());

    lv_obj_t* cancel_btn = lv_msgbox_add_footer_button(mbox, "Cancel");
    lv_obj_t* delete_btn = lv_msgbox_add_footer_button(mbox, "Delete");
    lv_obj_set_style_text_color(delete_btn, lv_color_make(255, 0, 0), 0);

    char* filename_copy = static_cast<char*>(malloc(strlen(filename) + 1));
    if (filename_copy != nullptr) {
        strcpy(filename_copy, filename);
        lv_obj_set_user_data(mbox, filename_copy);
        lv_obj_add_event_cb(mbox, FreeUserDataOnDelete, LV_EVENT_DELETE, nullptr);
    }

    lv_obj_add_event_cb(cancel_btn, OnDeleteDialogCancel, LV_EVENT_CLICKED, nullptr);
    lv_obj_add_event_cb(delete_btn, OnDeleteDialogConfirm, LV_EVENT_CLICKED, this);
}

void UiLcdDisplay::DeletePhoto(const char* filename) {
    char filepath[128];
    snprintf(filepath, sizeof(filepath), "/storage/%s", filename);
    if (unlink(filepath) == 0) {
        ESP_LOGI(TAG, "Photo deleted: %s", filepath);
        RefreshGalleryList();
    } else {
        ESP_LOGE(TAG, "Failed to delete photo: %s", filepath);
    }
}

void UiLcdDisplay::LoadRawImage(lv_obj_t* img_obj, const char* filepath) {
    FILE* f = fopen(filepath, "rb");
    if (f == nullptr) {
        ESP_LOGE(TAG, "Failed to open RAW file: %s", filepath);
        lv_obj_t* error_label = lv_label_create(lv_obj_get_parent(img_obj));
        lv_label_set_text(error_label, "无法加载图片");
        lv_obj_set_style_text_color(error_label, lv_color_white(), 0);
        lv_obj_center(error_label);
        return;
    }

    uint16_t width = 0;
    uint16_t height = 0;
    if (fread(&width, 2, 1, f) != 1 || fread(&height, 2, 1, f) != 1) {
        fclose(f);
        lv_obj_t* error_label = lv_label_create(lv_obj_get_parent(img_obj));
        lv_label_set_text(error_label, "读取文件头失败");
        lv_obj_set_style_text_color(error_label, lv_color_white(), 0);
        lv_obj_center(error_label);
        return;
    }

    uint32_t pixel_count = width * height;
    uint32_t data_size = pixel_count * 2;
    uint8_t* data = static_cast<uint8_t*>(heap_caps_malloc(data_size, MALLOC_CAP_SPIRAM));
    if (data == nullptr) {
        fclose(f);
        lv_obj_t* error_label = lv_label_create(lv_obj_get_parent(img_obj));
        lv_label_set_text(error_label, "内存不足");
        lv_obj_set_style_text_color(error_label, lv_color_white(), 0);
        lv_obj_center(error_label);
        return;
    }

    if (fread(data, 1, data_size, f) != data_size) {
        free(data);
        fclose(f);
        lv_obj_t* error_label = lv_label_create(lv_obj_get_parent(img_obj));
        lv_label_set_text(error_label, "读取像素数据失败");
        lv_obj_set_style_text_color(error_label, lv_color_white(), 0);
        lv_obj_center(error_label);
        return;
    }
    fclose(f);

    uint16_t* rgb565 = reinterpret_cast<uint16_t*>(data);
    for (uint32_t i = 0; i < pixel_count; ++i) {
        rgb565[i] = __builtin_bswap16(rgb565[i]);
    }

    current_photo_image_ = std::make_unique<LvglAllocatedImage>(data, data_size, width, height, width * 2, LV_COLOR_FORMAT_RGB565);
    lv_image_set_src(img_obj, current_photo_image_->image_dsc());

    lv_coord_t max_w = LV_HOR_RES;
    lv_coord_t max_h = LV_VER_RES - 50;
    lv_coord_t scale_w = (max_w * 256) / width;
    lv_coord_t scale_h = (max_h * 256) / height;
    lv_coord_t scale = (scale_w < scale_h) ? scale_w : scale_h;
    if (scale > 256) {
        scale = 256;
    }

    lv_image_set_scale(img_obj, scale);
    lv_obj_set_size(img_obj, (width * scale) / 256, (height * scale) / 256);
    lv_obj_center(img_obj);
}

void UiLcdDisplay::OnMenuButtonClicked(lv_event_t* e) {
    auto* target = static_cast<lv_obj_t*>(lv_event_get_current_target(e));
    if (target == nullptr) {
        return;
    }

    auto* data = static_cast<MenuButtonData*>(lv_obj_get_user_data(target));
    if (data == nullptr) {
        return;
    }

    lv_indev_t* indev = lv_event_get_indev(e);
    const lv_event_code_t code = lv_event_get_code(e);

    if (code == LV_EVENT_PRESSED) {
        data->moved = false;
        if (indev != nullptr) {
            lv_indev_get_point(indev, &data->press_point);
        }
        return;
    }

    if (code == LV_EVENT_PRESSING) {
        if (indev == nullptr || data->moved) {
            return;
        }

        lv_point_t current_point;
        lv_indev_get_point(indev, &current_point);
        const int delta_x = current_point.x - data->press_point.x;
        const int delta_y = current_point.y - data->press_point.y;
        const int abs_delta_x = delta_x >= 0 ? delta_x : -delta_x;
        const int abs_delta_y = delta_y >= 0 ? delta_y : -delta_y;
        if (abs_delta_x >= kMenuSwipeThreshold && abs_delta_x > (abs_delta_y + 4)) {
            data->moved = true;
        }
        return;
    }

    if (code == LV_EVENT_SHORT_CLICKED && !data->moved) {
        if (data->display != nullptr) {
            data->display->OnMenuOptionClicked(data->option_id);
        }
        return;
    }

    if (code == LV_EVENT_RELEASED) {
        data->moved = false;
    }
}

void UiLcdDisplay::OnAiScreenGesture(lv_event_t* e) {
    auto* display = static_cast<UiLcdDisplay*>(lv_event_get_user_data(e));
    if (display == nullptr) {
        return;
    }

    lv_dir_t dir = GetGestureDirection();
    if (dir == LV_DIR_LEFT && display->gallery_swipe_enabled_) {
        display->ShowGallery();
    }
}

void UiLcdDisplay::OnCameraScreenGesture(lv_event_t* e) {
    (void)e;
}

void UiLcdDisplay::OnGalleryScreenGesture(lv_event_t* e) {
    (void)e;
}

void UiLcdDisplay::OnPhotoViewerGesture(lv_event_t* e) {
    (void)e;
}

void UiLcdDisplay::OnSettingsScreenGesture(lv_event_t* e) {
    (void)e;
}

void UiLcdDisplay::OnAboutScreenGesture(lv_event_t* e) {
    (void)e;
}

void UiLcdDisplay::OnCameraControlButtonClicked(lv_event_t* e) {
    auto* target = static_cast<lv_obj_t*>(lv_event_get_current_target(e));
    auto* data = static_cast<CameraControlData*>(lv_obj_get_user_data(target));
    if (data != nullptr && data->display != nullptr) {
        data->display->ExecuteRemoteSelfieAction(data->action);
    }
}

void UiLcdDisplay::OnCameraDirectionButtonDraw(lv_event_t* e) {
    auto* target = static_cast<lv_obj_t*>(lv_event_get_current_target(e));
    auto* data = static_cast<CameraControlData*>(lv_obj_get_user_data(target));
    if (target == nullptr || data == nullptr || data->display == nullptr) {
        return;
    }

    lv_layer_t* layer = lv_event_get_layer(e);
    if (layer == nullptr) {
        return;
    }

    lv_area_t coords;
    lv_obj_get_coords(target, &coords);

    const lv_coord_t center_x = (coords.x1 + coords.x2) / 2;
    const lv_coord_t center_y = (coords.y1 + coords.y2) / 2;
    const lv_coord_t triangle_half_span = 19;
    const lv_coord_t triangle_tip_offset = 15;
    const lv_coord_t triangle_base_offset = 9;

    lv_draw_triangle_dsc_t triangle;
    lv_draw_triangle_dsc_init(&triangle);
    triangle.color = lv_color_hex(
        data->display->camera_ble_connected_ ? kRemoteSelfieActiveColor : kRemoteSelfieInactiveColor);
    triangle.opa = lv_obj_has_state(target, LV_STATE_PRESSED) ? LV_OPA_70 : LV_OPA_COVER;

    switch (data->action) {
    case RemoteSelfieAction::kSwipeUp:
        triangle.p[0].x = static_cast<lv_value_precise_t>(center_x - triangle_half_span);
        triangle.p[0].y = static_cast<lv_value_precise_t>(center_y + triangle_base_offset);
        triangle.p[1].x = static_cast<lv_value_precise_t>(center_x);
        triangle.p[1].y = static_cast<lv_value_precise_t>(center_y - triangle_tip_offset);
        triangle.p[2].x = static_cast<lv_value_precise_t>(center_x + triangle_half_span);
        triangle.p[2].y = static_cast<lv_value_precise_t>(center_y + triangle_base_offset);
        break;
    case RemoteSelfieAction::kSwipeDown:
        triangle.p[0].x = static_cast<lv_value_precise_t>(center_x - triangle_half_span);
        triangle.p[0].y = static_cast<lv_value_precise_t>(center_y - triangle_base_offset);
        triangle.p[1].x = static_cast<lv_value_precise_t>(center_x + triangle_half_span);
        triangle.p[1].y = static_cast<lv_value_precise_t>(center_y - triangle_base_offset);
        triangle.p[2].x = static_cast<lv_value_precise_t>(center_x);
        triangle.p[2].y = static_cast<lv_value_precise_t>(center_y + triangle_tip_offset);
        break;
    case RemoteSelfieAction::kSwipeLeft:
        triangle.p[0].x = static_cast<lv_value_precise_t>(center_x + triangle_base_offset);
        triangle.p[0].y = static_cast<lv_value_precise_t>(center_y - triangle_half_span);
        triangle.p[1].x = static_cast<lv_value_precise_t>(center_x - triangle_tip_offset);
        triangle.p[1].y = static_cast<lv_value_precise_t>(center_y);
        triangle.p[2].x = static_cast<lv_value_precise_t>(center_x + triangle_base_offset);
        triangle.p[2].y = static_cast<lv_value_precise_t>(center_y + triangle_half_span);
        break;
    case RemoteSelfieAction::kSwipeRight:
        triangle.p[0].x = static_cast<lv_value_precise_t>(center_x - triangle_base_offset);
        triangle.p[0].y = static_cast<lv_value_precise_t>(center_y - triangle_half_span);
        triangle.p[1].x = static_cast<lv_value_precise_t>(center_x + triangle_tip_offset);
        triangle.p[1].y = static_cast<lv_value_precise_t>(center_y);
        triangle.p[2].x = static_cast<lv_value_precise_t>(center_x - triangle_base_offset);
        triangle.p[2].y = static_cast<lv_value_precise_t>(center_y + triangle_half_span);
        break;
    default:
        return;
    }

    lv_draw_triangle(layer, &triangle);
}

void UiLcdDisplay::OnCameraActionButtonClicked(lv_event_t* e) {
    auto* target = static_cast<lv_obj_t*>(lv_event_get_current_target(e));
    auto* display = static_cast<UiLcdDisplay*>(lv_obj_get_user_data(target));
    if (display != nullptr) {
        display->ExecuteCameraAction();
    }
}

void UiLcdDisplay::OnBackButtonClicked(lv_event_t* e) {
    auto* target = static_cast<lv_obj_t*>(lv_event_get_current_target(e));
    auto* data = static_cast<BackButtonData*>(lv_obj_get_user_data(target));
    if (data != nullptr && data->display != nullptr) {
        if (data->target == kBackTargetSettings) {
            data->display->ShowSettingsScreen();
        } else if (data->target == kBackTargetCamera) {
            data->display->ReturnToCameraScreen();
        } else if (data->target == kBackTargetGallery) {
            data->display->ShowGallery();
        } else if (data->target == kBackTargetWordDictation) {
            data->display->HandleWordDictationBack();
        } else {
            data->display->ReturnToMainMenu();
        }
    }
}

void UiLcdDisplay::OnGalleryItemClicked(lv_event_t* e) {
    auto* display = static_cast<UiLcdDisplay*>(lv_event_get_user_data(e));
    auto* target = static_cast<lv_obj_t*>(lv_event_get_current_target(e));
    const char* filename = static_cast<const char*>(lv_obj_get_user_data(target));
    if (display != nullptr && filename != nullptr) {
        display->ShowPhotoViewer(filename);
    }
}

void UiLcdDisplay::OnGalleryItemLongPressed(lv_event_t* e) {
    auto* display = static_cast<UiLcdDisplay*>(lv_event_get_user_data(e));
    auto* target = static_cast<lv_obj_t*>(lv_event_get_current_target(e));
    const char* filename = static_cast<const char*>(lv_obj_get_user_data(target));
    if (display != nullptr && filename != nullptr) {
        display->ShowDeleteConfirmDialog(filename);
    }
}

void UiLcdDisplay::OnDeleteDialogCancel(lv_event_t* e) {
    auto* btn = static_cast<lv_obj_t*>(lv_event_get_current_target(e));
    lv_obj_t* footer = lv_obj_get_parent(btn);
    lv_obj_t* mbox = lv_obj_get_parent(footer);
    lv_msgbox_close(mbox);
}

void UiLcdDisplay::OnDeleteDialogConfirm(lv_event_t* e) {
    auto* display = static_cast<UiLcdDisplay*>(lv_event_get_user_data(e));
    auto* btn = static_cast<lv_obj_t*>(lv_event_get_current_target(e));
    lv_obj_t* footer = lv_obj_get_parent(btn);
    lv_obj_t* mbox = lv_obj_get_parent(footer);
    const char* filename = static_cast<const char*>(lv_obj_get_user_data(mbox));
    if (display != nullptr && filename != nullptr) {
        display->DeletePhoto(filename);
    }
    lv_msgbox_close(mbox);
}

void UiLcdDisplay::FreeUserDataOnDelete(lv_event_t* e) {
    auto* target = static_cast<lv_obj_t*>(lv_event_get_current_target(e));
    void* user_data = lv_obj_get_user_data(target);
    if (user_data != nullptr) {
        free(user_data);
        lv_obj_set_user_data(target, nullptr);
    }
}
