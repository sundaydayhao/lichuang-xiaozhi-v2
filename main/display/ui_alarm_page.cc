#include "ui_display.h"

#include "lvgl_theme.h"

#include <esp_err.h>
#include <font_awesome.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cctype>
#include <cstdio>
#include <ctime>
#include <string>
#include <string_view>
#include <vector>

namespace {

constexpr int kStatusBarHeight = 27;
constexpr int kAlarmRingSegmentCount = 24;
constexpr const char* kAlarmCountdownPlaceholder = "--:--";
constexpr std::array<const char*, 7> kWeekdayShortNames = {"日", "一", "二", "三", "四", "五", "六"};

lv_obj_t* TakeValidObject(lv_obj_t*& object) {
    if (object != nullptr && lv_obj_is_valid(object)) {
        return object;
    }

    object = nullptr;
    return nullptr;
}

std::string FormatCurrentTimeForDisplay(bool has_valid_time, time_t now) {
    if (!has_valid_time) {
        return "--:--";
    }

    struct tm local_tm;
    localtime_r(&now, &local_tm);
    char buffer[16];
    strftime(buffer, sizeof(buffer), "%H:%M", &local_tm);
    return buffer;
}

std::string FormatCurrentDateForDisplay(bool has_valid_time, time_t now) {
    if (!has_valid_time) {
        return "----年--月--日";
    }

    struct tm local_tm;
    localtime_r(&now, &local_tm);
    char buffer[64];
    snprintf(buffer, sizeof(buffer), "%d年%d月%d日", local_tm.tm_year + 1900, local_tm.tm_mon + 1,
             local_tm.tm_mday);
    return buffer;
}

std::string FormatCurrentWeekdayForDisplay(bool has_valid_time, time_t now) {
    if (!has_valid_time) {
        return "星期-";
    }

    struct tm local_tm;
    localtime_r(&now, &local_tm);
    const int weekday = local_tm.tm_wday;
    if (weekday < 0 || weekday >= static_cast<int>(kWeekdayShortNames.size())) {
        return "星期-";
    }
    return std::string("星期") + kWeekdayShortNames[weekday];
}

bool IsDigitsOnly(std::string_view text) {
    if (text.empty()) {
        return false;
    }

    for (const char ch : text) {
        if (!std::isdigit(static_cast<unsigned char>(ch))) {
            return false;
        }
    }

    return true;
}

bool TryParseAlarmCountdownSeconds(std::string_view text, int& total_seconds) {
    total_seconds = -1;
    if (text.empty() || text == kAlarmCountdownPlaceholder) {
        return false;
    }

    std::vector<int> parts;
    size_t start = 0;
    while (start <= text.size()) {
        const size_t end = text.find(':', start);
        const std::string_view part =
            end == std::string_view::npos ? text.substr(start) : text.substr(start, end - start);
        if (!IsDigitsOnly(part)) {
            return false;
        }

        parts.push_back(std::stoi(std::string(part)));
        if (end == std::string_view::npos) {
            break;
        }
        start = end + 1;
    }

    if (parts.size() != 2) {
        return false;
    }

    total_seconds = std::max(0, parts[0]) * 3600 + std::max(0, parts[1]) * 60;
    return total_seconds > 0;
}

lv_color_t GetAlarmRingActiveColor(int index) {
    if (index < kAlarmRingSegmentCount / 3) {
        return lv_color_hex(0x2D6BFF);
    }
    if (index < (kAlarmRingSegmentCount * 2) / 3) {
        return lv_color_hex(0xFFC107);
    }
    return lv_color_hex(0xFF7A59);
}

} // namespace

void UiLcdDisplay::ResetAlarmPanelObjects() {
    alarm_panel_ = nullptr;
    alarm_subtitle_label_ = nullptr;
    alarm_now_time_label_ = nullptr;
    alarm_date_label_ = nullptr;
    alarm_hint_label_ = nullptr;
    alarm_next_icon_label_ = nullptr;
    alarm_next_time_label_ = nullptr;
    alarm_ring_frame_ = nullptr;
    alarm_ring_inner_ = nullptr;
    alarm_clock_time_label_ = nullptr;
    alarm_countdown_label_ = nullptr;
    alarm_hourglass_label_ = nullptr;
    alarm_ring_segments_.fill(nullptr);
    alarm_countdown_active_ = false;
    alarm_countdown_initial_seconds_ = -1;
    alarm_countdown_last_remaining_seconds_ = -1;
}

