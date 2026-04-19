#ifndef UI_MENU_PAGE_H
#define UI_MENU_PAGE_H

#include "ui_page_common.h"

#include <array>

struct MenuButtonData {
    UiLcdDisplay* display = nullptr;
    int option_id = 0;
    lv_point_t press_point = {0, 0};
    bool moved = false;
};

struct UiDisplayMenuPage {
    lv_obj_t* ai_screen_ = nullptr;
    lv_obj_t* ai_back_button_ = nullptr;
    lv_obj_t* vision_ai_badge_ = nullptr;
    lv_obj_t* feature_background_image_ = nullptr;
    lv_obj_t* feature_overlay_label_ = nullptr;
    lv_obj_t* magic_mirror_intro_container_ = nullptr;
    lv_obj_t* magic_mirror_intro_emoji_ = nullptr;
    lv_obj_t* magic_mirror_intro_text_ = nullptr;
    lv_obj_t* ai_print_message_label_ = nullptr;
    lv_obj_t* menu_screen_ = nullptr;
    lv_obj_t* menu_page_container_ = nullptr;
    lv_obj_t* menu_page1_ = nullptr;
    lv_obj_t* menu_page2_ = nullptr;
    std::array<lv_obj_t*, 12> menu_buttons_ = {};
    std::array<lv_obj_t*, 12> menu_button_icon_images_ = {};
    std::array<lv_obj_t*, 12> menu_button_labels_ = {};
    std::array<MenuButtonData, 12> menu_button_data_ = {};
    BackButtonData ai_back_button_data_ = {};
};

#endif // UI_MENU_PAGE_H
