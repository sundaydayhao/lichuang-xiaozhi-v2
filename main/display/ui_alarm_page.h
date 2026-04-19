#ifndef UI_ALARM_PAGE_H
#define UI_ALARM_PAGE_H

#include <array>

struct UiAlarmPanelPage {
    lv_obj_t* alarm_panel_ = nullptr;
    lv_obj_t* alarm_subtitle_label_ = nullptr;
    lv_obj_t* alarm_now_time_label_ = nullptr;
    lv_obj_t* alarm_date_label_ = nullptr;
    lv_obj_t* alarm_hint_label_ = nullptr;
    lv_obj_t* alarm_next_icon_label_ = nullptr;
    lv_obj_t* alarm_next_time_label_ = nullptr;
    lv_obj_t* alarm_ring_frame_ = nullptr;
    lv_obj_t* alarm_ring_inner_ = nullptr;
    lv_obj_t* alarm_clock_time_label_ = nullptr;
    lv_obj_t* alarm_countdown_label_ = nullptr;
    lv_obj_t* alarm_hourglass_label_ = nullptr;
    std::array<lv_obj_t*, 24> alarm_ring_segments_ = {};
    bool alarm_countdown_active_ = false;
    int alarm_countdown_initial_seconds_ = -1;
    int alarm_countdown_last_remaining_seconds_ = -1;
};

#endif // UI_ALARM_PAGE_H
