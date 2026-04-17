#include "ui_display.h"

#include "assets.h"
#include "ble_hid.h"
#include "lvgl_theme.h"
#include "widgets/list/lv_list.h"

#include <esp_err.h>
#include <esp_heap_caps.h>
#include <esp_log.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <functional>
#include <memory>
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
constexpr int kRemoteSelfieOptionId = 6;

lv_dir_t GetGestureDirection() {
    lv_indev_t* indev = lv_indev_active();
    return indev ? lv_indev_get_gesture_dir(indev) : LV_DIR_NONE;
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

    InitializeMainMenu();
}

UiLcdDisplay::~UiLcdDisplay() {
    StopCameraPreview();
    if (camera_preview_timer_ != nullptr) {
        esp_timer_delete(camera_preview_timer_);
        camera_preview_timer_ = nullptr;
    }
    BleHid::GetInstance().OnConnectionStateChanged({});
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
        return "睡前故事";
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
        return "设置";
    case 12:
        return "助眠音乐";
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
        return "menu_bedtime_story.png";
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
        return "menu_settings.png";
    case 12:
        return "menu_sleep_music.png";
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
        return "bg_bedtime_story.png";
    case 8:
        return "bg_word_dictation.png";
    case 9:
        return "bg_face_reading.png";
    default:
        return nullptr;
    }
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

    if (feature_background_image_ == nullptr || emoji_box_ == nullptr) {
        return;
    }

    const lv_image_dsc_t* background = LoadAssetImage(GetFeatureBackgroundAssetName(option_id));
    if (background == nullptr) {
        lv_obj_add_flag(feature_background_image_, LV_OBJ_FLAG_HIDDEN);
        if (preview_image_ != nullptr) {
            lv_obj_add_flag(preview_image_, LV_OBJ_FLAG_HIDDEN);
        }
        lv_obj_remove_flag(emoji_box_, LV_OBJ_FLAG_HIDDEN);
        if (chat_message_label_ != nullptr) {
            lv_label_set_text(chat_message_label_, GetMenuText(option_id));
        }
        return;
    }

    if (chat_message_label_ != nullptr) {
        lv_label_set_text(chat_message_label_, "");
    }

    lv_image_set_src(feature_background_image_, background);
    lv_obj_add_flag(emoji_box_, LV_OBJ_FLAG_HIDDEN);
    if (preview_image_ != nullptr) {
        lv_obj_add_flag(preview_image_, LV_OBJ_FLAG_HIDDEN);
    }
    lv_obj_remove_flag(feature_background_image_, LV_OBJ_FLAG_HIDDEN);
}

void UiLcdDisplay::ClearFeatureVisual() {
    current_feature_option_id_ = 0;

    DisplayLockGuard lock(this);
    if (preview_timer_ != nullptr) {
        esp_timer_stop(preview_timer_);
    }
    preview_image_cached_.reset();

    if (feature_background_image_ != nullptr) {
        lv_obj_add_flag(feature_background_image_, LV_OBJ_FLAG_HIDDEN);
    }
    if (preview_image_ != nullptr) {
        lv_obj_add_flag(preview_image_, LV_OBJ_FLAG_HIDDEN);
    }
    if (emoji_box_ != nullptr) {
        lv_obj_remove_flag(emoji_box_, LV_OBJ_FLAG_HIDDEN);
    }
    if (chat_message_label_ != nullptr) {
        lv_label_set_text(chat_message_label_, "");
    }
}

