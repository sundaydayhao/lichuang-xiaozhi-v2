#ifndef UI_MENU_REMOTE_SELFIE_H
#define UI_MENU_REMOTE_SELFIE_H

#include "ui_menu_setting.h"

#include <lvgl.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <string>

class UiLcdDisplay;

inline constexpr int kUiMenuRemoteSelfieOptionId = 5;
inline constexpr size_t kUiMenuRemoteSelfieDirectionButtonCount = 4;
inline constexpr size_t kUiMenuRemoteSelfieControlCount = 6;

enum class UiMenuRemoteSelfieAction {
    kSwipeUp = 0,
    kSwipeDown,
    kSwipeLeft,
    kSwipeRight,
    kFocus,
    kCapture,
};

struct UiMenuRemoteSelfieControlData {
    UiLcdDisplay* display = nullptr;
    UiMenuRemoteSelfieAction action = UiMenuRemoteSelfieAction::kSwipeUp;
};

struct UiMenuRemoteSelfieState {
    lv_obj_t* screen = nullptr;
    lv_obj_t* card = nullptr;
    lv_obj_t* focus_button = nullptr;
    lv_obj_t* shutter_button = nullptr;
    lv_obj_t* shutter_body = nullptr;
    lv_obj_t* shutter_top = nullptr;
    lv_obj_t* shutter_lens_ring = nullptr;
    lv_obj_t* shutter_flash_dot = nullptr;
    std::array<lv_obj_t*, kUiMenuRemoteSelfieDirectionButtonCount> direction_buttons = {};
    std::array<UiMenuRemoteSelfieControlData, kUiMenuRemoteSelfieControlCount> control_data = {};
    lv_obj_t* ble_status_label = nullptr;
    UiBackButtonData back_button_data = {};
    std::atomic<bool> active{false};
    bool audio_suspended = false;
    bool ble_connected = false;
};

class UiMenuRemoteSelfie {
public:
    static bool IsOptionId(int option_id);
    static const char* GetMenuText();
    static const char* GetMenuIconAssetName();
    static bool IsActive(const UiLcdDisplay& display);
    static bool IsScreenActive(const UiLcdDisplay& display);
    static void ShowScreen(UiLcdDisplay& display);
    static void Deactivate(UiLcdDisplay& display);
    static bool HandleSpeechRecognized(UiLcdDisplay& display, const std::string& text);
    static bool ExecuteAction(UiLcdDisplay& display, UiMenuRemoteSelfieAction action);

private:
    static void InitializeScreen(UiLcdDisplay& display);
    static bool ExecuteSpeechAction(UiLcdDisplay& display,
                                    UiMenuRemoteSelfieAction action,
                                    const char* success_notification);
    static void UpdateControlStyles(UiLcdDisplay& display);
    static void ExecuteCameraAction(UiLcdDisplay& display);
    static void SuspendAudio(UiLcdDisplay& display);
    static void ResumeAudio(UiLcdDisplay& display);
    static void RequestBleStatusRefresh(UiLcdDisplay& display);
    static void UpdateBleStatus(UiLcdDisplay& display, bool connected);
    static void OnBleStatusAsync(void* user_data);
    static void OnControlButtonClicked(lv_event_t* e);
    static void OnDirectionButtonDraw(lv_event_t* e);
    static void OnActionButtonClicked(lv_event_t* e);
};

#endif // UI_MENU_REMOTE_SELFIE_H
