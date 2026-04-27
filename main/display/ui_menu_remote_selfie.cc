#include "ui_menu_remote_selfie.h"

#include "application.h"
#include "ble_hid.h"
#include "lvgl_theme.h"
#include "ui_display.h"

#include <esp_log.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <initializer_list>
#include <memory>
#include <string>

namespace {

constexpr char kRemoteSelfieTag[] = "RemoteSelfie";
constexpr int kBackTargetMainMenu = 0;
constexpr char kRemoteSelfieDeviceName[] = "Xiaozhi-HID";
constexpr uint32_t kRemoteSelfiePanelColor = 0xF2F2F2;
constexpr uint32_t kRemoteSelfieInactiveColor = 0x979797;
constexpr uint32_t kRemoteSelfieActiveColor = 0x39A856;

std::string NormalizeSpeechText(const std::string& text) {
    std::string normalized;
    normalized.reserve(text.size());
    for (unsigned char ch : text) {
        if (std::isspace(ch) != 0) {
            continue;
        }
        normalized.push_back(static_cast<char>(std::tolower(ch)));
    }
    return normalized;
}

bool ContainsAnyKeyword(const std::string& text, std::initializer_list<const char*> keywords) {
    return std::any_of(keywords.begin(), keywords.end(), [&text](const char* keyword) {
        return keyword != nullptr && text.find(keyword) != std::string::npos;
    });
}

} // namespace

bool UiMenuRemoteSelfie::IsOptionId(int option_id) {
    return option_id == kUiMenuRemoteSelfieOptionId;
}

const char* UiMenuRemoteSelfie::GetMenuText() {
    return "AI自拍杆";
}

const char* UiMenuRemoteSelfie::GetMenuIconAssetName() {
    return "menu_remote_selfie.png";
}

bool UiMenuRemoteSelfie::IsActive(const UiLcdDisplay& display) {
    return display.remote_selfie_.active.load();
}

bool UiMenuRemoteSelfie::IsScreenActive(const UiLcdDisplay& display) {
    return display.remote_selfie_.screen != nullptr && lv_screen_active() == display.remote_selfie_.screen;
}

