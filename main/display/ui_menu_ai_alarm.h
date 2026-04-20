#ifndef UI_MENU_AI_ALARM_H
#define UI_MENU_AI_ALARM_H

#include <array>
#include <cstddef>
#include <string>

#include <esp_timer.h>
#include <lvgl.h>

class UiLcdDisplay;

inline constexpr size_t kUiMenuAiAlarmRingSegmentCount = 24;

struct UiMenuAiAlarmState {
    esp_timer_handle_t ui_timer = nullptr;
    lv_obj_t* panel = nullptr;
    lv_obj_t* subtitle_label = nullptr;
    lv_obj_t* now_time_label = nullptr;
    lv_obj_t* date_label = nullptr;
    lv_obj_t* hint_label = nullptr;
    lv_obj_t* next_icon_label = nullptr;
    lv_obj_t* next_time_label = nullptr;
    lv_obj_t* ring_frame = nullptr;
    lv_obj_t* ring_inner = nullptr;
    lv_obj_t* clock_time_label = nullptr;
    lv_obj_t* countdown_label = nullptr;
    lv_obj_t* hourglass_label = nullptr;
    std::array<lv_obj_t*, kUiMenuAiAlarmRingSegmentCount> ring_segments = {};
    bool countdown_active = false;
    int countdown_initial_seconds = -1;
    int countdown_last_remaining_seconds = -1;
};

class UiMenuAiAlarm {
public:
    static void InitializeSupport(UiLcdDisplay& display);
    static void DeinitializeSupport(UiLcdDisplay& display);
    static void InitializePanel(UiLcdDisplay& display);
    static void RefreshUi(UiLcdDisplay& display);
    static void ShowPanel(UiLcdDisplay& display);
    static void HidePanel(UiLcdDisplay& display);
    static bool IsPanelVisible(UiLcdDisplay& display);
    static std::string BuildScenePayload(const UiLcdDisplay& display);

private:
    static void ResetPanelObjects(UiLcdDisplay& display);
    static void StartUiRefresh(UiLcdDisplay& display);
    static void StopUiRefresh(UiLcdDisplay& display);
    static bool HasValidLocalTime();
};

#endif // UI_MENU_AI_ALARM_H
