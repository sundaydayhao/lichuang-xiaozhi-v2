#include "ui_display.h"

#include "application.h"
#include "ui_menu_ai_print.h"
#include "assets.h"
#include "board.h"
#include "esp32_camera.h"
#include "lvgl_theme.h"
#include "widgets/list/lv_list.h"

#include <cJSON.h>
#include <esp_err.h>
#include <esp_heap_caps.h>
#include <esp_log.h>
#include <font_awesome.h>
#include <freertos/FreeRTOS.h>
#include <freertos/idf_additions.h>
#include <freertos/task.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

namespace {

const char* TAG = "CompactDisplay";

constexpr int kAiAlarmOptionId = 1;
constexpr int kEncyclopediaOptionId = 2;
constexpr int kOralPracticeOptionId = 3;
constexpr int kVisionOptionId = 4;
constexpr int kWordDictationOptionId = 7;
constexpr int kFaceReadingOptionId = 8;
constexpr int kMagicMirrorOptionId = 9;
constexpr int kSleepMusicOptionId = 10;
constexpr int kSettingsOptionId = 11;

constexpr int kStatusBarHeight = 27;
constexpr int kMenuIconOriginalSize = 48;
constexpr int kMenuSwipeThreshold = 14;
constexpr int kMenuButtonHitPadding = 8;
constexpr int kEncyclopediaPromptTimeoutSeconds = 10;
constexpr uint64_t kDialogueSceneTimerPeriodUs = 200 * 1000;
constexpr char kVisionDefaultQuestion[] = "拍摄到的东西是什么";
constexpr uint32_t kVisionTaskStackSize = 10 * 1024;
constexpr UBaseType_t kVisionTaskPriority = 3;
constexpr int kBackTargetMainMenu = 0;
constexpr int kBackTargetSettings = 1;
constexpr int kBackTargetCamera = 2;
constexpr int kBackTargetGallery = 3;
constexpr int kBackTargetWordDictation = 4;

lv_opa_t GetStatusBarBackgroundOpa(int width, int height, bool transparent) {
    if (transparent) {
        return LV_OPA_TRANSP;
    }

    return (width == 320 && height == 240) ? LV_OPA_COVER : LV_OPA_50;
}

struct VisionTaskContext {
    UiLcdDisplay* display = nullptr;
    std::string question;
};

lv_dir_t GetGestureDirection() {
    lv_indev_t* indev = lv_indev_active();
    return indev ? lv_indev_get_gesture_dir(indev) : LV_DIR_NONE;
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

std::string ParseVisionExplainMessage(const std::string& response) {
    const std::string trimmed = TrimCopy(response);
    if (trimmed.empty()) {
        return "没有识别结果，请再试一次";
    }

    cJSON* root = cJSON_Parse(trimmed.c_str());
    if (root == nullptr) {
        return trimmed;
    }

    std::string message;
    if (cJSON_IsString(root) && root->valuestring != nullptr) {
        message = root->valuestring;
    } else {
        cJSON* success = cJSON_GetObjectItem(root, "success");
        cJSON* result = cJSON_GetObjectItem(root, "result");
        cJSON* text = cJSON_GetObjectItem(root, "text");
        cJSON* answer = cJSON_GetObjectItem(root, "answer");
        cJSON* response_message = cJSON_GetObjectItem(root, "message");

        const bool call_failed = cJSON_IsBool(success) && !cJSON_IsTrue(success);
        cJSON* preferred = call_failed ? response_message : result;
        if (!cJSON_IsString(preferred)) {
            preferred = text;
        }
        if (!cJSON_IsString(preferred)) {
            preferred = answer;
        }
        if (!cJSON_IsString(preferred)) {
            preferred = response_message;
        }

        if (cJSON_IsString(preferred) && preferred->valuestring != nullptr) {
            message = preferred->valuestring;
        }
    }

    cJSON_Delete(root);
    return message.empty() ? trimmed : message;
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
    UiMenuAiAlarm::InitializeSupport(*this);
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
    UiMenuAiAlarm::DeinitializeSupport(*this);
    UiMenuRemoteSelfie::Deactivate(*this);
}

void UiLcdDisplay::SetEmotion(const char* emotion) {
    if (current_dialogue_scene_option_id_ == kMagicMirrorOptionId || IsVisionFeatureActive()) {
        return;
    }

    SpiLcdDisplay::SetEmotion(emotion);
}

void UiLcdDisplay::SetChatMessage(const char* role, const char* content) {
    if (IsVisionFeatureActive()) {
        ClearAiSceneMessages();
        return;
    }

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
    case kAiAlarmOptionId:
        return "AI闹钟";
    case kEncyclopediaOptionId:
        return "百科问答";
    case kOralPracticeOptionId:
        return "翻译官";
    case kVisionOptionId:
        return "拍识万物";
    case kUiMenuRemoteSelfieOptionId:
        return UiMenuRemoteSelfie::GetMenuText();
    case kUiMenuAiPrintOptionId:
        return UiMenuAiPrint::GetMenuText();
    case kWordDictationOptionId:
        return "单词听写";
    case kFaceReadingOptionId:
        return "看相";
    case kMagicMirrorOptionId:
        return "魔镜美妆";
    case kSleepMusicOptionId:
        return "助眠音乐";
    case kSettingsOptionId:
        return "设置";
    default:
        return "";
    }
}

const char* UiLcdDisplay::GetMenuIconAssetName(int option_id) const {
    switch (option_id) {
    case kAiAlarmOptionId:
        return "menu_ai_alarm.png";
    case kEncyclopediaOptionId:
        return "menu_encyclopedia.png";
    case kOralPracticeOptionId:
        return "menu_oral_practice.png";
    case kVisionOptionId:
        return "menu_vision.png";
    case kUiMenuRemoteSelfieOptionId:
        return UiMenuRemoteSelfie::GetMenuIconAssetName();
    case kUiMenuAiPrintOptionId:
        return UiMenuAiPrint::GetMenuIconAssetName();
    case kWordDictationOptionId:
        return "menu_word_dictation.png";
    case kFaceReadingOptionId:
        return "menu_face_reading.png";
    case kMagicMirrorOptionId:
        return "menu_magic_mirror.png";
    case kSleepMusicOptionId:
        return "menu_sleep_music.png";
    case kSettingsOptionId:
        return "menu_settings.png";
    default:
        return nullptr;
    }
}

const char* UiLcdDisplay::GetFeatureBackgroundAssetName(int option_id) const {
    switch (option_id) {
    case kEncyclopediaOptionId:
        return "bg_encyclopedia.png";
    case kOralPracticeOptionId:
        return "bg_oral_practice.png";
    case kWordDictationOptionId:
        return "bg_word_dictation.png";
    case kFaceReadingOptionId:
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
    case kUiMenuAiPrintOptionId:
    case kFaceReadingOptionId:
    case kMagicMirrorOptionId:
        return true;
    default:
        return false;
    }
}

bool UiLcdDisplay::IsVisionFeatureActive() const {
    return current_feature_option_id_ == kVisionOptionId;
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
    if (option_id == kVisionOptionId) {
        ClearAiSceneMessages();
    }
    HideVisionResultLabel();
    current_feature_option_id_ = option_id;

    DisplayLockGuard lock(this);
    if (preview_timer_ != nullptr) {
        esp_timer_stop(preview_timer_);
    }
    preview_image_cached_.reset();
    UpdateVisionAiBadgeVisibility();
    UiMenuAiPrint::HideMessageLabel(*this);

    lv_obj_t* chat_label = TakeValidObject(chat_message_label_);
    if (chat_label != nullptr) {
        lv_obj_clear_flag(chat_label, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(chat_label, LV_OBJ_FLAG_IGNORE_LAYOUT);
    }

    if (feature_background_image_ == nullptr) {
        return;
    }

    if (option_id == kVisionOptionId) {
        suppress_emotion_display_ = true;
        lv_obj_add_flag(feature_background_image_, LV_OBJ_FLAG_HIDDEN);
        DestroyEmotionWidgetsLocked();
        if (preview_image_ != nullptr) {
            lv_obj_add_flag(preview_image_, LV_OBJ_FLAG_HIDDEN);
        }
        RefreshAiContentLayout(0);
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
    HideVisionResultLabel();
    current_feature_option_id_ = 0;
    magic_mirror_chat_started_ = false;
    suppress_emotion_display_ = false;

    DisplayLockGuard lock(this);
    if (preview_timer_ != nullptr) {
        esp_timer_stop(preview_timer_);
    }
    preview_image_cached_.reset();
    UpdateVisionAiBadgeVisibility();
    UiMenuAiPrint::HideMessageLabel(*this);

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
    RefreshAiContentLayout(lv_obj_get_style_pad_top(content_, 0));
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

void UiLcdDisplay::ShowVisionResultLabel(const char* text) {
    DisplayLockGuard lock(this);
    if (ai_screen_ == nullptr) {
        return;
    }

    if (vision_result_label_ != nullptr && !lv_obj_is_valid(vision_result_label_)) {
        vision_result_label_ = nullptr;
    }

    if (vision_result_label_ == nullptr) {
        auto* lvgl_theme = static_cast<LvglTheme*>(GetTheme());
        vision_result_label_ = lv_label_create(ai_screen_);
        lv_obj_set_width(vision_result_label_, std::max(140, width_ - 72));
        lv_label_set_long_mode(vision_result_label_, LV_LABEL_LONG_WRAP);
        lv_obj_set_style_text_align(vision_result_label_, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_font(vision_result_label_, lvgl_theme->text_font()->font(), 0);
        lv_obj_set_style_text_color(vision_result_label_, lv_color_white(), 0);
        lv_obj_set_style_bg_color(vision_result_label_, lv_color_hex(0x111111), 0);
        lv_obj_set_style_bg_opa(vision_result_label_, LV_OPA_70, 0);
        lv_obj_set_style_border_width(vision_result_label_, 1, 0);
        lv_obj_set_style_border_color(vision_result_label_, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_border_opa(vision_result_label_, LV_OPA_20, 0);
        lv_obj_set_style_radius(vision_result_label_, 18, 0);
        lv_obj_set_style_pad_hor(vision_result_label_, 14, 0);
        lv_obj_set_style_pad_ver(vision_result_label_, 10, 0);
        lv_obj_set_style_shadow_width(vision_result_label_, 18, 0);
        lv_obj_set_style_shadow_color(vision_result_label_, lv_color_hex(0x000000), 0);
        lv_obj_set_style_shadow_opa(vision_result_label_, LV_OPA_10, 0);
        lv_obj_add_flag(vision_result_label_, LV_OBJ_FLAG_FLOATING);
        lv_obj_add_flag(vision_result_label_, LV_OBJ_FLAG_IGNORE_LAYOUT);
        lv_obj_clear_flag(vision_result_label_, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_flag(vision_result_label_, LV_OBJ_FLAG_HIDDEN);
    }

    if (text == nullptr || text[0] == '\0') {
        lv_label_set_text(vision_result_label_, "");
        lv_obj_add_flag(vision_result_label_, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    lv_label_set_text(vision_result_label_, text);
    lv_obj_align(vision_result_label_, LV_ALIGN_BOTTOM_MID, 0, -20);
    lv_obj_clear_flag(vision_result_label_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(vision_result_label_);
}

void UiLcdDisplay::HideVisionResultLabel() {
    ShowVisionResultLabel("");
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

void UiLcdDisplay::StartVisionRecognition(const std::string& question) {
    if (!IsVisionFeatureActive()) {
        return;
    }

    bool expected = false;
    if (!vision_recognition_running_.compare_exchange_strong(expected, true)) {
        ShowNotification("正在识别中，请稍候", 1200);
        return;
    }

    StopCameraPreview();
    ShowVisionResultLabel("识别中...");

    auto* context = new (std::nothrow) VisionTaskContext{
        .display = this,
        .question = question.empty() ? std::string(kVisionDefaultQuestion) : question,
    };
    if (context == nullptr) {
        vision_recognition_running_ = false;
        if (IsVisionFeatureActive()) {
            StartCameraPreview(100);
        }
        ShowVisionResultLabel("启动识别失败，请重试");
        return;
    }

    BaseType_t task_ret = pdFAIL;
#if CONFIG_SPIRAM
    task_ret = xTaskCreatePinnedToCoreWithCaps(
        [](void* arg) {
            std::unique_ptr<VisionTaskContext> holder(static_cast<VisionTaskContext*>(arg));
            if (holder != nullptr && holder->display != nullptr) {
                holder->display->RunVisionRecognitionTask(holder->question);
            }
            vTaskDelete(nullptr);
        },
        "vision_explain",
        kVisionTaskStackSize,
        context,
        kVisionTaskPriority,
        nullptr,
        tskNO_AFFINITY,
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#else
    task_ret = xTaskCreatePinnedToCore(
        [](void* arg) {
            std::unique_ptr<VisionTaskContext> holder(static_cast<VisionTaskContext*>(arg));
            if (holder != nullptr && holder->display != nullptr) {
                holder->display->RunVisionRecognitionTask(holder->question);
            }
            vTaskDelete(nullptr);
        },
        "vision_explain",
        kVisionTaskStackSize,
        context,
        kVisionTaskPriority,
        nullptr,
        tskNO_AFFINITY);
#endif

    if (task_ret != pdPASS) {
        delete context;
        vision_recognition_running_ = false;
        if (IsVisionFeatureActive()) {
            StartCameraPreview(100);
        }
        ShowVisionResultLabel("启动识别失败，请重试");
    }
}

void UiLcdDisplay::RunVisionRecognitionTask(const std::string& question) {
    std::string result_message;
    auto* camera = Board::GetInstance().GetCamera();

    if (camera == nullptr) {
        result_message = "相机未就绪，请稍后再试";
    } else {
        try {
            auto* esp32_camera = dynamic_cast<Esp32Camera*>(camera);
            std::unique_ptr<Esp32Camera::CaptureLock> capture_lock;
            if (esp32_camera != nullptr) {
                capture_lock = esp32_camera->CaptureForExplain();
                if (!capture_lock || !capture_lock->IsLocked()) {
                    throw std::runtime_error("拍照失败，请重试");
                }
            } else if (!camera->Capture()) {
                throw std::runtime_error("拍照失败，请重试");
            }

            result_message = ParseVisionExplainMessage(camera->Explain(question));
        } catch (const std::exception& e) {
            result_message = e.what();
        }
    }

    if (result_message.empty()) {
        result_message = "没有识别结果，请再试一次";
    }

    Application::GetInstance().Schedule([display_ptr = this, result_message = std::move(result_message)]() {
        display_ptr->vision_recognition_running_ = false;
        if (!display_ptr->IsVisionFeatureActive()) {
            return;
        }

        display_ptr->ShowVisionResultLabel(result_message.c_str());
        display_ptr->StartCameraPreview(100);
    });
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
        UiMenuAiAlarm::ShowPanel(*this);
    } else if (UiMenuAiPrint::IsOptionId(option_id)) {
        ApplyFeatureVisual(option_id);
        UiMenuAiPrint::ShowListeningState(*this);
    } else {
        ApplyFeatureVisual(option_id);
    }

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

    vision_ai_badge_ = lv_btn_create(ai_screen_);
    lv_obj_set_size(vision_ai_badge_, 42, 42);
    lv_obj_align(vision_ai_badge_, LV_ALIGN_BOTTOM_LEFT, 12, -14);
    lv_obj_add_flag(vision_ai_badge_, LV_OBJ_FLAG_FLOATING);
    lv_obj_add_flag(vision_ai_badge_, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_clear_flag(vision_ai_badge_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(vision_ai_badge_, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_radius(vision_ai_badge_, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(vision_ai_badge_, lv_color_hex(0x111111), 0);
    lv_obj_set_style_bg_opa(vision_ai_badge_, LV_OPA_70, 0);
    lv_obj_set_style_bg_color(vision_ai_badge_, lv_color_hex(0x0B0B0B), LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(vision_ai_badge_, LV_OPA_80, LV_STATE_PRESSED);
    lv_obj_set_style_border_width(vision_ai_badge_, 1, 0);
    lv_obj_set_style_border_color(vision_ai_badge_, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_border_opa(vision_ai_badge_, LV_OPA_30, 0);
    lv_obj_set_style_pad_all(vision_ai_badge_, 0, 0);
    lv_obj_set_style_shadow_width(vision_ai_badge_, 12, 0);
    lv_obj_set_style_shadow_color(vision_ai_badge_, lv_color_hex(0x000000), 0);
    lv_obj_set_style_shadow_opa(vision_ai_badge_, LV_OPA_20, 0);
    lv_obj_set_ext_click_area(vision_ai_badge_, 8);
    lv_obj_set_user_data(vision_ai_badge_, this);
    lv_obj_add_event_cb(vision_ai_badge_, OnVisionAiButtonClicked, LV_EVENT_CLICKED, nullptr);
    lv_obj_add_flag(vision_ai_badge_, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t* vision_ai_badge_label = lv_label_create(vision_ai_badge_);
    lv_label_set_text(vision_ai_badge_label, "AI");
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
    UiMenuAiAlarm::InitializePanel(*this);
    ai_back_button_data_ = UiBackButtonData{this, kBackTargetMainMenu};
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
    gallery_back_button_data_ = UiBackButtonData{this, kBackTargetCamera};
    CreateBackButton(gallery_screen_, &gallery_back_button_data_);
}

bool UiLcdDisplay::ShouldUseTransparentStatusBar(lv_obj_t* target_screen) {
    if (target_screen != ai_screen_) {
        return false;
    }

    return !UiMenuAiAlarm::IsPanelVisible(*this);
}

void UiLcdDisplay::UpdateStatusBarAppearance(lv_obj_t* target_screen) {
    if (status_bar_ == nullptr) {
        return;
    }

    auto* lvgl_theme = static_cast<LvglTheme*>(GetTheme());
    const bool transparent = ShouldUseTransparentStatusBar(target_screen);
    const lv_opa_t background_opa = GetStatusBarBackgroundOpa(width_, height_, transparent);
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
        const lv_coord_t reserved_top =
            (target_screen == ai_screen_ && !IsVisionFeatureActive()) ? lv_obj_get_height(status_bar_) : 0;
        RefreshAiContentLayout(reserved_top);
    }

    if (target_screen == ai_screen_ && feature_background_image_ != nullptr) {
        lv_obj_set_size(feature_background_image_, LV_HOR_RES, LV_VER_RES);
        lv_obj_set_pos(feature_background_image_, 0, 0);
        lv_obj_move_to_index(feature_background_image_, 0);
    }
}

void UiLcdDisplay::RefreshAiContentLayout(lv_coord_t top_padding) {
    if (content_ == nullptr) {
        return;
    }

    auto* lvgl_theme = static_cast<LvglTheme*>(GetTheme());
    const bool immersive = IsVisionFeatureActive();
    const lv_coord_t side_padding = immersive ? 0 : lvgl_theme->spacing(4);
    const lv_coord_t bottom_padding = immersive ? 0 : lvgl_theme->spacing(4);
    const lv_coord_t row_padding = immersive ? 0 : lvgl_theme->spacing(4);

    lv_obj_set_style_pad_top(content_, top_padding, 0);
    lv_obj_set_style_pad_bottom(content_, bottom_padding, 0);
    lv_obj_set_style_pad_left(content_, side_padding, 0);
    lv_obj_set_style_pad_right(content_, side_padding, 0);
    lv_obj_set_style_pad_row(content_, row_padding, 0);
}

lv_obj_t* UiLcdDisplay::CreateBackButton(lv_obj_t* parent, UiBackButtonData* data, bool dark_surface) {
    if (parent == nullptr || data == nullptr) {
        return nullptr;
    }

    auto* lvgl_theme = static_cast<LvglTheme*>(GetTheme());
    lv_obj_t* back_btn = lv_btn_create(parent);
    lv_obj_set_size(back_btn, 60, 30);
    lv_obj_align(back_btn, LV_ALIGN_TOP_LEFT, 0, kStatusBarHeight + 3);
    lv_obj_set_style_radius(back_btn, 0, 0);
    lv_obj_set_style_border_width(back_btn, 0, 0);
    lv_obj_set_style_pad_all(back_btn, 0, 0);
    lv_obj_set_style_shadow_width(back_btn, 0, 0);
    lv_obj_set_style_shadow_opa(back_btn, LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_opa(back_btn, LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_opa(back_btn, LV_OPA_TRANSP, LV_STATE_PRESSED);
    lv_obj_add_flag(back_btn, LV_OBJ_FLAG_FLOATING);
    lv_obj_clear_flag(back_btn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_ext_click_area(back_btn, 8);
    lv_obj_set_user_data(back_btn, data);
    lv_obj_add_event_cb(back_btn, OnBackButtonClicked, LV_EVENT_CLICKED, nullptr);

    lv_obj_t* label = lv_label_create(back_btn);
    lv_label_set_text(label, LV_SYMBOL_LEFT);
    lv_obj_align(label, LV_ALIGN_CENTER, -10, 0);
    lv_obj_set_style_text_font(label, lvgl_theme->icon_font()->font(), 0);
    lv_obj_set_style_text_color(label, dark_surface ? lv_color_white() : lv_color_black(), 0);
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

    if (option_id == kUiMenuRemoteSelfieOptionId) {
        UiMenuRemoteSelfie::ShowScreen(*this);
        return;
    }

    if (option_id == kSettingsOptionId) {
        gallery_swipe_enabled_ = false;
        StopCameraPreview();
        UiMenuAiAlarm::HidePanel(*this);
        ClearFeatureVisual();
        UiMenuSetting::ShowSettingsScreen(*this);
        return;
    }

    if (option_id == kWordDictationOptionId) {
        gallery_swipe_enabled_ = false;
        StopCameraPreview();
        UiMenuAiAlarm::HidePanel(*this);
        ClearFeatureVisual();
        ShowWordDictationTextbookScreen();
        return;
    }

    if (option_id == kSleepMusicOptionId) {
        gallery_swipe_enabled_ = false;
        StopCameraPreview();
        UiMenuAiAlarm::HidePanel(*this);
        ClearFeatureVisual();
        ShowSleepMusicScreen();
        return;
    }

    if (UiMenuRemoteSelfie::IsScreenActive(*this)) {
        UiMenuRemoteSelfie::Deactivate(*this);
    }

    if (IsDialogueSceneOption(option_id)) {
        gallery_swipe_enabled_ = false;
        StopCameraPreview();
        UiMenuAiAlarm::HidePanel(*this);
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
        UiMenuAiAlarm::ShowPanel(*this);
    } else {
        UiMenuAiAlarm::HidePanel(*this);
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

}

void UiLcdDisplay::ReturnToMainMenu() {
    ExitDialogueScene(true);
    SetPreviewPresentationMode(PreviewPresentationMode::kChatBubble);
    if (UiMenuRemoteSelfie::IsScreenActive(*this)) {
        UiMenuRemoteSelfie::Deactivate(*this);
    }

    StopWordDictationPlayback();
    StopSleepMusicPlayback(false);
    word_dictation_view_ = WordDictationView::kHidden;
    gallery_swipe_enabled_ = false;
    StopCameraPreview();
    UiMenuAiAlarm::HidePanel(*this);
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
    UiMenuAiAlarm::HidePanel(*this);
    ApplyFeatureVisual(kVisionOptionId);
    {
        DisplayLockGuard lock(this);
        lv_screen_load(ai_screen_);
        MoveStatusBarToScreen(ai_screen_);
    }
    StartCameraPreview(200);
}

bool UiLcdDisplay::IsRemoteSelfieActive() const {
    return UiMenuRemoteSelfie::IsActive(*this);
}

bool UiLcdDisplay::ExecuteRemoteSelfieAction(UiMenuRemoteSelfieAction action) {
    return UiMenuRemoteSelfie::ExecuteAction(*this, action);
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
        photo_viewer_back_button_data_ = UiBackButtonData{this, kBackTargetGallery};
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

void UiLcdDisplay::OnVisionAiButtonClicked(lv_event_t* e) {
    auto* target = static_cast<lv_obj_t*>(lv_event_get_current_target(e));
    auto* display = static_cast<UiLcdDisplay*>(lv_obj_get_user_data(target));
    if (display != nullptr) {
        display->StartVisionRecognition(kVisionDefaultQuestion);
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

void UiLcdDisplay::OnGalleryScreenGesture(lv_event_t* e) {
    (void)e;
}

void UiLcdDisplay::OnPhotoViewerGesture(lv_event_t* e) {
    (void)e;
}

void UiLcdDisplay::OnBackButtonClicked(lv_event_t* e) {
    auto* target = static_cast<lv_obj_t*>(lv_event_get_current_target(e));
    auto* data = static_cast<UiBackButtonData*>(lv_obj_get_user_data(target));
    if (data != nullptr && data->display != nullptr) {
        if (data->target == kBackTargetSettings) {
            UiMenuSetting::ShowSettingsScreen(*data->display);
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