void UiLcdDisplay::InitializeMainMenu() {
    DisplayLockGuard lock(this);

    auto* lvgl_theme = static_cast<LvglTheme*>(GetTheme());
    ai_screen_ = lv_screen_active();

    feature_background_image_ = lv_image_create(container_);
    lv_obj_set_size(feature_background_image_, LV_HOR_RES, LV_VER_RES);
    lv_obj_add_flag(feature_background_image_, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_image_set_inner_align(feature_background_image_, LV_IMAGE_ALIGN_STRETCH);
    lv_obj_set_style_bg_opa(feature_background_image_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(feature_background_image_, 0, 0);
    lv_obj_center(feature_background_image_);
    lv_obj_add_flag(feature_background_image_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_to_index(feature_background_image_, 0);

    lv_obj_add_event_cb(ai_screen_, OnAiScreenGesture, LV_EVENT_GESTURE, this);
    lv_obj_clear_flag(ai_screen_, LV_OBJ_FLAG_GESTURE_BUBBLE);

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

    camera_screen_ = lv_obj_create(nullptr);
    lv_obj_set_style_bg_color(camera_screen_, lv_color_white(), 0);
    lv_obj_set_style_border_width(camera_screen_, 0, 0);
    lv_obj_set_style_pad_all(camera_screen_, 0, 0);
    lv_obj_add_event_cb(camera_screen_, OnCameraScreenGesture, LV_EVENT_GESTURE, this);
    lv_obj_clear_flag(camera_screen_, LV_OBJ_FLAG_GESTURE_BUBBLE);

    camera_ble_status_label_ = lv_label_create(camera_screen_);
    lv_obj_set_size(camera_ble_status_label_, LV_HOR_RES, 30);
    lv_obj_align(camera_ble_status_label_, LV_ALIGN_BOTTOM_MID, 0, -105);
    lv_obj_set_style_text_align(camera_ble_status_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(camera_ble_status_label_, lv_color_black(), 0);
    lv_obj_set_style_text_font(camera_ble_status_label_, lvgl_theme->text_font()->font(), 0);
    lv_label_set_text(camera_ble_status_label_, "请连接xiaozhi-hid");

    InitializeCameraScreen();

    MoveStatusBarToScreen(menu_screen_);
    lv_screen_load(menu_screen_);
}

void UiLcdDisplay::InitializeCameraScreen() {
    DisplayLockGuard lock(this);

    auto* lvgl_theme = static_cast<LvglTheme*>(GetTheme());

    lv_obj_t* camera_bar = lv_obj_create(camera_screen_);
    lv_obj_set_size(camera_bar, LV_HOR_RES, 80);
    lv_obj_align(camera_bar, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(camera_bar, lvgl_theme->background_color(), 0);
    lv_obj_set_style_border_width(camera_bar, 1, 0);
    lv_obj_set_style_border_color(camera_bar, lvgl_theme->border_color(), 0);
    lv_obj_set_style_radius(camera_bar, 0, 0);
    lv_obj_set_scrollbar_mode(camera_bar, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(camera_bar, LV_OBJ_FLAG_SCROLLABLE);

    camera_mode_btn_left_ = lv_btn_create(camera_bar);
    lv_obj_set_size(camera_mode_btn_left_, kMenuIconOriginalSize, kMenuIconOriginalSize);
    lv_obj_set_style_radius(camera_mode_btn_left_, kMenuIconOriginalSize / 2, 0);
    lv_obj_set_style_bg_opa(camera_mode_btn_left_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(camera_mode_btn_left_, 0, 0);
    lv_obj_align(camera_mode_btn_left_, LV_ALIGN_LEFT_MID, 40, 0);
    lv_obj_clear_flag(camera_mode_btn_left_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t* video_icon_img = lv_image_create(camera_mode_btn_left_);
    if (const lv_image_dsc_t* icon = LoadAssetImage("menu_settings.png"); icon != nullptr) {
        lv_image_set_src(video_icon_img, icon);
    }
    lv_obj_center(video_icon_img);
    lv_obj_set_user_data(camera_mode_btn_left_, this);
    lv_obj_add_event_cb(camera_mode_btn_left_, OnCameraModeButtonClicked, LV_EVENT_CLICKED, nullptr);

    camera_mode_btn_center_ = lv_btn_create(camera_bar);
    lv_obj_set_size(camera_mode_btn_center_, 44, 44);
    lv_obj_set_style_radius(camera_mode_btn_center_, 22, 0);
    lv_obj_align(camera_mode_btn_center_, LV_ALIGN_CENTER, 0, 0);
    lv_obj_clear_flag(camera_mode_btn_center_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_user_data(camera_mode_btn_center_, this);
    lv_obj_add_event_cb(camera_mode_btn_center_, OnCameraActionButtonClicked, LV_EVENT_CLICKED, nullptr);

    camera_mode_btn_right_ = lv_btn_create(camera_bar);
    lv_obj_set_size(camera_mode_btn_right_, kMenuIconOriginalSize, kMenuIconOriginalSize);
    lv_obj_set_style_radius(camera_mode_btn_right_, kMenuIconOriginalSize / 2, 0);
    lv_obj_set_style_bg_opa(camera_mode_btn_right_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(camera_mode_btn_right_, 0, 0);
    lv_obj_align(camera_mode_btn_right_, LV_ALIGN_RIGHT_MID, -40, 0);
    lv_obj_clear_flag(camera_mode_btn_right_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t* photo_icon_img = lv_image_create(camera_mode_btn_right_);
    if (const lv_image_dsc_t* icon = LoadAssetImage("menu_remote_selfie.png"); icon != nullptr) {
        lv_image_set_src(photo_icon_img, icon);
    }
    lv_obj_center(photo_icon_img);
    lv_obj_set_user_data(camera_mode_btn_right_, this);
    lv_obj_add_event_cb(camera_mode_btn_right_, OnCameraModeButtonClicked, LV_EVENT_CLICKED, nullptr);

    current_camera_mode_ = CameraMode::kPhoto;
    UpdateCameraModeDisplay();
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
    lv_obj_add_event_cb(gallery_screen_, OnGalleryScreenGesture, LV_EVENT_GESTURE, this);
    lv_obj_clear_flag(gallery_screen_, LV_OBJ_FLAG_GESTURE_BUBBLE);

    gallery_list_ = lv_list_create(gallery_screen_);
    lv_obj_set_size(gallery_list_, LV_HOR_RES, LV_VER_RES - kStatusBarHeight);
    lv_obj_align(gallery_list_, LV_ALIGN_TOP_MID, 0, kStatusBarHeight);
    lv_obj_set_style_bg_color(gallery_list_, lv_color_white(), 0);
    lv_obj_set_style_border_width(gallery_list_, 0, 0);
    lv_obj_set_style_pad_all(gallery_list_, 0, 0);
}

void UiLcdDisplay::MoveStatusBarToScreen(lv_obj_t* target_screen) {
    if (status_bar_ == nullptr || target_screen == nullptr) {
        return;
    }

    lv_obj_set_parent(status_bar_, target_screen);
    lv_obj_clear_flag(status_bar_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_align(status_bar_, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_move_foreground(status_bar_);
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

    if (option_id < 1 || option_id > static_cast<int>(kMenuItemCount)) {
        return;
    }

    if (option_id == kRemoteSelfieOptionId) {
        auto& ble_hid = BleHid::GetInstance();
        ble_hid.Initialize();
        ble_hid.OnConnectionStateChanged([this](bool connected) {
            UpdateCameraBleStatus(connected);
        });
        UpdateCameraBleStatus(ble_hid.IsConnected());

        ClearFeatureVisual();
        gallery_swipe_enabled_ = false;
        StopCameraPreview();
        DisplayLockGuard lock(this);
        lv_screen_load(camera_screen_);
        MoveStatusBarToScreen(camera_screen_);
        return;
    }

    if (lv_screen_active() == camera_screen_) {
        BleHid::GetInstance().Deinitialize();
    }

    ApplyFeatureVisual(option_id);

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
    if (lv_screen_active() == camera_screen_) {
        BleHid::GetInstance().Deinitialize();
    }

    gallery_swipe_enabled_ = false;
    StopCameraPreview();
    ClearFeatureVisual();
    {
        DisplayLockGuard lock(this);
        lv_screen_load(menu_screen_);
        MoveStatusBarToScreen(menu_screen_);
    }
}

void UiLcdDisplay::ReturnToCameraScreen() {
    gallery_swipe_enabled_ = true;
    ApplyFeatureVisual(kVisionOptionId);
    {
        DisplayLockGuard lock(this);
        lv_screen_load(ai_screen_);
        MoveStatusBarToScreen(ai_screen_);
    }
    StartCameraPreview(200);
}

void UiLcdDisplay::SetCameraMode(CameraMode mode) {
    if (current_camera_mode_ == mode) {
        return;
    }

    current_camera_mode_ = mode;
    UpdateCameraModeDisplay();
}

void UiLcdDisplay::UpdateCameraModeDisplay() {
    if (camera_mode_btn_center_ == nullptr) {
        return;
    }

    DisplayLockGuard lock(this);

    lv_obj_remove_style_all(camera_mode_btn_center_);
    lv_obj_set_size(camera_mode_btn_center_, 44, 44);
    lv_obj_align(camera_mode_btn_center_, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_radius(camera_mode_btn_center_, 22, 0);
    lv_obj_clear_flag(camera_mode_btn_center_, LV_OBJ_FLAG_SCROLLABLE);

    if (current_camera_mode_ == CameraMode::kPhoto) {
        lv_obj_set_style_bg_color(camera_mode_btn_center_, lv_color_make(150, 150, 150), 0);
        lv_obj_set_style_bg_opa(camera_mode_btn_center_, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(camera_mode_btn_center_, 0, 0);
        lv_obj_set_style_border_opa(camera_mode_btn_center_, LV_OPA_TRANSP, 0);
        lv_obj_set_style_bg_color(camera_mode_btn_center_, lv_color_make(150, 150, 150), LV_STATE_PRESSED);
        lv_obj_set_style_bg_opa(camera_mode_btn_center_, LV_OPA_COVER, LV_STATE_PRESSED);
        return;
    }

    lv_color_t record_color = lv_color_make(0, 0, 255);
    if (is_recording_) {
        lv_obj_set_style_bg_opa(camera_mode_btn_center_, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_color(camera_mode_btn_center_, record_color, 0);
        lv_obj_set_style_border_width(camera_mode_btn_center_, 4, 0);
        lv_obj_set_style_border_opa(camera_mode_btn_center_, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_opa(camera_mode_btn_center_, LV_OPA_TRANSP, LV_STATE_PRESSED);
        lv_obj_set_style_border_color(camera_mode_btn_center_, record_color, LV_STATE_PRESSED);
        lv_obj_set_style_border_width(camera_mode_btn_center_, 4, LV_STATE_PRESSED);
        lv_obj_set_style_border_opa(camera_mode_btn_center_, LV_OPA_COVER, LV_STATE_PRESSED);
    } else {
        lv_obj_set_style_bg_color(camera_mode_btn_center_, record_color, 0);
        lv_obj_set_style_bg_opa(camera_mode_btn_center_, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(camera_mode_btn_center_, 0, 0);
        lv_obj_set_style_border_opa(camera_mode_btn_center_, LV_OPA_TRANSP, 0);
        lv_obj_set_style_bg_color(camera_mode_btn_center_, record_color, LV_STATE_PRESSED);
        lv_obj_set_style_bg_opa(camera_mode_btn_center_, LV_OPA_COVER, LV_STATE_PRESSED);
    }
}

void UiLcdDisplay::ExecuteCameraAction() {
    auto& ble_hid = BleHid::GetInstance();

    if (current_camera_mode_ == CameraMode::kPhoto) {
        if (!ble_hid.SendVolumeDown()) {
            ESP_LOGW(TAG, "BLE HID not connected, cannot take photo");
        }
        return;
    }

    is_recording_ = !is_recording_;
    if (!ble_hid.SendVolumeUp()) {
        ESP_LOGW(TAG, "BLE HID not connected, cannot toggle recording");
    }
    UpdateCameraModeDisplay();
}

void UiLcdDisplay::UpdateCameraBleStatus(bool connected) {
    if (camera_ble_status_label_ == nullptr) {
        return;
    }

    DisplayLockGuard lock(this);
    lv_label_set_text(camera_ble_status_label_, connected ? "已连接" : "请连接xiaozhi-hid");
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
        lv_obj_add_event_cb(photo_viewer_screen_, OnPhotoViewerGesture, LV_EVENT_GESTURE, this);
        lv_obj_clear_flag(photo_viewer_screen_, LV_OBJ_FLAG_GESTURE_BUBBLE);

        photo_viewer_container_ = lv_obj_create(photo_viewer_screen_);
        lv_obj_set_size(photo_viewer_container_, LV_HOR_RES, LV_VER_RES);
        lv_obj_set_style_bg_color(photo_viewer_container_, lv_color_black(), 0);
        lv_obj_set_style_border_width(photo_viewer_container_, 0, 0);
        lv_obj_set_style_pad_all(photo_viewer_container_, 0, 0);
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
    if (dir == LV_DIR_RIGHT) {
        display->ReturnToMainMenu();
    } else if (dir == LV_DIR_LEFT && display->gallery_swipe_enabled_) {
        display->ShowGallery();
    }
}

void UiLcdDisplay::OnCameraScreenGesture(lv_event_t* e) {
    auto* display = static_cast<UiLcdDisplay*>(lv_event_get_user_data(e));
    if (display != nullptr && GetGestureDirection() == LV_DIR_RIGHT) {
        display->ReturnToMainMenu();
    }
}

void UiLcdDisplay::OnGalleryScreenGesture(lv_event_t* e) {
    auto* display = static_cast<UiLcdDisplay*>(lv_event_get_user_data(e));
    if (display != nullptr && GetGestureDirection() == LV_DIR_RIGHT) {
        display->ReturnToCameraScreen();
    }
}

void UiLcdDisplay::OnPhotoViewerGesture(lv_event_t* e) {
    auto* display = static_cast<UiLcdDisplay*>(lv_event_get_user_data(e));
    if (display != nullptr && GetGestureDirection() == LV_DIR_RIGHT) {
        display->ShowGallery();
    }
}

void UiLcdDisplay::OnCameraModeButtonClicked(lv_event_t* e) {
    auto* target = static_cast<lv_obj_t*>(lv_event_get_current_target(e));
    auto* display = static_cast<UiLcdDisplay*>(lv_obj_get_user_data(target));
    if (display == nullptr) {
        return;
    }

    if (target == display->camera_mode_btn_left_) {
        display->SetCameraMode(CameraMode::kVideo);
    } else if (target == display->camera_mode_btn_right_) {
        display->SetCameraMode(CameraMode::kPhoto);
    }
}

void UiLcdDisplay::OnCameraActionButtonClicked(lv_event_t* e) {
    auto* target = static_cast<lv_obj_t*>(lv_event_get_current_target(e));
    auto* display = static_cast<UiLcdDisplay*>(lv_obj_get_user_data(target));
    if (display != nullptr) {
        display->ExecuteCameraAction();
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
