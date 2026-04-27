#include "ui_menu_ai_alarm.h"

#include "application.h"
#include "lvgl_theme.h"
#include "ui_display.h"

#include <esp_err.h>
#include <font_awesome.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <string>

namespace {

constexpr int kStatusBarHeight = 27;
constexpr int kAlarmUiRefreshPeriodUs = 1000 * 1000;
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

std::string FormatAlarmCountdownForDisplay(int total_seconds) {
    if (total_seconds < 0) {
        return kAlarmCountdownPlaceholder;
    }

    const int hours = total_seconds / 3600;
    const int minutes = (total_seconds % 3600) / 60;
    const int seconds = total_seconds % 60;
    char buffer[16];
    if (hours > 0) {
        snprintf(buffer, sizeof(buffer), "%02d:%02d:%02d", hours, minutes, seconds);
    } else {
        snprintf(buffer, sizeof(buffer), "%02d:%02d", minutes, seconds);
    }
    return buffer;
}

std::string FormatAlarmTargetTimeForDisplay(bool has_valid_time, time_t target_time) {
    if (!has_valid_time || target_time <= 0) {
        return "--:--";
    }

    struct tm local_tm;
    localtime_r(&target_time, &local_tm);
    char buffer[16];
    strftime(buffer, sizeof(buffer), "%H:%M", &local_tm);
    return buffer;
}

std::string BuildAlarmNextTimeText(bool has_valid_time, const Application::AiAlarmSnapshot& snapshot) {
    if (snapshot.ringing) {
        return snapshot.reminder.empty() ? "提醒中" : snapshot.reminder;
    }

    if (!snapshot.active) {
        return "暂无闹钟";
    }

    std::string text = FormatAlarmTargetTimeForDisplay(has_valid_time, snapshot.target_time);
    if (!snapshot.reminder.empty()) {
        text += " ";
        text += snapshot.reminder;
    }
    return text;
}

lv_color_t GetAlarmRingActiveColor(int index) {
    if (index < static_cast<int>(kUiMenuAiAlarmRingSegmentCount) / 3) {
        return lv_color_hex(0x2D6BFF);
    }
    if (index < (static_cast<int>(kUiMenuAiAlarmRingSegmentCount) * 2) / 3) {
        return lv_color_hex(0xFFC107);
    }
    return lv_color_hex(0xFF7A59);
}

} // namespace

void UiMenuAiAlarm::ResetPanelObjects(UiLcdDisplay& display) {
    auto& state = display.ai_alarm_;
    state.panel = nullptr;
    state.subtitle_label = nullptr;
    state.now_time_label = nullptr;
    state.date_label = nullptr;
    state.hint_label = nullptr;
    state.next_icon_label = nullptr;
    state.next_time_label = nullptr;
    state.ring_frame = nullptr;
    state.ring_inner = nullptr;
    state.clock_time_label = nullptr;
    state.countdown_label = nullptr;
    state.hourglass_label = nullptr;
    state.ring_segments.fill(nullptr);
    state.countdown_active = false;
    state.countdown_initial_seconds = -1;
    state.countdown_last_remaining_seconds = -1;
}

void UiMenuAiAlarm::InitializeSupport(UiLcdDisplay& display) {
    auto& state = display.ai_alarm_;
    if (state.ui_timer != nullptr) {
        return;
    }

    ResetPanelObjects(display);

    esp_timer_create_args_t alarm_ui_timer_args = {
        .callback = [](void* arg) {
            auto* display_ptr = static_cast<UiLcdDisplay*>(arg);
            if (display_ptr != nullptr) {
                UiMenuAiAlarm::RefreshUi(*display_ptr);
            }
        },
        .arg = &display,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "alarm_ui_timer",
        .skip_unhandled_events = true,
    };
    ESP_ERROR_CHECK(esp_timer_create(&alarm_ui_timer_args, &state.ui_timer));
}

void UiMenuAiAlarm::DeinitializeSupport(UiLcdDisplay& display) {
    auto& state = display.ai_alarm_;
    StopUiRefresh(display);
    if (state.ui_timer != nullptr) {
        esp_timer_delete(state.ui_timer);
        state.ui_timer = nullptr;
    }
    ResetPanelObjects(display);
}

