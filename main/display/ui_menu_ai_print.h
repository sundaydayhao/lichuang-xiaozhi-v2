#ifndef UI_MENU_AI_PRINT_H
#define UI_MENU_AI_PRINT_H

#include <lvgl.h>

#include <atomic>
#include <memory>
#include <string>

class UiLcdDisplay;
class LvglImage;

inline constexpr int kUiMenuAiPrintOptionId = 7;

struct UiMenuAiPrintState {
    lv_obj_t* message_label = nullptr;
    std::atomic<bool> generation_running{false};
};

class UiMenuAiPrint {
public:
    static bool IsOptionId(int option_id);
    static bool IsActive(const UiLcdDisplay& display);
    static const char* GetMenuText();
    static const char* GetMenuIconAssetName();
    static bool HandleSpeechRecognized(UiLcdDisplay& display, const std::string& text);
    static void ShowListeningState(UiLcdDisplay& display);
    static void ShowGeneratingState(UiLcdDisplay& display);
    static void ShowErrorState(UiLcdDisplay& display, const std::string& message);
    static void ShowResultImage(UiLcdDisplay& display, std::unique_ptr<LvglImage> image);
    static void HideMessageLabel(UiLcdDisplay& display);

private:
    static lv_obj_t* EnsureMessageLabel(UiLcdDisplay& display);
    static void SetMessageLabel(UiLcdDisplay& display, const char* text);
    static std::string BuildPrompt(const std::string& text);
    static void StartGeneration(UiLcdDisplay& display, const std::string& prompt);
    static void RunTask(UiLcdDisplay& display, const std::string& prompt);
};

#endif // UI_MENU_AI_PRINT_H
