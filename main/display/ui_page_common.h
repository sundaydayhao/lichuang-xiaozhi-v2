#ifndef UI_PAGE_COMMON_H
#define UI_PAGE_COMMON_H

class UiLcdDisplay;

struct BackButtonData {
    UiLcdDisplay* display = nullptr;
    int target = 0;
};

#endif // UI_PAGE_COMMON_H