bool UiMenuAiAlarm::HasValidLocalTime() {
    time_t now = time(nullptr);
    struct tm local_tm;
    localtime_r(&now, &local_tm);
    return local_tm.tm_year >= (2025 - 1900);
}

void UiMenuAiAlarm::StartUiRefresh(UiLcdDisplay& display) {
    auto& state = display.ai_alarm_;
    if (state.ui_timer == nullptr) {
        return;
    }

    esp_timer_stop(state.ui_timer);
    RefreshUi(display);
    ESP_ERROR_CHECK(esp_timer_start_periodic(state.ui_timer, kAlarmUiRefreshPeriodUs));
}

void UiMenuAiAlarm::StopUiRefresh(UiLcdDisplay& display) {
    auto& state = display.ai_alarm_;
    if (state.ui_timer != nullptr) {
        esp_timer_stop(state.ui_timer);
    }
}

void UiMenuAiAlarm::InitializePanel(UiLcdDisplay& display) {
    auto& state = display.ai_alarm_;
    if (display.ai_screen_ == nullptr) {
        return;
    }

    if (state.panel != nullptr && !lv_obj_is_valid(state.panel)) {
        ResetPanelObjects(display);
    }

    if (state.panel != nullptr) {
        return;
    }

    auto* lvgl_theme = static_cast<LvglTheme*>(display.GetTheme());
    const lv_coord_t panel_top = kStatusBarHeight;

    state.panel = lv_obj_create(display.ai_screen_);
    lv_obj_set_size(state.panel, LV_HOR_RES, LV_VER_RES - panel_top);
    lv_obj_align(state.panel, LV_ALIGN_TOP_MID, 0, panel_top);
    lv_obj_set_style_bg_color(state.panel, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(state.panel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(state.panel, 0, 0);
    lv_obj_set_style_radius(state.panel, 0, 0);
    lv_obj_set_style_pad_all(state.panel, 0, 0);
    lv_obj_set_layout(state.panel, LV_LAYOUT_NONE);
    lv_obj_set_scrollbar_mode(state.panel, LV_SCROLLBAR_MODE_OFF);
    lv_obj_remove_flag(state.panel, static_cast<lv_obj_flag_t>(
                                        LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_SCROLL_ELASTIC |
                                        LV_OBJ_FLAG_SCROLL_MOMENTUM | LV_OBJ_FLAG_SCROLL_CHAIN));

    state.subtitle_label = lv_label_create(state.panel);
    lv_obj_set_width(state.subtitle_label, LV_HOR_RES - 24);
    lv_obj_align(state.subtitle_label, LV_ALIGN_TOP_LEFT, 12, 8);
    lv_obj_set_style_text_align(state.subtitle_label, LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_set_style_text_font(state.subtitle_label, lvgl_theme->text_font()->font(), 0);
    lv_obj_set_style_text_color(state.subtitle_label, lv_color_hex(0x9CA3AF), 0);
    lv_label_set_text(state.subtitle_label, "");
    lv_obj_add_flag(state.subtitle_label, LV_OBJ_FLAG_HIDDEN);

    state.now_time_label = lv_label_create(state.panel);
    lv_obj_set_style_text_font(state.now_time_label, lvgl_theme->text_font()->font(), 0);
    lv_obj_set_style_text_color(state.now_time_label, lv_color_hex(0x111827), 0);
    lv_obj_set_style_text_letter_space(state.now_time_label, 2, 0);

    state.date_label = lv_label_create(state.panel);
    lv_label_set_long_mode(state.date_label, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_font(state.date_label, lvgl_theme->text_font()->font(), 0);
    lv_obj_set_style_text_color(state.date_label, lv_color_hex(0x6B7280), 0);

    state.next_icon_label = lv_label_create(state.panel);
    lv_obj_set_style_text_font(state.next_icon_label, lvgl_theme->icon_font()->font(), 0);
    lv_obj_set_style_text_color(state.next_icon_label, lv_color_hex(0x2563EB), 0);

    state.next_time_label = lv_label_create(state.panel);
    lv_label_set_long_mode(state.next_time_label, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_font(state.next_time_label, lvgl_theme->text_font()->font(), 0);
    lv_obj_set_style_text_color(state.next_time_label, lv_color_hex(0x2563EB), 0);

    state.hint_label = lv_label_create(state.panel);
    lv_obj_set_width(state.hint_label, 108);
    lv_label_set_long_mode(state.hint_label, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_font(state.hint_label, lvgl_theme->text_font()->font(), 0);
    lv_obj_set_style_text_color(state.hint_label, lv_color_hex(0x2563EB), 0);

    state.ring_frame = lv_obj_create(state.panel);
    lv_obj_set_style_radius(state.ring_frame, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(state.ring_frame, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(state.ring_frame, 0, 0);
    lv_obj_set_style_pad_all(state.ring_frame, 0, 0);
    lv_obj_set_style_shadow_width(state.ring_frame, 0, 0);
    lv_obj_remove_flag(state.ring_frame, static_cast<lv_obj_flag_t>(
                                             LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_SCROLL_ELASTIC |
                                             LV_OBJ_FLAG_SCROLL_MOMENTUM | LV_OBJ_FLAG_SCROLL_CHAIN));

    for (size_t i = 0; i < kUiMenuAiAlarmRingSegmentCount; ++i) {
        state.ring_segments[i] = lv_obj_create(state.ring_frame);
        lv_obj_set_style_radius(state.ring_segments[i], LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_border_width(state.ring_segments[i], 0, 0);
        lv_obj_set_style_pad_all(state.ring_segments[i], 0, 0);
        lv_obj_set_style_shadow_width(state.ring_segments[i], 0, 0);
    }

    state.ring_inner = lv_obj_create(state.ring_frame);
    lv_obj_set_style_radius(state.ring_inner, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(state.ring_inner, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(state.ring_inner, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(state.ring_inner, 0, 0);
    lv_obj_set_style_pad_all(state.ring_inner, 0, 0);
    lv_obj_set_style_shadow_width(state.ring_inner, 10, 0);
    lv_obj_set_style_shadow_color(state.ring_inner, lv_color_hex(0xD1D5DB), 0);

    state.clock_time_label = lv_label_create(state.ring_inner);
    lv_obj_set_style_text_font(state.clock_time_label, lvgl_theme->text_font()->font(), 0);
    lv_obj_set_style_text_color(state.clock_time_label, lv_color_hex(0x6B7280), 0);

    state.countdown_label = lv_label_create(state.ring_inner);
    lv_obj_set_style_text_font(state.countdown_label, lvgl_theme->text_font()->font(), 0);
    lv_obj_set_style_text_color(state.countdown_label, lv_color_hex(0x111827), 0);

    state.hourglass_label = lv_label_create(state.ring_inner);
    lv_obj_set_style_text_font(state.hourglass_label, lvgl_theme->icon_font()->font(), 0);
    lv_obj_set_style_text_color(state.hourglass_label, lv_color_hex(0xD97706), 0);

    lv_obj_add_flag(state.panel, LV_OBJ_FLAG_HIDDEN);
}

void UiMenuAiAlarm::RefreshUi(UiLcdDisplay& display) {
    auto& state = display.ai_alarm_;
    lv_obj_t* panel = TakeValidObject(state.panel);
    if (panel == nullptr) {
        ResetPanelObjects(display);
        return;
    }

    if (lv_obj_has_flag(panel, LV_OBJ_FLAG_HIDDEN)) {
        return;
    }

    DisplayLockGuard lock(&display);
    const lv_coord_t panel_w = lv_obj_get_width(panel);
    const lv_coord_t panel_h = lv_obj_get_height(panel);
    if (panel_w <= 0 || panel_h <= 0) {
        return;
    }

    const bool has_valid_time = HasValidLocalTime();
    const time_t now = time(nullptr);
    const auto alarm_snapshot = Application::GetInstance().GetAiAlarmSnapshot();
    const std::string now_text = FormatCurrentTimeForDisplay(has_valid_time, now);
    const std::string date_text = FormatCurrentDateForDisplay(has_valid_time, now);
    const std::string weekday_text = FormatCurrentWeekdayForDisplay(has_valid_time, now);
    const bool has_alarm = alarm_snapshot.active || alarm_snapshot.ringing;
    const std::string countdown_text =
        alarm_snapshot.ringing ? "00:00"
                               : (alarm_snapshot.active
                                      ? FormatAlarmCountdownForDisplay(alarm_snapshot.remaining_seconds)
                                      : kAlarmCountdownPlaceholder);
    const std::string next_time_text = BuildAlarmNextTimeText(has_valid_time, alarm_snapshot);
    const std::string clock_time_text =
        alarm_snapshot.ringing ? "提醒中"
                               : (alarm_snapshot.active
                                      ? FormatAlarmTargetTimeForDisplay(has_valid_time, alarm_snapshot.target_time)
                                      : "");

    lv_obj_set_style_bg_color(display.ai_screen_, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(display.ai_screen_, LV_OPA_COVER, 0);
    if (display.status_bar_ != nullptr) {
        lv_obj_set_style_bg_color(display.status_bar_, lv_color_white(), 0);
        lv_obj_set_style_bg_opa(display.status_bar_, LV_OPA_COVER, 0);
        lv_obj_set_style_text_color(display.status_bar_, lv_color_hex(0x111827), 0);
    }
    if (display.network_label_ != nullptr) {
        lv_obj_set_style_text_color(display.network_label_, lv_color_hex(0x111827), 0);
    }
    if (display.status_label_ != nullptr) {
        lv_obj_set_style_text_color(display.status_label_, lv_color_hex(0x111827), 0);
    }
    if (display.notification_label_ != nullptr) {
        lv_obj_set_style_text_color(display.notification_label_, lv_color_hex(0x111827), 0);
    }
    if (display.mute_label_ != nullptr) {
        lv_obj_set_style_text_color(display.mute_label_, lv_color_hex(0x111827), 0);
    }
    if (display.battery_label_ != nullptr) {
        lv_obj_set_style_text_color(display.battery_label_, lv_color_hex(0x111827), 0);
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

    lv_obj_set_width(state.now_time_label, left_content_w);
    lv_obj_set_pos(state.now_time_label, left_margin, 48);
    lv_obj_set_style_transform_scale(state.now_time_label, left_content_w > 124 ? 470 : 420, 0);
    lv_obj_set_style_transform_pivot_x(state.now_time_label, 0, 0);
    lv_obj_set_style_transform_pivot_y(state.now_time_label, 0, 0);
    lv_label_set_text(state.now_time_label, now_text.c_str());

    lv_obj_clear_flag(state.subtitle_label, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_width(state.subtitle_label, left_content_w);
    lv_obj_set_pos(state.subtitle_label, left_margin, 102);
    lv_obj_set_style_transform_scale(state.subtitle_label, 185, 0);
    lv_obj_set_style_transform_pivot_x(state.subtitle_label, 0, 0);
    lv_obj_set_style_transform_pivot_y(state.subtitle_label, 0, 0);
    lv_label_set_text(state.subtitle_label, weekday_text.c_str());

    lv_obj_set_width(state.date_label, left_content_w + 8);
    lv_obj_set_pos(state.date_label, left_margin, 120);
    lv_obj_set_style_transform_scale(state.date_label, 210, 0);
    lv_obj_set_style_transform_pivot_x(state.date_label, 0, 0);
    lv_obj_set_style_transform_pivot_y(state.date_label, 0, 0);
    lv_label_set_text(state.date_label, date_text.c_str());

    lv_obj_set_pos(state.next_icon_label, left_margin, panel_h - 38);
    lv_obj_set_pos(state.next_time_label, left_margin + 18, panel_h - 40);
    lv_obj_set_width(state.next_time_label, std::max(72, left_content_w - 22));
    lv_label_set_text(state.next_icon_label, FONT_AWESOME_BELL);
    lv_label_set_text(state.next_time_label, next_time_text.c_str());

    lv_label_set_text(state.hint_label, "");
    lv_obj_add_flag(state.hint_label, LV_OBJ_FLAG_HIDDEN);

    lv_obj_set_size(state.ring_frame, ring_size, ring_size);
    lv_obj_set_pos(state.ring_frame, ring_x, ring_y);
    lv_obj_set_size(state.ring_inner, inner_size, inner_size);
    lv_obj_center(state.ring_inner);

    if (has_alarm) {
        lv_obj_clear_flag(state.clock_time_label, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_transform_scale(state.clock_time_label, 205, 0);
        lv_obj_set_style_transform_pivot_x(state.clock_time_label, 0, 0);
        lv_obj_set_style_transform_pivot_y(state.clock_time_label, 0, 0);
        lv_obj_align(state.clock_time_label, LV_ALIGN_CENTER, 0, -30);
        lv_label_set_text(state.clock_time_label, clock_time_text.c_str());
    } else {
        lv_obj_add_flag(state.clock_time_label, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(state.clock_time_label, "");
    }

    const int countdown_scale =
        countdown_text.size() > 5 ? (inner_size > 116 ? 250 : 230) : (inner_size > 116 ? 360 : 330);
    lv_obj_set_style_transform_scale(state.countdown_label, countdown_scale, 0);
    lv_obj_set_style_transform_pivot_x(state.countdown_label, 0, 0);
    lv_obj_set_style_transform_pivot_y(state.countdown_label, 0, 0);
    lv_obj_set_style_text_letter_space(state.countdown_label, 1, 0);
    lv_obj_align(state.countdown_label, LV_ALIGN_CENTER, 0, has_alarm ? 6 : -2);
    lv_label_set_text(state.countdown_label, countdown_text.c_str());

    lv_obj_set_style_transform_scale(state.hourglass_label, inner_size > 116 ? 180 : 165, 0);
    lv_obj_set_style_transform_pivot_x(state.hourglass_label, 0, 0);
    lv_obj_set_style_transform_pivot_y(state.hourglass_label, 0, 0);
    lv_obj_align(state.hourglass_label, LV_ALIGN_CENTER, 0, has_alarm ? 42 : 34);
    lv_label_set_text(state.hourglass_label, FONT_AWESOME_ALARM_CLOCK);

    if (!alarm_snapshot.active) {
        state.countdown_active = false;
        state.countdown_initial_seconds = -1;
        state.countdown_last_remaining_seconds = -1;
    } else {
        if (!state.countdown_active || state.countdown_last_remaining_seconds < 0 ||
            alarm_snapshot.remaining_seconds > state.countdown_last_remaining_seconds) {
            state.countdown_active = true;
            state.countdown_initial_seconds = std::max(1, alarm_snapshot.total_seconds);
        }
        state.countdown_last_remaining_seconds = alarm_snapshot.remaining_seconds;
    }

    int highlighted_segment_count = 0;
    if (alarm_snapshot.ringing) {
        highlighted_segment_count = static_cast<int>(kUiMenuAiAlarmRingSegmentCount);
    } else if (alarm_snapshot.active) {
        if (state.countdown_initial_seconds > 0) {
            highlighted_segment_count = static_cast<int>(std::ceil(
                static_cast<double>(alarm_snapshot.remaining_seconds) * kUiMenuAiAlarmRingSegmentCount /
                static_cast<double>(state.countdown_initial_seconds)));
            highlighted_segment_count =
                std::clamp(highlighted_segment_count, 0, static_cast<int>(kUiMenuAiAlarmRingSegmentCount));
        } else {
            highlighted_segment_count = static_cast<int>(kUiMenuAiAlarmRingSegmentCount);
        }
    }

    constexpr double kPi = 3.14159265358979323846;
    const int segment_height = std::max(8, ring_size / 14);
    const int segment_width = std::max(14, ring_size / 9);
    const double radius_x = ring_size / 2.0 - segment_height / 2.0 - 2.0;
    const double radius_y = ring_size / 2.0 - segment_width / 2.0 - 2.0;
    const double center = ring_size / 2.0;
    const double start_angle = -140.0;
    const double step = 360.0 / static_cast<double>(kUiMenuAiAlarmRingSegmentCount);

    for (size_t i = 0; i < kUiMenuAiAlarmRingSegmentCount; ++i) {
        const double angle_deg = start_angle + i * step;
        const double angle_rad = angle_deg * kPi / 180.0;
        const int x = static_cast<int>(std::lround(center + std::cos(angle_rad) * radius_x - segment_width / 2.0));
        const int y = static_cast<int>(std::lround(center + std::sin(angle_rad) * radius_y - segment_height / 2.0));
        const int rotation = static_cast<int>(std::lround((angle_deg + 90.0) * 10.0));
        const bool active = static_cast<int>(i) < highlighted_segment_count;
        const lv_color_t color = active ? GetAlarmRingActiveColor(static_cast<int>(i)) : lv_color_hex(0xD1D5DB);

        lv_obj_set_size(state.ring_segments[i], segment_width, segment_height);
        lv_obj_set_pos(state.ring_segments[i], x, y);
        lv_obj_set_style_transform_pivot_x(state.ring_segments[i], segment_width / 2, 0);
        lv_obj_set_style_transform_pivot_y(state.ring_segments[i], segment_height / 2, 0);
        lv_obj_set_style_transform_rotation(state.ring_segments[i], rotation, 0);
        lv_obj_set_style_bg_color(state.ring_segments[i], color, 0);
        lv_obj_set_style_bg_opa(state.ring_segments[i], active ? LV_OPA_COVER : LV_OPA_80, 0);
    }
}

void UiMenuAiAlarm::ShowPanel(UiLcdDisplay& display) {
    auto& state = display.ai_alarm_;
    if (state.panel != nullptr && !lv_obj_is_valid(state.panel)) {
        ResetPanelObjects(display);
    }

    if (state.panel == nullptr) {
        InitializePanel(display);
    }
    lv_obj_t* panel = TakeValidObject(state.panel);
    if (panel == nullptr) {
        ResetPanelObjects(display);
        return;
    }

    {
        DisplayLockGuard lock(&display);
        if (display.feature_background_image_ != nullptr) {
            lv_obj_add_flag(display.feature_background_image_, LV_OBJ_FLAG_HIDDEN);
        }
        if (display.preview_image_ != nullptr) {
            lv_obj_clear_flag(display.preview_image_, LV_OBJ_FLAG_IGNORE_LAYOUT);
            lv_obj_add_flag(display.preview_image_, LV_OBJ_FLAG_HIDDEN);
        }
        if (display.emoji_box_ != nullptr) {
            lv_obj_add_flag(display.emoji_box_, LV_OBJ_FLAG_HIDDEN);
        }
        lv_obj_t* chat_label = TakeValidObject(display.chat_message_label_);
        if (chat_label != nullptr) {
            lv_label_set_text(chat_label, "");
            lv_obj_add_flag(chat_label, LV_OBJ_FLAG_HIDDEN);
        }
        lv_obj_clear_flag(panel, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(panel);
        if (display.ai_back_button_ != nullptr && lv_obj_is_valid(display.ai_back_button_)) {
            lv_obj_move_foreground(display.ai_back_button_);
        }
    }
    StartUiRefresh(display);
}

void UiMenuAiAlarm::HidePanel(UiLcdDisplay& display) {
    auto& state = display.ai_alarm_;
    StopUiRefresh(display);
    lv_obj_t* panel = TakeValidObject(state.panel);
    if (panel == nullptr) {
        ResetPanelObjects(display);
        return;
    }

    DisplayLockGuard lock(&display);
    lv_obj_add_flag(panel, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t* chat_label = TakeValidObject(display.chat_message_label_);
    if (chat_label != nullptr) {
        lv_obj_clear_flag(chat_label, LV_OBJ_FLAG_HIDDEN);
    }
}

bool UiMenuAiAlarm::IsPanelVisible(UiLcdDisplay& display) {
    auto& state = display.ai_alarm_;
    if (state.panel != nullptr && !lv_obj_is_valid(state.panel)) {
        ResetPanelObjects(display);
        return false;
    }

    return state.panel != nullptr && !lv_obj_has_flag(state.panel, LV_OBJ_FLAG_HIDDEN);
}
