#ifndef AI_PRINT_FEATURE_H
#define AI_PRINT_FEATURE_H

#include <string>

class UiLcdDisplay;

class AiPrintFeature {
public:
    static bool HandleSpeechRecognized(UiLcdDisplay& display, const std::string& text);

private:
    static std::string BuildPrompt(const std::string& text);
    static void StartGeneration(UiLcdDisplay& display, const std::string& prompt);
    static void RunTask(UiLcdDisplay& display, const std::string& prompt);
};

#endif // AI_PRINT_FEATURE_H