void UiLcdDisplay::InitializeAlarmSupport() {
    esp_timer_create_args_t alarm_ui_timer_args = {
        .callback = [](void* arg) {
            auto* display = static_cast<UiLcdDisplay*>(arg);
            if (display != nullptr) {
                display->RefreshAlarmUi();
            }
        },
        .arg = this,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "alarm_ui_timer",
        .skip_unhandled_events = true,
    };
    ESP_ERROR_CHECK(esp_timer_create(&alarm_ui_timer_args, &alarm_ui_timer_));
}

void UiLcdDisplay::DeinitializeAlarmSupport() {
    StopAlarmUiRefresh();
    if (alarm_ui_timer_ != nullptr) {
        esp_timer_delete(alarm_ui_timer_);
        alarm_ui_timer_ = nullptr;
    }
}

bool UiLcdDisplay::HasValidLocalTime() const {
    time_t now = time(nullptr);
    struct tm local_tm;
    localtime_r(&now, &local_tm);
    return local_tm.tm_year >= (2025 - 1900);
}

void UiLcdDisplay::StartAlarmUiRefresh() {
    if (alarm_ui_timer_ == nullptr) {
        return;
    }

    esp_timer_stop(alarm_ui_timer_);
    RefreshAlarmUi();
    ESP_ERROR_CHECK(esp_timer_start_periodic(alarm_ui_timer_, 1000000));
}

void UiLcdDisplay::StopAlarmUiRefresh() {
    if (alarm_ui_timer_ != nullptr) {
        esp_timer_stop(alarm_ui_timer_);
    }
}

