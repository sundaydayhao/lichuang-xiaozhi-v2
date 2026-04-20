#ifndef UI_MENU_SETTING_H
#define UI_MENU_SETTING_H

#include <lvgl.h>

#include <array>
#include <cstddef>
#include <string>

class UiLcdDisplay;

struct UiBackButtonData {
    UiLcdDisplay* display = nullptr;
    int target = 0;
};

inline constexpr size_t kUiMenuSettingItemCount = 4;
inline constexpr size_t kUiMenuSettingNetworkOptionCount = 2;
inline constexpr size_t kUiMenuSettingServerOptionCount = 2;
inline constexpr size_t kUiMenuSettingLanguageOptionCount = 3;
inline constexpr size_t kUiMenuSettingAboutInfoItemCount = 7;

struct UiMenuSettingItemData {
    UiLcdDisplay* display = nullptr;
    int item_id = 0;
};

struct UiMenuSettingNetworkOptionData {
    UiLcdDisplay* display = nullptr;
    int option_id = 0;
};

struct UiMenuSettingServerOptionData {
    UiLcdDisplay* display = nullptr;
    int option_id = 0;
};

struct UiMenuSettingLanguageOptionData {
    UiLcdDisplay* display = nullptr;
    int option_id = 0;
};

struct UiMenuSettingState {
    lv_obj_t* settings_screen = nullptr;
    lv_obj_t* settings_title_label = nullptr;
    std::array<lv_obj_t*, kUiMenuSettingItemCount> settings_item_name_labels = {};
    std::array<UiMenuSettingItemData, kUiMenuSettingItemCount> settings_item_data = {};
    UiBackButtonData settings_back_button_data = {};

    lv_obj_t* network_switch_screen = nullptr;
    lv_obj_t* network_switch_title_label = nullptr;
    lv_obj_t* network_switch_hint_label = nullptr;
    std::array<lv_obj_t*, kUiMenuSettingNetworkOptionCount> network_switch_option_buttons = {};
    std::array<lv_obj_t*, kUiMenuSettingNetworkOptionCount> network_switch_option_labels = {};
    std::array<lv_obj_t*, kUiMenuSettingNetworkOptionCount> network_switch_option_state_labels = {};
    std::array<UiMenuSettingNetworkOptionData, kUiMenuSettingNetworkOptionCount>
        network_switch_option_data = {};
    UiBackButtonData network_switch_back_button_data = {};

    lv_obj_t* server_switch_screen = nullptr;
    lv_obj_t* server_switch_title_label = nullptr;
    std::array<lv_obj_t*, kUiMenuSettingServerOptionCount> server_switch_option_buttons = {};
    std::array<lv_obj_t*, kUiMenuSettingServerOptionCount> server_switch_option_labels = {};
    std::array<lv_obj_t*, kUiMenuSettingServerOptionCount> server_switch_option_state_labels = {};
    std::array<UiMenuSettingServerOptionData, kUiMenuSettingServerOptionCount>
        server_switch_option_data = {};
    UiBackButtonData server_switch_back_button_data = {};

    lv_obj_t* language_switch_screen = nullptr;
    lv_obj_t* language_switch_title_label = nullptr;
    std::array<lv_obj_t*, kUiMenuSettingLanguageOptionCount> language_switch_option_buttons = {};
    std::array<lv_obj_t*, kUiMenuSettingLanguageOptionCount> language_switch_option_labels = {};
    std::array<lv_obj_t*, kUiMenuSettingLanguageOptionCount> language_switch_option_state_labels = {};
    std::array<UiMenuSettingLanguageOptionData, kUiMenuSettingLanguageOptionCount>
        language_switch_option_data = {};
    UiBackButtonData language_switch_back_button_data = {};

    lv_obj_t* about_screen = nullptr;
    lv_obj_t* about_title_label = nullptr;
    lv_obj_t* about_content = nullptr;
    std::array<lv_obj_t*, kUiMenuSettingAboutInfoItemCount> about_info_title_labels = {};
    std::array<lv_obj_t*, kUiMenuSettingAboutInfoItemCount> about_info_value_labels = {};
    std::array<lv_obj_t*, kUiMenuSettingAboutInfoItemCount> about_info_separator_labels = {};
    UiBackButtonData about_back_button_data = {};
};

class UiMenuSetting {
public:
    static std::string GetSelectedLanguageCode();
    static void ShowSettingsScreen(UiLcdDisplay& display);
    static void ShowNetworkSwitchScreen(UiLcdDisplay& display);
    static void ShowServerSwitchScreen(UiLcdDisplay& display);
    static void ShowLanguageSwitchScreen(UiLcdDisplay& display);
    static void ShowAboutScreen(UiLcdDisplay& display);

private:
    static const char* GetSettingsTitle();
    static const char* GetSettingsItemName(int index);
    static const char* GetNetworkSwitchOptionName(int index);
    static const char* GetServerSwitchOptionName(int index);
    static const char* GetLanguageSwitchOptionName(int index);
    static const char* GetAboutTitle();
    static const char* GetAboutInfoTitle(int index);
    static int GetCurrentNetworkSwitchOptionIndex();
    static int GetCurrentServerSwitchOptionIndex();
    static int GetCurrentLanguageSwitchOptionIndex();
    static std::string GetFirmwareVersionForDisplay();
    static std::string GetActivationCodeForDisplay();
    static std::string GetServerAddressForDisplay();
    static std::string GetWifiMacAddressForDisplay();
    static std::string Get4gImeiForDisplay();
    static std::string GetIpAddressForDisplay();
    static std::string GetUuidForDisplay();
    static void InitializeSettingsScreen(UiLcdDisplay& display);
    static void InitializeNetworkSwitchScreen(UiLcdDisplay& display);
    static void InitializeServerSwitchScreen(UiLcdDisplay& display);
    static void InitializeLanguageSwitchScreen(UiLcdDisplay& display);
    static void InitializeAboutScreen(UiLcdDisplay& display);
    static void RefreshNetworkSwitchScreen(UiLcdDisplay& display);
    static void SelectNetworkSwitchOption(UiLcdDisplay& display, int index);
    static void RefreshServerSwitchScreen(UiLcdDisplay& display);
    static void SelectServerSwitchOption(UiLcdDisplay& display, int index);
    static void RefreshLanguageSwitchScreen(UiLcdDisplay& display);
    static void SelectLanguageSwitchOption(UiLcdDisplay& display, int index);
    static void RefreshAboutScreenTexts(UiLcdDisplay& display);
    static void OnSettingsItemClicked(lv_event_t* e);
    static void OnNetworkSwitchOptionClicked(lv_event_t* e);
    static void OnServerSwitchOptionClicked(lv_event_t* e);
    static void OnLanguageSwitchOptionClicked(lv_event_t* e);
};

#endif // UI_MENU_SETTING_H
