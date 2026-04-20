#ifndef UI_MENU_AI_PRINT_H
#define UI_MENU_AI_PRINT_H

#include <lvgl.h>

#include <string>

class UiLcdDisplay;

class UiPrintPanel {
public:
    static bool HandleSpeechRecognized(UiLcdDisplay& display, const std::string& text);
    static lv_obj_t* EnsureMessageLabel(UiLcdDisplay& display);
    static void SetMessageLabel(UiLcdDisplay& display, const char* text);

private:
    static std::string BuildPrompt(const std::string& text);
    static void StartGeneration(UiLcdDisplay& display, const std::string& prompt);
    static void RunTask(UiLcdDisplay& display, const std::string& prompt);
};

#endif // UI_MENU_AI_PRINT_H