void UiMenuRemoteSelfie::InitializeScreen(UiLcdDisplay& display) {
    DisplayLockGuard lock(&display);
    auto& state = display.remote_selfie_;

    if (state.screen != nullptr) {
        return;
    }

    auto* lvgl_theme = static_cast<LvglTheme*>(display.GetTheme());

    state.screen = lv_obj_create(nullptr);
    lv_obj_set_style_bg_color(state.screen, lv_color_white(), 0);
    lv_obj_set_style_border_width(state.screen, 0, 0);
    lv_obj_set_style_pad_all(state.screen, 0, 0);
    lv_obj_set_scrollbar_mode(state.screen, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(state.screen, LV_OBJ_FLAG_SCROLLABLE);

    state.card = lv_obj_create(state.screen);
    lv_obj_set_size(state.card, LV_HOR_RES - 4, LV_VER_RES - 4);
    lv_obj_align(state.card, LV_ALIGN_TOP_MID, 0, 2);
    lv_obj_set_style_bg_color(state.card, lv_color_hex(kRemoteSelfiePanelColor), 0);
    lv_obj_set_style_border_width(state.card, 0, 0);
    lv_obj_set_style_radius(state.card, 26, 0);
    lv_obj_set_style_pad_all(state.card, 0, 0);
    lv_obj_set_scrollbar_mode(state.card, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(state.card, LV_OBJ_FLAG_SCROLLABLE);

    auto create_icon_button = [&](lv_coord_t width, lv_coord_t height, lv_align_t align,
                                  lv_coord_t x, lv_coord_t y) {
        lv_obj_t* button = lv_btn_create(state.card);
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

    auto create_camera_shape = [](lv_obj_t* parent, lv_coord_t width, lv_coord_t height,
                                  lv_align_t align, lv_coord_t x, lv_coord_t y,
                                  lv_coord_t radius) {
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

    const lv_coord_t card_width = LV_HOR_RES - 4;
    const lv_coord_t card_height = LV_VER_RES - 4;
    const lv_coord_t control_pad_center_x = card_width / 4 + 2;
    const lv_coord_t control_pad_center_y = card_height / 2 - 4;
    const lv_coord_t control_step = 56;
    const lv_coord_t direction_button_size = 58;
    const lv_coord_t focus_button_size = 44;
    const lv_coord_t shutter_button_width = 112;
    const lv_coord_t shutter_button_height = 88;
    const lv_coord_t shutter_center_x = card_width * 3 / 4 + 6;
    const lv_coord_t shutter_center_y = card_height / 2 - 2;

    const UiMenuRemoteSelfieAction direction_actions[kUiMenuRemoteSelfieDirectionButtonCount] = {
        UiMenuRemoteSelfieAction::kSwipeUp,
        UiMenuRemoteSelfieAction::kSwipeDown,
        UiMenuRemoteSelfieAction::kSwipeLeft,
        UiMenuRemoteSelfieAction::kSwipeRight,
    };
    const lv_coord_t direction_x[kUiMenuRemoteSelfieDirectionButtonCount] = {
        control_pad_center_x - direction_button_size / 2,
        control_pad_center_x - direction_button_size / 2,
        control_pad_center_x - control_step - direction_button_size / 2,
        control_pad_center_x + control_step - direction_button_size / 2,
    };
    const lv_coord_t direction_y[kUiMenuRemoteSelfieDirectionButtonCount] = {
        control_pad_center_y - control_step - direction_button_size / 2,
        control_pad_center_y + control_step - direction_button_size / 2,
        control_pad_center_y - direction_button_size / 2,
        control_pad_center_y - direction_button_size / 2,
    };

    for (size_t i = 0; i < kUiMenuRemoteSelfieDirectionButtonCount; ++i) {
        state.control_data[i] = UiMenuRemoteSelfieControlData{&display, direction_actions[i]};
        state.direction_buttons[i] = create_icon_button(
            direction_button_size, direction_button_size, LV_ALIGN_TOP_LEFT,
            direction_x[i], direction_y[i]);
        lv_obj_set_user_data(state.direction_buttons[i], &state.control_data[i]);
        lv_obj_add_event_cb(state.direction_buttons[i], OnControlButtonClicked, LV_EVENT_CLICKED, nullptr);
        lv_obj_add_event_cb(state.direction_buttons[i], OnDirectionButtonDraw, LV_EVENT_DRAW_MAIN, nullptr);
    }

    state.control_data[4] =
        UiMenuRemoteSelfieControlData{&display, UiMenuRemoteSelfieAction::kFocus};
    state.focus_button = create_icon_button(
        focus_button_size,
        focus_button_size,
        LV_ALIGN_TOP_LEFT,
        control_pad_center_x - focus_button_size / 2,
        control_pad_center_y - focus_button_size / 2);
    lv_obj_set_style_bg_opa(state.focus_button, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_opa(state.focus_button, LV_OPA_80, LV_STATE_PRESSED);
    lv_obj_set_user_data(state.focus_button, &state.control_data[4]);
    lv_obj_add_event_cb(state.focus_button, OnControlButtonClicked, LV_EVENT_CLICKED, nullptr);

    state.shutter_button = create_icon_button(
        shutter_button_width,
        shutter_button_height,
        LV_ALIGN_TOP_LEFT,
        shutter_center_x - shutter_button_width / 2,
        shutter_center_y - shutter_button_height / 2);
    lv_obj_set_style_bg_opa(state.shutter_button, LV_OPA_20, LV_STATE_PRESSED);
    lv_obj_set_user_data(state.shutter_button, &display);
    lv_obj_add_event_cb(state.shutter_button, OnActionButtonClicked, LV_EVENT_CLICKED, nullptr);

    state.shutter_body = create_camera_shape(
        state.shutter_button, 78, 52, LV_ALIGN_BOTTOM_MID, 0, -6, 16);
    state.shutter_top = create_camera_shape(
        state.shutter_button, 34, 16, LV_ALIGN_TOP_LEFT, 0, 0, 8);
    lv_obj_align_to(state.shutter_top, state.shutter_body, LV_ALIGN_OUT_TOP_MID, -14, 6);
    state.shutter_lens_ring = create_camera_shape(
        state.shutter_body, 36, 36, LV_ALIGN_CENTER, 0, 0, LV_RADIUS_CIRCLE);
    lv_obj_set_style_bg_opa(state.shutter_lens_ring, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(state.shutter_lens_ring, 5, 0);
    lv_obj_set_style_border_color(state.shutter_lens_ring, lv_color_white(), 0);
    state.shutter_flash_dot = create_camera_shape(
        state.shutter_body, 10, 10, LV_ALIGN_TOP_RIGHT, -10, 10, LV_RADIUS_CIRCLE);
    lv_obj_set_style_bg_color(state.shutter_flash_dot, lv_color_white(), 0);

    state.ble_status_label = lv_label_create(state.card);
    lv_obj_set_width(state.ble_status_label, LV_HOR_RES - 28);
    lv_obj_align(state.ble_status_label, LV_ALIGN_BOTTOM_MID, 0, -16);
    lv_obj_set_style_text_align(state.ble_status_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(state.ble_status_label, lv_color_black(), 0);
    lv_obj_set_style_text_font(state.ble_status_label, lvgl_theme->text_font()->font(), 0);
    lv_label_set_text(state.ble_status_label, "");

    state.back_button_data = UiBackButtonData{&display, kBackTargetMainMenu};
    display.CreateBackButton(state.screen, &state.back_button_data);
    state.ble_connected = false;
    lv_label_set_text(state.ble_status_label, "请连接蓝牙Xiaozhi-HID");
    UpdateControlStyles(display);
}

void UiMenuRemoteSelfie::ShowScreen(UiLcdDisplay& display) {
    auto& state = display.remote_selfie_;
    if (state.screen == nullptr) {
        InitializeScreen(display);
    }

    state.active = true;

    auto& ble_hid = BleHid::GetInstance();
    ble_hid.SetScreenActive(true);
    bool ble_ready = ble_hid.Initialize();
    if (!ble_ready) {
        ESP_LOGW(kRemoteSelfieTag, "BLE HID init failed with audio active, retrying after suspending audio");
        SuspendAudio(display);
        ble_ready = ble_hid.Initialize();
    }

    ble_hid.OnConnectionStateChanged([display_ptr = &display](bool /*connected*/) {
        if (display_ptr != nullptr) {
            UiMenuRemoteSelfie::RequestBleStatusRefresh(*display_ptr);
        }
    });

    if (ble_ready) {
        ResumeAudio(display);
        UpdateBleStatus(display, ble_hid.IsConnected());
    } else {
        ResumeAudio(display);
        display.ShowNotification("蓝牙启动失败，请返回重试", 2000);
        DisplayLockGuard lock(&display);
        if (state.ble_status_label != nullptr) {
            lv_label_set_text(state.ble_status_label, "蓝牙启动失败，请返回重试");
        }
    }

    UiMenuAiAlarm::HidePanel(display);
    display.ClearFeatureVisual();
    display.gallery_swipe_enabled_ = false;
    display.StopCameraPreview();

    DisplayLockGuard lock(&display);
    lv_screen_load(state.screen);
    display.MoveStatusBarToScreen(state.screen);
}

void UiMenuRemoteSelfie::Deactivate(UiLcdDisplay& display) {
    auto& state = display.remote_selfie_;
    state.active = false;

    auto& app = Application::GetInstance();
    app.ExitChatSession();

    auto& ble_hid = BleHid::GetInstance();
    ble_hid.OnConnectionStateChanged({});
    ble_hid.SetScreenActive(false);
    ble_hid.Deinitialize();

    UpdateBleStatus(display, false);
    ResumeAudio(display);
}

bool UiMenuRemoteSelfie::HandleSpeechRecognized(UiLcdDisplay& display, const std::string& text) {
    if (!IsActive(display)) {
        return false;
    }

    const std::string normalized = NormalizeSpeechText(text);
    if (normalized.empty()) {
        return false;
    }

    if (ContainsAnyKeyword(normalized, {
            "停止录像", "结束录像", "停止录制", "结束录制",
            "stoprecording", "endrecording"})) {
        return ExecuteSpeechAction(
            display, UiMenuRemoteSelfieAction::kCapture, "已发送停止录像指令");
    }

    if (ContainsAnyKeyword(normalized, {
            "开始录像", "开始录制", "录视频", "录像",
            "startrecording", "recordvideo"})) {
        return ExecuteSpeechAction(
            display, UiMenuRemoteSelfieAction::kCapture, "已发送录像指令");
    }

    if (ContainsAnyKeyword(normalized, {
            "拍照", "自拍", "照相", "拍一张", "快门",
            "takephoto", "capturephoto"})) {
        return ExecuteSpeechAction(
            display, UiMenuRemoteSelfieAction::kCapture, "已发送拍照指令");
    }

    if (ContainsAnyKeyword(normalized, {
            "对焦", "聚焦", "点中间", "点击中间", "点一下中间",
            "focus", "tapcenter"})) {
        return ExecuteSpeechAction(
            display, UiMenuRemoteSelfieAction::kFocus, "已发送对焦指令");
    }

    if (ContainsAnyKeyword(normalized, {"向上滑", "往上滑", "上滑", "上划", "swipeup"})) {
        return ExecuteSpeechAction(
            display, UiMenuRemoteSelfieAction::kSwipeUp, "已发送上滑指令");
    }

    if (ContainsAnyKeyword(normalized, {"向下滑", "往下滑", "下滑", "下划", "swipedown"})) {
        return ExecuteSpeechAction(
            display, UiMenuRemoteSelfieAction::kSwipeDown, "已发送下滑指令");
    }

    if (ContainsAnyKeyword(normalized, {"向左滑", "往左滑", "左滑", "左划", "swipeleft"})) {
        return ExecuteSpeechAction(
            display, UiMenuRemoteSelfieAction::kSwipeLeft, "已发送左滑指令");
    }

    if (ContainsAnyKeyword(normalized, {"向右滑", "往右滑", "右滑", "右划", "swiperight"})) {
        return ExecuteSpeechAction(
            display, UiMenuRemoteSelfieAction::kSwipeRight, "已发送右滑指令");
    }

    if (ContainsAnyKeyword(normalized, {
            "语音使能", "开启语音控制", "打开语音控制", "启用语音控制",
            "enablevoicecontrol"})) {
        if (!BleHid::GetInstance().IsConnected()) {
            UpdateBleStatus(display, false);
            return false;
        }
        display.ShowNotification("语音控制已就绪", 1200);
        return true;
    }

    return false;
}

bool UiMenuRemoteSelfie::ExecuteSpeechAction(UiLcdDisplay& display,
                                             UiMenuRemoteSelfieAction action,
                                             const char* success_notification) {
    auto& ble_hid = BleHid::GetInstance();
    if (!ble_hid.IsConnected()) {
        UpdateBleStatus(display, false);
        return false;
    }

    if (!ExecuteAction(display, action)) {
        if (!ble_hid.IsConnected()) {
            UpdateBleStatus(display, false);
            return false;
        }
        display.ShowNotification("自拍指令发送失败，请重试", 1500);
        return true;
    }

    if (success_notification != nullptr && success_notification[0] != '\0') {
        display.ShowNotification(success_notification, 1000);
    }
    return true;
}

void UiMenuRemoteSelfie::UpdateControlStyles(UiLcdDisplay& display) {
    auto& state = display.remote_selfie_;
    const lv_color_t control_color =
        lv_color_hex(state.ble_connected ? kRemoteSelfieActiveColor : kRemoteSelfieInactiveColor);

    for (auto* button : state.direction_buttons) {
        if (button != nullptr) {
            lv_obj_invalidate(button);
        }
    }

    if (state.focus_button != nullptr) {
        lv_obj_set_style_bg_color(state.focus_button, control_color, 0);
        lv_obj_set_style_bg_color(state.focus_button, control_color, LV_STATE_PRESSED);
    }

    if (state.shutter_body != nullptr) {
        lv_obj_set_style_bg_color(state.shutter_body, control_color, 0);
    }

    if (state.shutter_top != nullptr) {
        lv_obj_set_style_bg_color(state.shutter_top, control_color, 0);
    }

    if (state.shutter_lens_ring != nullptr) {
        lv_obj_set_style_border_color(state.shutter_lens_ring, lv_color_white(), 0);
    }

    if (state.shutter_flash_dot != nullptr) {
        lv_obj_set_style_bg_color(state.shutter_flash_dot, lv_color_white(), 0);
    }
}

bool UiMenuRemoteSelfie::ExecuteAction(UiLcdDisplay& display, UiMenuRemoteSelfieAction action) {
    (void)display;
    auto& ble_hid = BleHid::GetInstance();
    bool success = false;

    switch (action) {
    case UiMenuRemoteSelfieAction::kSwipeUp:
        success = ble_hid.SendSwipeUp();
        break;
    case UiMenuRemoteSelfieAction::kSwipeDown:
        success = ble_hid.SendSwipeDown();
        break;
    case UiMenuRemoteSelfieAction::kSwipeLeft:
        success = ble_hid.SendSwipeLeft();
        break;
    case UiMenuRemoteSelfieAction::kSwipeRight:
        success = ble_hid.SendSwipeRight();
        break;
    case UiMenuRemoteSelfieAction::kFocus:
        success = ble_hid.SendMouseClick();
        break;
    case UiMenuRemoteSelfieAction::kCapture:
        success = ble_hid.SendVolumeDown();
        break;
    }

    if (!success) {
        ESP_LOGW(kRemoteSelfieTag, "BLE HID not connected, remote selfie action=%d failed",
                 static_cast<int>(action));
    }

    return success;
}

void UiMenuRemoteSelfie::ExecuteCameraAction(UiLcdDisplay& display) {
    ExecuteAction(display, UiMenuRemoteSelfieAction::kCapture);
}

void UiMenuRemoteSelfie::SuspendAudio(UiLcdDisplay& display) {
    auto& state = display.remote_selfie_;
    if (state.audio_suspended) {
        return;
    }

    Application::GetInstance().SuspendAudioForFeature();
    state.audio_suspended = true;
}

void UiMenuRemoteSelfie::ResumeAudio(UiLcdDisplay& display) {
    auto& state = display.remote_selfie_;
    if (!state.audio_suspended) {
        return;
    }

    Application::GetInstance().ResumeAudioAfterFeature();
    state.audio_suspended = false;
}

void UiMenuRemoteSelfie::RequestBleStatusRefresh(UiLcdDisplay& display) {
    if (display.remote_selfie_.ble_status_label == nullptr) {
        return;
    }

    if (!display.Lock(5)) {
        return;
    }

    lv_async_call(OnBleStatusAsync, &display);
    display.Unlock();
}

void UiMenuRemoteSelfie::UpdateBleStatus(UiLcdDisplay& display, bool connected) {
    auto& state = display.remote_selfie_;
    state.ble_connected = connected;
    if (state.ble_status_label == nullptr) {
        return;
    }

    char status_text[64];
    snprintf(status_text,
             sizeof(status_text),
             connected ? "已连接蓝牙%s" : "请连接蓝牙%s",
             kRemoteSelfieDeviceName);

    DisplayLockGuard lock(&display);
    lv_label_set_text(state.ble_status_label, status_text);
    UpdateControlStyles(display);
}

void UiMenuRemoteSelfie::OnBleStatusAsync(void* user_data) {
    auto* display = static_cast<UiLcdDisplay*>(user_data);
    if (display == nullptr) {
        return;
    }

    UpdateBleStatus(*display, BleHid::GetInstance().IsConnected());
}

void UiMenuRemoteSelfie::OnControlButtonClicked(lv_event_t* e) {
    auto* target = static_cast<lv_obj_t*>(lv_event_get_current_target(e));
    auto* data = static_cast<UiMenuRemoteSelfieControlData*>(lv_obj_get_user_data(target));
    if (data != nullptr && data->display != nullptr) {
        ExecuteAction(*data->display, data->action);
    }
}

void UiMenuRemoteSelfie::OnDirectionButtonDraw(lv_event_t* e) {
    auto* target = static_cast<lv_obj_t*>(lv_event_get_current_target(e));
    auto* data = static_cast<UiMenuRemoteSelfieControlData*>(lv_obj_get_user_data(target));
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
        data->display->remote_selfie_.ble_connected ? kRemoteSelfieActiveColor : kRemoteSelfieInactiveColor);
    triangle.opa = lv_obj_has_state(target, LV_STATE_PRESSED) ? LV_OPA_70 : LV_OPA_COVER;

    switch (data->action) {
    case UiMenuRemoteSelfieAction::kSwipeUp:
        triangle.p[0].x = static_cast<lv_value_precise_t>(center_x - triangle_half_span);
        triangle.p[0].y = static_cast<lv_value_precise_t>(center_y + triangle_base_offset);
        triangle.p[1].x = static_cast<lv_value_precise_t>(center_x);
        triangle.p[1].y = static_cast<lv_value_precise_t>(center_y - triangle_tip_offset);
        triangle.p[2].x = static_cast<lv_value_precise_t>(center_x + triangle_half_span);
        triangle.p[2].y = static_cast<lv_value_precise_t>(center_y + triangle_base_offset);
        break;
    case UiMenuRemoteSelfieAction::kSwipeDown:
        triangle.p[0].x = static_cast<lv_value_precise_t>(center_x - triangle_half_span);
        triangle.p[0].y = static_cast<lv_value_precise_t>(center_y - triangle_base_offset);
        triangle.p[1].x = static_cast<lv_value_precise_t>(center_x + triangle_half_span);
        triangle.p[1].y = static_cast<lv_value_precise_t>(center_y - triangle_base_offset);
        triangle.p[2].x = static_cast<lv_value_precise_t>(center_x);
        triangle.p[2].y = static_cast<lv_value_precise_t>(center_y + triangle_tip_offset);
        break;
    case UiMenuRemoteSelfieAction::kSwipeLeft:
        triangle.p[0].x = static_cast<lv_value_precise_t>(center_x + triangle_base_offset);
        triangle.p[0].y = static_cast<lv_value_precise_t>(center_y - triangle_half_span);
        triangle.p[1].x = static_cast<lv_value_precise_t>(center_x - triangle_tip_offset);
        triangle.p[1].y = static_cast<lv_value_precise_t>(center_y);
        triangle.p[2].x = static_cast<lv_value_precise_t>(center_x + triangle_base_offset);
        triangle.p[2].y = static_cast<lv_value_precise_t>(center_y + triangle_half_span);
        break;
    case UiMenuRemoteSelfieAction::kSwipeRight:
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

void UiMenuRemoteSelfie::OnActionButtonClicked(lv_event_t* e) {
    auto* target = static_cast<lv_obj_t*>(lv_event_get_current_target(e));
    auto* display = static_cast<UiLcdDisplay*>(lv_obj_get_user_data(target));
    if (display != nullptr) {
        ExecuteCameraAction(*display);
    }
}
