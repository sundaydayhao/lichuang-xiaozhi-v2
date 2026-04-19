#include "ui_display.h"

#include "application.h"
#include "ble_hid.h"
#include "lvgl_theme.h"

#include <esp_log.h>

namespace {

constexpr int kMenuIconOriginalSize = 48;

}

void UiLcdDisplay::AllocateCameraPageResources() {
    DisplayLockGuard lock(this);

    if (camera_screen_ != nullptr) {
        return;
    }

    auto* lvgl_theme = static_cast<LvglTheme*>(GetTheme());

    camera_screen_ = lv_obj_create(nullptr);
    lv_obj_set_style_bg_color(camera_screen_, lv_color_white(), 0);
    lv_obj_set_style_border_width(camera_screen_, 0, 0);
    lv_obj_set_style_pad_all(camera_screen_, 0, 0);
    camera_ble_status_label_ = lv_label_create(camera_screen_);
    lv_obj_set_size(camera_ble_status_label_, LV_HOR_RES, 30);
    lv_obj_align(camera_ble_status_label_, LV_ALIGN_BOTTOM_MID, 0, -105);
    lv_obj_set_style_text_align(camera_ble_status_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(camera_ble_status_label_, lv_color_black(), 0);
    lv_obj_set_style_text_font(camera_ble_status_label_, lvgl_theme->text_font()->font(), 0);
    lv_label_set_text(camera_ble_status_label_, "请连接xiaozhi-hid");

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
    camera_back_button_data_ = BackButtonData{this, 0};
    CreateBackButton(camera_screen_, &camera_back_button_data_);
}

void UiLcdDisplay::ReturnToCameraScreen() {
    gallery_swipe_enabled_ = true;
    SetPreviewPresentationMode(PreviewPresentationMode::kOverlay);
    HideAlarmPanel();
    ApplyFeatureVisual(4);
    LoadScreen(ai_screen_);
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
            ESP_LOGW("CompactDisplay", "BLE HID not connected, cannot take photo");
        }
        return;
    }

    is_recording_ = !is_recording_;
    if (!ble_hid.SendVolumeUp()) {
        ESP_LOGW("CompactDisplay", "BLE HID not connected, cannot toggle recording");
    }
    UpdateCameraModeDisplay();
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
    auto& ble_hid = BleHid::GetInstance();
    ble_hid.OnConnectionStateChanged({});
    ble_hid.SetScreenActive(false);
    ble_hid.Deinitialize();
}

void UiLcdDisplay::UpdateCameraBleStatus(bool connected) {
    if (camera_ble_status_label_ == nullptr) {
        return;
    }

    DisplayLockGuard lock(this);
    lv_label_set_text(camera_ble_status_label_, connected ? "已连接" : "请连接xiaozhi-hid");
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
