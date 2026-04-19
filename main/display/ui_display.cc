#include "ui_display.h"

#include "application.h"
#include "ui_print_page.h"
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
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <wifi_station.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <functional>
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

void UiLcdDisplay::LoadScreen(lv_obj_t* target_screen) {
    if (target_screen == nullptr) {
        return;
    }

    lv_obj_t* previous_screen = lv_screen_active();
    {
        DisplayLockGuard lock(this);
        lv_screen_load(target_screen);
        MoveStatusBarToScreen(target_screen);
    }

    ReleaseScreenResources(previous_screen, target_screen);
}

void UiLcdDisplay::ReleaseScreenResources(lv_obj_t* previous_screen, lv_obj_t* target_screen) {
    previous_screen = TakeValidObject(previous_screen);
    target_screen = TakeValidObject(target_screen);
    if (previous_screen == nullptr || previous_screen == target_screen) {
        return;
    }

    if (previous_screen == settings_screen_) {
        DestroySettingsScreen();
    } else if (previous_screen == network_switch_screen_) {
        DestroyNetworkSwitchScreen();
    } else if (previous_screen == server_switch_screen_) {
        DestroyServerSwitchScreen();
    } else if (previous_screen == language_switch_screen_) {
        DestroyLanguageSwitchScreen();
    } else if (previous_screen == about_screen_) {
        DestroyAboutScreen();
    } else if (previous_screen == camera_screen_) {
        DestroyCameraScreen();
    } else if (previous_screen == gallery_screen_) {
        DestroyGalleryScreen();
    } else if (previous_screen == photo_viewer_screen_) {
        DestroyPhotoViewerScreen();
    } else if (previous_screen == word_dictation_screen_) {
        DestroyWordDictationScreen();
    } else if (previous_screen == sleep_music_screen_) {
        DestroySleepMusicScreen();
    }
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

std::string UiLcdDisplay::BuildDialogueScenePayload(int option_id) const {
    const char* scene = nullptr;
    const char* mode = nullptr;
    std::string instructions;
    std::string language = GetSelectedLanguageCode();

    switch (option_id) {
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
    if (option_id == kAiPrintOptionId) {
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
        SuspendAudioForRemoteSelfie();
        auto& ble_hid = BleHid::GetInstance();
        ble_hid.SetScreenActive(true);
        const bool ble_ready = ble_hid.Initialize();
        ble_hid.OnConnectionStateChanged([this](bool connected) {
            UpdateCameraBleStatus(connected);
        });
        if (ble_ready) {
            UpdateCameraBleStatus(ble_hid.IsConnected());
        } else {
            ShowNotification("蓝牙启动失败，请返回重试", 2000);
            DisplayLockGuard lock(this);
            lv_label_set_text(camera_ble_status_label_, "蓝牙启动失败，请返回重试");
        }

        HideAlarmPanel();
        ClearFeatureVisual();
        gallery_swipe_enabled_ = false;
        StopCameraPreview();
        LoadScreen(camera_screen_);
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
        LoadScreen(ai_screen_);
        return;
    }

    ApplyFeatureVisual(option_id);
    if (option_id == kAiAlarmOptionId) {
        ShowAlarmPanel();
    } else {
        HideAlarmPanel();
    }

    LoadScreen(ai_screen_);

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
    LoadScreen(menu_screen_);
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