void UiLcdDisplay::InitializeAlarmPanel() {
    if (ai_screen_ == nullptr) {
        return;
    }

    if (alarm_panel_ != nullptr && !lv_obj_is_valid(alarm_panel_)) {
        ResetAlarmPanelObjects();
    }

    if (alarm_panel_ != nullptr) {
        return;
    }

    auto* lvgl_theme = static_cast<LvglTheme*>(GetTheme());
    const lv_coord_t panel_top = kStatusBarHeight;

    alarm_panel_ = lv_obj_create(ai_screen_);
    lv_obj_set_size(alarm_panel_, LV_HOR_RES, LV_VER_RES - panel_top);
    lv_obj_align(alarm_panel_, LV_ALIGN_TOP_MID, 0, panel_top);
    lv_obj_set_style_bg_color(alarm_panel_, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(alarm_panel_, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(alarm_panel_, 0, 0);
    lv_obj_set_style_radius(alarm_panel_, 0, 0);
    lv_obj_set_style_pad_all(alarm_panel_, 0, 0);
    lv_obj_set_layout(alarm_panel_, LV_LAYOUT_NONE);
    lv_obj_set_scrollbar_mode(alarm_panel_, LV_SCROLLBAR_MODE_OFF);
    lv_obj_remove_flag(alarm_panel_, static_cast<lv_obj_flag_t>(
                                         LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_SCROLL_ELASTIC |
                                         LV_OBJ_FLAG_SCROLL_MOMENTUM | LV_OBJ_FLAG_SCROLL_CHAIN));

    alarm_subtitle_label_ = lv_label_create(alarm_panel_);
    lv_obj_set_width(alarm_subtitle_label_, LV_HOR_RES - 24);
    lv_obj_align(alarm_subtitle_label_, LV_ALIGN_TOP_LEFT, 12, 8);
    lv_obj_set_style_text_align(alarm_subtitle_label_, LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_set_style_text_font(alarm_subtitle_label_, lvgl_theme->text_font()->font(), 0);
    lv_obj_set_style_text_color(alarm_subtitle_label_, lv_color_hex(0x9CA3AF), 0);
    lv_label_set_text(alarm_subtitle_label_, "");
    lv_obj_add_flag(alarm_subtitle_label_, LV_OBJ_FLAG_HIDDEN);

    alarm_now_time_label_ = lv_label_create(alarm_panel_);
    lv_obj_set_style_text_font(alarm_now_time_label_, lvgl_theme->text_font()->font(), 0);
    lv_obj_set_style_text_color(alarm_now_time_label_, lv_color_hex(0x111827), 0);
    lv_obj_set_style_text_letter_space(alarm_now_time_label_, 2, 0);

    alarm_date_label_ = lv_label_create(alarm_panel_);
    lv_label_set_long_mode(alarm_date_label_, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_font(alarm_date_label_, lvgl_theme->text_font()->font(), 0);
    lv_obj_set_style_text_color(alarm_date_label_, lv_color_hex(0x6B7280), 0);

    alarm_next_icon_label_ = lv_label_create(alarm_panel_);
    lv_obj_set_style_text_font(alarm_next_icon_label_, lvgl_theme->icon_font()->font(), 0);
    lv_obj_set_style_text_color(alarm_next_icon_label_, lv_color_hex(0x2563EB), 0);

    alarm_next_time_label_ = lv_label_create(alarm_panel_);
    lv_obj_set_style_text_font(alarm_next_time_label_, lvgl_theme->text_font()->font(), 0);
    lv_obj_set_style_text_color(alarm_next_time_label_, lv_color_hex(0x2563EB), 0);

    alarm_hint_label_ = lv_label_create(alarm_panel_);
    lv_obj_set_width(alarm_hint_label_, 108);
    lv_label_set_long_mode(alarm_hint_label_, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_font(alarm_hint_label_, lvgl_theme->text_font()->font(), 0);
    lv_obj_set_style_text_color(alarm_hint_label_, lv_color_hex(0x2563EB), 0);

    alarm_ring_frame_ = lv_obj_create(alarm_panel_);
    lv_obj_set_style_radius(alarm_ring_frame_, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(alarm_ring_frame_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(alarm_ring_frame_, 0, 0);
    lv_obj_set_style_pad_all(alarm_ring_frame_, 0, 0);
    lv_obj_set_style_shadow_width(alarm_ring_frame_, 0, 0);
    lv_obj_remove_flag(alarm_ring_frame_, static_cast<lv_obj_flag_t>(
                                             LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_SCROLL_ELASTIC |
                                             LV_OBJ_FLAG_SCROLL_MOMENTUM | LV_OBJ_FLAG_SCROLL_CHAIN));

    for (int i = 0; i < kAlarmRingSegmentCount; ++i) {
        alarm_ring_segments_[i] = lv_obj_create(alarm_ring_frame_);
        lv_obj_set_style_radius(alarm_ring_segments_[i], LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_border_width(alarm_ring_segments_[i], 0, 0);
        lv_obj_set_style_pad_all(alarm_ring_segments_[i], 0, 0);
        lv_obj_set_style_shadow_width(alarm_ring_segments_[i], 0, 0);
    }

    alarm_ring_inner_ = lv_obj_create(alarm_ring_frame_);
    lv_obj_set_style_radius(alarm_ring_inner_, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(alarm_ring_inner_, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(alarm_ring_inner_, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(alarm_ring_inner_, 0, 0);
    lv_obj_set_style_pad_all(alarm_ring_inner_, 0, 0);
    lv_obj_set_style_shadow_width(alarm_ring_inner_, 10, 0);
    lv_obj_set_style_shadow_color(alarm_ring_inner_, lv_color_hex(0xD1D5DB), 0);

    alarm_clock_time_label_ = lv_label_create(alarm_ring_inner_);
    lv_obj_set_style_text_font(alarm_clock_time_label_, lvgl_theme->text_font()->font(), 0);
    lv_obj_set_style_text_color(alarm_clock_time_label_, lv_color_hex(0x6B7280), 0);

    alarm_countdown_label_ = lv_label_create(alarm_ring_inner_);
    lv_obj_set_style_text_font(alarm_countdown_label_, lvgl_theme->text_font()->font(), 0);
    lv_obj_set_style_text_color(alarm_countdown_label_, lv_color_hex(0x111827), 0);

    alarm_hourglass_label_ = lv_label_create(alarm_ring_inner_);
    lv_obj_set_style_text_font(alarm_hourglass_label_, lvgl_theme->icon_font()->font(), 0);
    lv_obj_set_style_text_color(alarm_hourglass_label_, lv_color_hex(0xD97706), 0);

    lv_obj_add_flag(alarm_panel_, LV_OBJ_FLAG_HIDDEN);
}

void UiLcdDisplay::RefreshAlarmUi() {
    lv_obj_t* alarm_panel = TakeValidObject(alarm_panel_);
    if (alarm_panel == nullptr) {
        ResetAlarmPanelObjects();
        return;
    }

    if (lv_obj_has_flag(alarm_panel, LV_OBJ_FLAG_HIDDEN)) {
        return;
    }

    DisplayLockGuard lock(this);
    const lv_coord_t panel_w = lv_obj_get_width(alarm_panel);
    const lv_coord_t panel_h = lv_obj_get_height(alarm_panel);
    if (panel_w <= 0 || panel_h <= 0) {
        return;
    }

    const bool has_valid_time = HasValidLocalTime();
    const time_t now = time(nullptr);
    const std::string now_text = FormatCurrentTimeForDisplay(has_valid_time, now);
    const std::string date_text = FormatCurrentDateForDisplay(has_valid_time, now);
    const std::string weekday_text = FormatCurrentWeekdayForDisplay(has_valid_time, now);
    const std::string countdown_text = kAlarmCountdownPlaceholder;

    lv_obj_set_style_bg_color(ai_screen_, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(ai_screen_, LV_OPA_COVER, 0);
    if (status_bar_ != nullptr) {
        lv_obj_set_style_bg_color(status_bar_, lv_color_white(), 0);
        lv_obj_set_style_bg_opa(status_bar_, LV_OPA_COVER, 0);
        lv_obj_set_style_text_color(status_bar_, lv_color_hex(0x111827), 0);
    }
    if (network_label_ != nullptr) {
        lv_obj_set_style_text_color(network_label_, lv_color_hex(0x111827), 0);
    }
    if (status_label_ != nullptr) {
        lv_obj_set_style_text_color(status_label_, lv_color_hex(0x111827), 0);
    }
    if (notification_label_ != nullptr) {
        lv_obj_set_style_text_color(notification_label_, lv_color_hex(0x111827), 0);
    }
    if (mute_label_ != nullptr) {
        lv_obj_set_style_text_color(mute_label_, lv_color_hex(0x111827), 0);
    }
    if (battery_label_ != nullptr) {
        lv_obj_set_style_text_color(battery_label_, lv_color_hex(0x111827), 0);
    }

    const int left_margin = 12;
    const int right_margin = 8;
    const int left_area_w = std::max(108, static_cast<int>(panel_w) * 36 / 100);
    int ring_size = std::min(static_cast<int>(panel_h) - 20, static_cast<int>(panel_w) - left_area_w - 14);
    ring_size = std::clamp(ring_size, 150, 190);
    const int ring_x = std::max(left_margin + left_area_w, static_cast<int>(panel_w) - right_margin - ring_size);
    const int ring_y = std::max(10, (static_cast<int>(panel_h) - ring_size) / 2 - 4);
    const int inner_size = std::max(104, ring_size - 52);
    const int left_content_w = std::max(104, ring_x - left_margin - 1);

    lv_obj_set_width(alarm_now_time_label_, left_content_w);
    lv_obj_set_pos(alarm_now_time_label_, left_margin, 48);
    lv_obj_set_style_transform_scale(alarm_now_time_label_, left_content_w > 124 ? 470 : 420, 0);
    lv_obj_set_style_transform_pivot_x(alarm_now_time_label_, 0, 0);
    lv_obj_set_style_transform_pivot_y(alarm_now_time_label_, 0, 0);
    lv_label_set_text(alarm_now_time_label_, now_text.c_str());

    lv_obj_clear_flag(alarm_subtitle_label_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_width(alarm_subtitle_label_, left_content_w);
    lv_obj_set_pos(alarm_subtitle_label_, left_margin, 102);
    lv_obj_set_style_transform_scale(alarm_subtitle_label_, 185, 0);
    lv_obj_set_style_transform_pivot_x(alarm_subtitle_label_, 0, 0);
    lv_obj_set_style_transform_pivot_y(alarm_subtitle_label_, 0, 0);
    lv_label_set_text(alarm_subtitle_label_, weekday_text.c_str());

    lv_obj_set_width(alarm_date_label_, left_content_w + 8);
    lv_obj_set_pos(alarm_date_label_, left_margin, 120);
    lv_obj_set_style_transform_scale(alarm_date_label_, 210, 0);
    lv_obj_set_style_transform_pivot_x(alarm_date_label_, 0, 0);
    lv_obj_set_style_transform_pivot_y(alarm_date_label_, 0, 0);
    lv_label_set_text(alarm_date_label_, date_text.c_str());

    lv_obj_set_pos(alarm_next_icon_label_, left_margin, panel_h - 38);
    lv_obj_set_pos(alarm_next_time_label_, left_margin + 18, panel_h - 40);
    lv_obj_set_width(alarm_next_time_label_, std::max(72, left_content_w - 22));
    lv_label_set_text(alarm_next_icon_label_, FONT_AWESOME_BELL);
    lv_label_set_text(alarm_next_time_label_, "暂无闹钟");

    lv_obj_add_flag(alarm_hint_label_, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(alarm_hint_label_, "");

    lv_obj_set_size(alarm_ring_frame_, ring_size, ring_size);
    lv_obj_set_pos(alarm_ring_frame_, ring_x, ring_y);
    lv_obj_set_size(alarm_ring_inner_, inner_size, inner_size);
    lv_obj_center(alarm_ring_inner_);

    lv_obj_add_flag(alarm_clock_time_label_, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(alarm_clock_time_label_, "");

    lv_obj_set_style_transform_scale(alarm_countdown_label_, inner_size > 116 ? 360 : 330, 0);
    lv_obj_set_style_transform_pivot_x(alarm_countdown_label_, 0, 0);
    lv_obj_set_style_transform_pivot_y(alarm_countdown_label_, 0, 0);
    lv_obj_set_style_text_letter_space(alarm_countdown_label_, 1, 0);
    lv_obj_align(alarm_countdown_label_, LV_ALIGN_CENTER, 0, -2);
    lv_label_set_text(alarm_countdown_label_, countdown_text.c_str());

    lv_obj_set_style_transform_scale(alarm_hourglass_label_, inner_size > 116 ? 180 : 165, 0);
    lv_obj_set_style_transform_pivot_x(alarm_hourglass_label_, 0, 0);
    lv_obj_set_style_transform_pivot_y(alarm_hourglass_label_, 0, 0);
    lv_obj_align(alarm_hourglass_label_, LV_ALIGN_CENTER, 0, 34);
    lv_label_set_text(alarm_hourglass_label_, FONT_AWESOME_ALARM_CLOCK);

    int countdown_remaining_seconds = -1;
    const bool countdown_parseable = TryParseAlarmCountdownSeconds(countdown_text, countdown_remaining_seconds);
    const bool countdown_started = countdown_text != kAlarmCountdownPlaceholder;
    if (!countdown_started) {
        alarm_countdown_active_ = false;
        alarm_countdown_initial_seconds_ = -1;
        alarm_countdown_last_remaining_seconds_ = -1;
    } else {
        if (!alarm_countdown_active_ || alarm_countdown_last_remaining_seconds_ < 0 ||
            (countdown_parseable && countdown_remaining_seconds > alarm_countdown_last_remaining_seconds_)) {
            alarm_countdown_active_ = true;
            alarm_countdown_initial_seconds_ = countdown_parseable ? countdown_remaining_seconds : 1;
        }
        alarm_countdown_last_remaining_seconds_ = countdown_parseable ? countdown_remaining_seconds : -1;
    }

    int highlighted_segment_count = 0;
    if (countdown_started) {
        if (!countdown_parseable || alarm_countdown_initial_seconds_ <= 0) {
            highlighted_segment_count = kAlarmRingSegmentCount;
        } else {
            highlighted_segment_count = static_cast<int>(std::ceil(
                static_cast<double>(countdown_remaining_seconds) * kAlarmRingSegmentCount /
                static_cast<double>(alarm_countdown_initial_seconds_)));
            highlighted_segment_count = std::clamp(highlighted_segment_count, 0, kAlarmRingSegmentCount);
        }
    }

    constexpr double kPi = 3.14159265358979323846;
    const int segment_height = std::max(8, ring_size / 14);
    const int segment_width = std::max(14, ring_size / 9);
    const double radius_x = ring_size / 2.0 - segment_height / 2.0 - 2.0;
    const double radius_y = ring_size / 2.0 - segment_width / 2.0 - 2.0;
    const double center = ring_size / 2.0;
    const double start_angle = -140.0;
    const double step = 360.0 / static_cast<double>(kAlarmRingSegmentCount);

    for (int i = 0; i < kAlarmRingSegmentCount; ++i) {
        const double angle_deg = start_angle + i * step;
        const double angle_rad = angle_deg * kPi / 180.0;
        const int x = static_cast<int>(std::lround(center + std::cos(angle_rad) * radius_x - segment_width / 2.0));
        const int y = static_cast<int>(std::lround(center + std::sin(angle_rad) * radius_y - segment_height / 2.0));
        const int rotation = static_cast<int>(std::lround((angle_deg + 90.0) * 10.0));
        const bool active = i < highlighted_segment_count;
        const lv_color_t color = active ? GetAlarmRingActiveColor(i) : lv_color_hex(0xD1D5DB);

        lv_obj_set_size(alarm_ring_segments_[i], segment_width, segment_height);
        lv_obj_set_pos(alarm_ring_segments_[i], x, y);
        lv_obj_set_style_transform_pivot_x(alarm_ring_segments_[i], segment_width / 2, 0);
        lv_obj_set_style_transform_pivot_y(alarm_ring_segments_[i], segment_height / 2, 0);
        lv_obj_set_style_transform_rotation(alarm_ring_segments_[i], rotation, 0);
        lv_obj_set_style_bg_color(alarm_ring_segments_[i], color, 0);
        lv_obj_set_style_bg_opa(alarm_ring_segments_[i], active ? LV_OPA_COVER : LV_OPA_80, 0);
    }
}

void UiLcdDisplay::ShowAlarmPanel() {
    if (alarm_panel_ != nullptr && !lv_obj_is_valid(alarm_panel_)) {
        ResetAlarmPanelObjects();
    }

    if (alarm_panel_ == nullptr) {
        InitializeAlarmPanel();
    }
    lv_obj_t* alarm_panel = TakeValidObject(alarm_panel_);
    if (alarm_panel == nullptr) {
        ResetAlarmPanelObjects();
        return;
    }

    {
        DisplayLockGuard lock(this);
        if (feature_background_image_ != nullptr) {
            lv_obj_add_flag(feature_background_image_, LV_OBJ_FLAG_HIDDEN);
        }
        if (preview_image_ != nullptr) {
            lv_obj_clear_flag(preview_image_, LV_OBJ_FLAG_IGNORE_LAYOUT);
            lv_obj_add_flag(preview_image_, LV_OBJ_FLAG_HIDDEN);
        }
        if (emoji_box_ != nullptr) {
            lv_obj_add_flag(emoji_box_, LV_OBJ_FLAG_HIDDEN);
        }
        lv_obj_t* chat_label = TakeValidObject(chat_message_label_);
        if (chat_label != nullptr) {
            lv_label_set_text(chat_label, "");
            lv_obj_add_flag(chat_label, LV_OBJ_FLAG_HIDDEN);
        }
        lv_obj_clear_flag(alarm_panel, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(alarm_panel);
        if (ai_back_button_ != nullptr && lv_obj_is_valid(ai_back_button_)) {
            lv_obj_move_foreground(ai_back_button_);
        }
    }
    StartAlarmUiRefresh();
}

void UiLcdDisplay::HideAlarmPanel() {
    StopAlarmUiRefresh();
    lv_obj_t* alarm_panel = TakeValidObject(alarm_panel_);
    if (alarm_panel == nullptr) {
        ResetAlarmPanelObjects();
        return;
    }

    DisplayLockGuard lock(this);
    lv_obj_add_flag(alarm_panel, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t* chat_label = TakeValidObject(chat_message_label_);
    if (chat_label != nullptr) {
        lv_obj_clear_flag(chat_label, LV_OBJ_FLAG_HIDDEN);
    }
}
