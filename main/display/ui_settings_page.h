#ifndef UI_SETTINGS_PAGE_H
#define UI_SETTINGS_PAGE_H

#include "ui_page_common.h"

#include <array>

struct SettingsItemData {
    UiLcdDisplay* display = nullptr;
    int item_id = 0;
};

struct NetworkSwitchOptionData {
    UiLcdDisplay* display = nullptr;
    int option_id = 0;
};

struct LanguageSwitchOptionData {
    UiLcdDisplay* display = nullptr;
    int option_id = 0;
};

struct ServerSwitchOptionData {
    UiLcdDisplay* display = nullptr;
    int option_id = 0;
};

struct UiDisplaySettingsPage {
    lv_obj_t* settings_screen_ = nullptr;
    lv_obj_t* settings_title_label_ = nullptr;
    std::array<lv_obj_t*, 4> settings_item_name_labels_ = {};
    std::array<SettingsItemData, 4> settings_item_data_ = {};
    BackButtonData settings_back_button_data_ = {};
};

struct UiDisplayNetworkSwitchPage {
    lv_obj_t* network_switch_screen_ = nullptr;
    lv_obj_t* network_switch_title_label_ = nullptr;
    lv_obj_t* network_switch_hint_label_ = nullptr;
    std::array<lv_obj_t*, 2> network_switch_option_buttons_ = {};
    std::array<lv_obj_t*, 2> network_switch_option_labels_ = {};
    std::array<lv_obj_t*, 2> network_switch_option_state_labels_ = {};
    std::array<NetworkSwitchOptionData, 2> network_switch_option_data_ = {};
    BackButtonData network_switch_back_button_data_ = {};
};

struct UiDisplayLanguageSwitchPage {
    lv_obj_t* language_switch_screen_ = nullptr;
    lv_obj_t* language_switch_title_label_ = nullptr;
    std::array<lv_obj_t*, 3> language_switch_option_buttons_ = {};
    std::array<lv_obj_t*, 3> language_switch_option_labels_ = {};
    std::array<lv_obj_t*, 3> language_switch_option_state_labels_ = {};
    std::array<LanguageSwitchOptionData, 3> language_switch_option_data_ = {};
    BackButtonData language_switch_back_button_data_ = {};
};

struct UiDisplayServerSwitchPage {
    lv_obj_t* server_switch_screen_ = nullptr;
    lv_obj_t* server_switch_title_label_ = nullptr;
    std::array<lv_obj_t*, 2> server_switch_option_buttons_ = {};
    std::array<lv_obj_t*, 2> server_switch_option_labels_ = {};
    std::array<lv_obj_t*, 2> server_switch_option_state_labels_ = {};
    std::array<ServerSwitchOptionData, 2> server_switch_option_data_ = {};
    BackButtonData server_switch_back_button_data_ = {};
};

struct UiDisplayAboutPage {
    lv_obj_t* about_screen_ = nullptr;
    lv_obj_t* about_title_label_ = nullptr;
    lv_obj_t* about_content_ = nullptr;
    std::array<lv_obj_t*, 7> about_info_title_labels_ = {};
    std::array<lv_obj_t*, 7> about_info_value_labels_ = {};
    std::array<lv_obj_t*, 7> about_info_separator_labels_ = {};
    BackButtonData about_back_button_data_ = {};
};

#endif // UI_SETTINGS_PAGE_H
