#ifndef UI_PRINT_PANEL_H
#define UI_PRINT_PANEL_H

#include <string>

class UiLcdDisplay;

class UiPrintPanel {
public:
    static bool HandleSpeechRecognized(UiLcdDisplay& display, const std::string& text);

private:
    static std::string BuildPrompt(const std::string& text);
    static void StartGeneration(UiLcdDisplay& display, const std::string& prompt);
    static void RunTask(UiLcdDisplay& display, const std::string& prompt);
};

#endif // UI_PRINT_PANEL_H
