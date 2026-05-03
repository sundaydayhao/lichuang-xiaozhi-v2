#include "ui_menu_setting.h"

#include "application.h"
#include "board.h"
#include "lvgl_theme.h"
#include "settings.h"
#include "system_info.h"
#include "ui_display.h"
#include "assets/lang_config.h"

#include <esp_app_desc.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <wifi_station.h>

#include <array>

namespace {

constexpr int kBackTargetMainMenu = 0;
constexpr int kBackTargetSettings = 1;
constexpr const char* kCheckMarkText = "√";
constexpr const char* kBackupServerOtaUrl = "";

} // namespace

std::string UiMenuSetting::GetSelectedLanguageCode() {
    Settings settings("ui", false);
    std::string language = settings.GetString("language");
    if (language == "zh-CN" || language == "zh-TW" || language == "en-US") {
        return language;
    }
    return Lang::CODE;
}

const char* UiMenuSetting::GetSettingsTitle() {
    return "设置";
}

const char* UiMenuSetting::GetSettingsItemName(int index) {
    static const char* kTexts[kUiMenuSettingItemCount] = {
        "服务器切换",
        "语言切换",
        "关于本机",
    };
    return (index >= 0 && index < static_cast<int>(kUiMenuSettingItemCount)) ? kTexts[index] : "";
}

const char* UiMenuSetting::GetServerSwitchOptionName(int index) {
    static const char* kTexts[kUiMenuSettingServerOptionCount] = {
        "默认服务器",
        "备选服务器",
    };
    return (index >= 0 && index < static_cast<int>(kUiMenuSettingServerOptionCount)) ? kTexts[index] : "";
}

const char* UiMenuSetting::GetLanguageSwitchOptionName(int index) {
    static const char* kTexts[kUiMenuSettingLanguageOptionCount] = {
        "简体中文",
        "繁体中文",
        "英语",
    };
    return (index >= 0 && index < static_cast<int>(kUiMenuSettingLanguageOptionCount)) ? kTexts[index] : "";
}

int UiMenuSetting::GetCurrentServerSwitchOptionIndex() {
    Settings settings("server", false);
    const int current_index = settings.GetInt("type", 0);
    const std::string backup_url = settings.GetString("backup_ota_url", kBackupServerOtaUrl);
    if (current_index == 1 && !backup_url.empty()) {
        return 1;
    }
    return 0;
}

int UiMenuSetting::GetCurrentLanguageSwitchOptionIndex() {
    const std::string language = GetSelectedLanguageCode();
    if (language == "zh-TW") {
        return 1;
    }
    if (language == "en-US") {
        return 2;
    }
    return 0;
}

const char* UiMenuSetting::GetAboutTitle() {
    return "关于本机";
}

const char* UiMenuSetting::GetAboutInfoTitle(int index) {
    static const char* kTexts[kUiMenuSettingAboutInfoItemCount] = {
        "固件版本",
        "绑定验证码",
        "服务器地址",
        "WiFi MAC",
        "IP地址",
        "UUID",
    };
    return (index >= 0 && index < static_cast<int>(kUiMenuSettingAboutInfoItemCount)) ? kTexts[index] : "";
}

std::string UiMenuSetting::GetFirmwareVersionForDisplay() {
    const esp_app_desc_t* app_desc = esp_app_get_description();
    if (app_desc == nullptr || app_desc->version[0] == '\0') {
        return "unknown";
    }
    return app_desc->version;
}

std::string UiMenuSetting::GetActivationCodeForDisplay() {
    return "暂无";
}

std::string UiMenuSetting::GetServerAddressForDisplay() {
    Settings wifi_settings("wifi", false);
    std::string url = wifi_settings.GetString("ota_url");
    if (!url.empty()) {
        return url;
    }

    Settings websocket_settings("websocket", false);
    url = websocket_settings.GetString("url");
    return url.empty() ? "未配置" : url;
}

std::string UiMenuSetting::GetWifiMacAddressForDisplay() {
    std::string mac = SystemInfo::GetMacAddress();
    return mac.empty() ? "unknown" : mac;
}

std::string UiMenuSetting::GetIpAddressForDisplay() {
    const std::string& ip = WifiStation::GetInstance().GetIpAddress();
    return ip.empty() ? "未配置" : ip;
}

std::string UiMenuSetting::GetUuidForDisplay() {
    std::string uuid = Board::GetInstance().GetUuid();
    return uuid.empty() ? "unknown" : uuid;
}

void UiMenuSetting::InitializeSettingsScreen(UiLcdDisplay& display) {
    DisplayLockGuard lock(&display);
    auto& state = display.menu_setting_;

    if (state.settings_screen != nullptr) {
        return;
    }

    auto* lvgl_theme = static_cast<LvglTheme*>(display.GetTheme());

    state.settings_screen = lv_obj_create(nullptr);
    lv_obj_set_style_bg_color(state.settings_screen, lv_color_white(), 0);
    lv_obj_set_style_border_width(state.settings_screen, 0, 0);
    lv_obj_set_style_pad_all(state.settings_screen, 0, 0);
    lv_obj_set_scrollbar_mode(state.settings_screen, LV_SCROLLBAR_MODE_OFF);

    state.settings_title_label = lv_label_create(state.settings_screen);
    lv_label_set_text(state.settings_title_label, GetSettingsTitle());
    lv_obj_align(state.settings_title_label, LV_ALIGN_TOP_MID, 0, 40);
    lv_obj_set_style_text_font(state.settings_title_label, lvgl_theme->text_font()->font(), 0);
    lv_obj_set_style_text_color(state.settings_title_label, lv_color_black(), 0);

    lv_obj_t* content = lv_obj_create(state.settings_screen);
    lv_obj_set_size(content, LV_HOR_RES, LV_VER_RES - 84);
    lv_obj_align(content, LV_ALIGN_TOP_MID, 0, 84);
    lv_obj_set_style_bg_color(content, lv_color_white(), 0);
    lv_obj_set_style_border_width(content, 0, 0);
    lv_obj_set_style_pad_all(content, 0, 0);
    lv_obj_set_scroll_dir(content, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(content, LV_SCROLLBAR_MODE_OFF);

    const int item_height = 46;
    for (int i = 0; i < static_cast<int>(kUiMenuSettingItemCount); ++i) {
        lv_obj_t* item = lv_btn_create(content);
        lv_obj_set_size(item, LV_HOR_RES, item_height);
        lv_obj_set_pos(item, 0, i * item_height);
        lv_obj_set_style_radius(item, 0, 0);
        lv_obj_set_style_bg_color(item, lv_color_white(), 0);
        lv_obj_set_style_bg_opa(item, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(item, 0, 0);
        lv_obj_set_style_shadow_width(item, 0, 0);
        lv_obj_set_style_pad_left(item, 16, 0);
        lv_obj_set_style_pad_right(item, 16, 0);

        state.settings_item_name_labels[i] = lv_label_create(item);
        lv_label_set_text(state.settings_item_name_labels[i], GetSettingsItemName(i));
        lv_obj_align(state.settings_item_name_labels[i], LV_ALIGN_LEFT_MID, 0, 0);
        lv_obj_set_style_text_color(state.settings_item_name_labels[i], lv_color_black(), 0);
        lv_obj_set_style_text_font(state.settings_item_name_labels[i], lvgl_theme->text_font()->font(), 0);

        lv_obj_t* arrow_label = lv_label_create(item);
        lv_label_set_text(arrow_label, ">");
        lv_obj_align(arrow_label, LV_ALIGN_RIGHT_MID, 0, 0);
        lv_obj_set_style_text_color(arrow_label, lv_color_hex(0xB0B0B0), 0);
        lv_obj_set_style_text_font(arrow_label, lvgl_theme->text_font()->font(), 0);

        state.settings_item_data[i] = UiMenuSettingItemData{&display, i};
        lv_obj_set_user_data(item, &state.settings_item_data[i]);
        lv_obj_add_event_cb(item, OnSettingsItemClicked, LV_EVENT_CLICKED, nullptr);

        if (i < static_cast<int>(kUiMenuSettingItemCount) - 1) {
            lv_obj_t* separator = lv_obj_create(content);
            lv_obj_set_size(separator, LV_HOR_RES - 32, 1);
            lv_obj_set_pos(separator, 16, (i + 1) * item_height);
            lv_obj_set_style_bg_color(separator, lv_color_hex(0xD9D9D9), 0);
            lv_obj_set_style_bg_opa(separator, LV_OPA_COVER, 0);
            lv_obj_set_style_border_width(separator, 0, 0);
            lv_obj_set_style_radius(separator, 0, 0);
            lv_obj_clear_flag(separator, LV_OBJ_FLAG_CLICKABLE);
        }
    }

    state.settings_back_button_data = UiBackButtonData{&display, kBackTargetMainMenu};
    display.CreateBackButton(state.settings_screen, &state.settings_back_button_data);
}

void UiMenuSetting::InitializeServerSwitchScreen(UiLcdDisplay& display) {
    DisplayLockGuard lock(&display);
    auto& state = display.menu_setting_;

    if (state.server_switch_screen != nullptr) {
        return;
    }

    auto* lvgl_theme = static_cast<LvglTheme*>(display.GetTheme());

    state.server_switch_screen = lv_obj_create(nullptr);
    lv_obj_set_style_bg_color(state.server_switch_screen, lv_color_white(), 0);
    lv_obj_set_style_border_width(state.server_switch_screen, 0, 0);
    lv_obj_set_style_pad_all(state.server_switch_screen, 0, 0);
    lv_obj_set_scrollbar_mode(state.server_switch_screen, LV_SCROLLBAR_MODE_OFF);

    state.server_switch_title_label = lv_label_create(state.server_switch_screen);
    lv_label_set_text(state.server_switch_title_label, GetSettingsItemName(0));
    lv_obj_align(state.server_switch_title_label, LV_ALIGN_TOP_MID, 0, 40);
    lv_obj_set_style_text_font(state.server_switch_title_label, lvgl_theme->text_font()->font(), 0);
    lv_obj_set_style_text_color(state.server_switch_title_label, lv_color_black(), 0);

    lv_obj_t* content = lv_obj_create(state.server_switch_screen);
    lv_obj_set_size(content, LV_HOR_RES, LV_VER_RES - 84);
    lv_obj_align(content, LV_ALIGN_TOP_MID, 0, 84);
    lv_obj_set_style_bg_color(content, lv_color_white(), 0);
    lv_obj_set_style_border_width(content, 0, 0);
    lv_obj_set_style_pad_all(content, 0, 0);
    lv_obj_set_scroll_dir(content, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(content, LV_SCROLLBAR_MODE_OFF);

    const int item_height = 54;
    for (int i = 0; i < static_cast<int>(kUiMenuSettingServerOptionCount); ++i) {
        lv_obj_t* item = lv_btn_create(content);
        lv_obj_set_size(item, LV_HOR_RES, item_height);
        lv_obj_set_pos(item, 0, i * item_height);
        lv_obj_set_style_radius(item, 0, 0);
        lv_obj_set_style_bg_color(item, lv_color_white(), 0);
        lv_obj_set_style_bg_opa(item, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(item, 0, 0);
        lv_obj_set_style_shadow_width(item, 0, 0);
        lv_obj_set_style_pad_left(item, 16, 0);
        lv_obj_set_style_pad_right(item, 16, 0);

        state.server_switch_option_buttons[i] = item;
        state.server_switch_option_labels[i] = lv_label_create(item);
        lv_label_set_text(state.server_switch_option_labels[i], GetServerSwitchOptionName(i));
        lv_obj_align(state.server_switch_option_labels[i], LV_ALIGN_LEFT_MID, 0, 0);
        lv_obj_set_style_text_color(state.server_switch_option_labels[i], lv_color_black(), 0);
        lv_obj_set_style_text_font(state.server_switch_option_labels[i], lvgl_theme->text_font()->font(), 0);

        state.server_switch_option_state_labels[i] = lv_label_create(item);
        lv_label_set_text(state.server_switch_option_state_labels[i], "");
        lv_obj_align(state.server_switch_option_state_labels[i], LV_ALIGN_RIGHT_MID, 0, 0);
        lv_obj_set_style_text_color(state.server_switch_option_state_labels[i], lv_color_hex(0x2A8F63), 0);
        lv_obj_set_style_text_font(state.server_switch_option_state_labels[i], lvgl_theme->text_font()->font(), 0);

        state.server_switch_option_data[i] = UiMenuSettingServerOptionData{&display, i};
        lv_obj_set_user_data(item, &state.server_switch_option_data[i]);
        lv_obj_add_event_cb(item, OnServerSwitchOptionClicked, LV_EVENT_CLICKED, nullptr);

        if (i < static_cast<int>(kUiMenuSettingServerOptionCount) - 1) {
            lv_obj_t* separator = lv_obj_create(content);
            lv_obj_set_size(separator, LV_HOR_RES - 32, 1);
            lv_obj_set_pos(separator, 16, (i + 1) * item_height);
            lv_obj_set_style_bg_color(separator, lv_color_hex(0xD9D9D9), 0);
            lv_obj_set_style_bg_opa(separator, LV_OPA_COVER, 0);
            lv_obj_set_style_border_width(separator, 0, 0);
            lv_obj_set_style_radius(separator, 0, 0);
            lv_obj_clear_flag(separator, LV_OBJ_FLAG_CLICKABLE);
        }
    }

    state.server_switch_back_button_data = UiBackButtonData{&display, kBackTargetSettings};
    display.CreateBackButton(state.server_switch_screen, &state.server_switch_back_button_data);
}

void UiMenuSetting::InitializeLanguageSwitchScreen(UiLcdDisplay& display) {
    DisplayLockGuard lock(&display);
    auto& state = display.menu_setting_;

    if (state.language_switch_screen != nullptr) {
        return;
    }

    auto* lvgl_theme = static_cast<LvglTheme*>(display.GetTheme());

    state.language_switch_screen = lv_obj_create(nullptr);
    lv_obj_set_style_bg_color(state.language_switch_screen, lv_color_white(), 0);
    lv_obj_set_style_border_width(state.language_switch_screen, 0, 0);
    lv_obj_set_style_pad_all(state.language_switch_screen, 0, 0);
    lv_obj_set_scrollbar_mode(state.language_switch_screen, LV_SCROLLBAR_MODE_OFF);

    state.language_switch_title_label = lv_label_create(state.language_switch_screen);
    lv_label_set_text(state.language_switch_title_label, GetSettingsItemName(1));
    lv_obj_align(state.language_switch_title_label, LV_ALIGN_TOP_MID, 0, 40);
    lv_obj_set_style_text_font(state.language_switch_title_label, lvgl_theme->text_font()->font(), 0);
    lv_obj_set_style_text_color(state.language_switch_title_label, lv_color_black(), 0);

    lv_obj_t* content = lv_obj_create(state.language_switch_screen);
    lv_obj_set_size(content, LV_HOR_RES, LV_VER_RES - 84);
    lv_obj_align(content, LV_ALIGN_TOP_MID, 0, 84);
    lv_obj_set_style_bg_color(content, lv_color_white(), 0);
    lv_obj_set_style_border_width(content, 0, 0);
    lv_obj_set_style_pad_all(content, 0, 0);
    lv_obj_set_scroll_dir(content, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(content, LV_SCROLLBAR_MODE_OFF);

    const int item_height = 54;
    for (int i = 0; i < static_cast<int>(kUiMenuSettingLanguageOptionCount); ++i) {
        lv_obj_t* item = lv_btn_create(content);
        lv_obj_set_size(item, LV_HOR_RES, item_height);
        lv_obj_set_pos(item, 0, i * item_height);
        lv_obj_set_style_radius(item, 0, 0);
        lv_obj_set_style_bg_color(item, lv_color_white(), 0);
        lv_obj_set_style_bg_opa(item, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(item, 0, 0);
        lv_obj_set_style_shadow_width(item, 0, 0);
        lv_obj_set_style_pad_left(item, 16, 0);
        lv_obj_set_style_pad_right(item, 16, 0);

        state.language_switch_option_buttons[i] = item;
        state.language_switch_option_labels[i] = lv_label_create(item);
        lv_label_set_text(state.language_switch_option_labels[i], GetLanguageSwitchOptionName(i));
        lv_obj_align(state.language_switch_option_labels[i], LV_ALIGN_LEFT_MID, 0, 0);
        lv_obj_set_style_text_color(state.language_switch_option_labels[i], lv_color_black(), 0);
        lv_obj_set_style_text_font(state.language_switch_option_labels[i], lvgl_theme->text_font()->font(), 0);

        state.language_switch_option_state_labels[i] = lv_label_create(item);
        lv_label_set_text(state.language_switch_option_state_labels[i], "");
        lv_obj_align(state.language_switch_option_state_labels[i], LV_ALIGN_RIGHT_MID, 0, 0);
        lv_obj_set_style_text_color(state.language_switch_option_state_labels[i], lv_color_hex(0x2A8F63), 0);
        lv_obj_set_style_text_font(state.language_switch_option_state_labels[i], lvgl_theme->text_font()->font(), 0);

        state.language_switch_option_data[i] = UiMenuSettingLanguageOptionData{&display, i};
        lv_obj_set_user_data(item, &state.language_switch_option_data[i]);
        lv_obj_add_event_cb(item, OnLanguageSwitchOptionClicked, LV_EVENT_CLICKED, nullptr);

        if (i < static_cast<int>(kUiMenuSettingLanguageOptionCount) - 1) {
            lv_obj_t* separator = lv_obj_create(content);
            lv_obj_set_size(separator, LV_HOR_RES - 32, 1);
            lv_obj_set_pos(separator, 16, (i + 1) * item_height);
            lv_obj_set_style_bg_color(separator, lv_color_hex(0xD9D9D9), 0);
            lv_obj_set_style_bg_opa(separator, LV_OPA_COVER, 0);
            lv_obj_set_style_border_width(separator, 0, 0);
            lv_obj_set_style_radius(separator, 0, 0);
            lv_obj_clear_flag(separator, LV_OBJ_FLAG_CLICKABLE);
        }
    }

    state.language_switch_back_button_data = UiBackButtonData{&display, kBackTargetSettings};
    display.CreateBackButton(state.language_switch_screen, &state.language_switch_back_button_data);
}

void UiMenuSetting::InitializeAboutScreen(UiLcdDisplay& display) {
    DisplayLockGuard lock(&display);
    auto& state = display.menu_setting_;

    if (state.about_screen != nullptr) {
        return;
    }

    auto* lvgl_theme = static_cast<LvglTheme*>(display.GetTheme());

    state.about_screen = lv_obj_create(nullptr);
    lv_obj_set_style_bg_color(state.about_screen, lv_color_white(), 0);
    lv_obj_set_style_border_width(state.about_screen, 0, 0);
    lv_obj_set_style_pad_all(state.about_screen, 0, 0);
    lv_obj_set_scrollbar_mode(state.about_screen, LV_SCROLLBAR_MODE_OFF);

    state.about_title_label = lv_label_create(state.about_screen);
    lv_label_set_text(state.about_title_label, GetAboutTitle());
    lv_obj_align(state.about_title_label, LV_ALIGN_TOP_MID, 0, 40);
    lv_obj_set_style_text_font(state.about_title_label, lvgl_theme->text_font()->font(), 0);
    lv_obj_set_style_text_color(state.about_title_label, lv_color_black(), 0);

    state.about_content = lv_obj_create(state.about_screen);
    lv_obj_set_size(state.about_content, LV_HOR_RES, LV_VER_RES - 84);
    lv_obj_align(state.about_content, LV_ALIGN_TOP_MID, 0, 84);
    lv_obj_set_style_bg_color(state.about_content, lv_color_white(), 0);
    lv_obj_set_style_border_width(state.about_content, 0, 0);
    lv_obj_set_style_pad_left(state.about_content, 16, 0);
    lv_obj_set_style_pad_right(state.about_content, 16, 0);
    lv_obj_set_style_pad_top(state.about_content, 0, 0);
    lv_obj_set_style_pad_bottom(state.about_content, 16, 0);
    lv_obj_set_scroll_dir(state.about_content, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(state.about_content, LV_SCROLLBAR_MODE_OFF);

    for (int i = 0; i < static_cast<int>(kUiMenuSettingAboutInfoItemCount); ++i) {
        state.about_info_title_labels[i] = lv_label_create(state.about_content);
        lv_label_set_text(state.about_info_title_labels[i], GetAboutInfoTitle(i));
        lv_obj_set_width(state.about_info_title_labels[i], LV_HOR_RES - 32);
        lv_obj_set_style_text_color(state.about_info_title_labels[i], lv_color_hex(0x666666), 0);
        lv_obj_set_style_text_font(state.about_info_title_labels[i], lvgl_theme->text_font()->font(), 0);

        state.about_info_value_labels[i] = lv_label_create(state.about_content);
        lv_obj_set_width(state.about_info_value_labels[i], LV_HOR_RES - 32);
        lv_label_set_long_mode(state.about_info_value_labels[i], LV_LABEL_LONG_WRAP);
        lv_obj_set_style_text_color(state.about_info_value_labels[i], lv_color_black(), 0);
        lv_obj_set_style_text_font(state.about_info_value_labels[i], lvgl_theme->text_font()->font(), 0);

        if (i < static_cast<int>(kUiMenuSettingAboutInfoItemCount) - 1) {
            state.about_info_separator_labels[i] = lv_obj_create(state.about_content);
            lv_obj_set_size(state.about_info_separator_labels[i], LV_HOR_RES - 32, 1);
            lv_obj_set_style_bg_color(state.about_info_separator_labels[i], lv_color_hex(0xD9D9D9), 0);
            lv_obj_set_style_bg_opa(state.about_info_separator_labels[i], LV_OPA_COVER, 0);
            lv_obj_set_style_border_width(state.about_info_separator_labels[i], 0, 0);
            lv_obj_set_style_radius(state.about_info_separator_labels[i], 0, 0);
            lv_obj_clear_flag(state.about_info_separator_labels[i], LV_OBJ_FLAG_CLICKABLE);
        }
    }

    state.about_back_button_data = UiBackButtonData{&display, kBackTargetSettings};
    display.CreateBackButton(state.about_screen, &state.about_back_button_data);
}

void UiMenuSetting::ShowSettingsScreen(UiLcdDisplay& display) {
    auto& state = display.menu_setting_;
    if (state.settings_screen == nullptr) {
        InitializeSettingsScreen(display);
    }

    DisplayLockGuard lock(&display);
    lv_screen_load(state.settings_screen);
    display.MoveStatusBarToScreen(state.settings_screen);
}

void UiMenuSetting::ShowServerSwitchScreen(UiLcdDisplay& display) {
    auto& state = display.menu_setting_;
    if (state.server_switch_screen == nullptr) {
        InitializeServerSwitchScreen(display);
    }

    RefreshServerSwitchScreen(display);
    DisplayLockGuard lock(&display);
    lv_screen_load(state.server_switch_screen);
    display.MoveStatusBarToScreen(state.server_switch_screen);
}

void UiMenuSetting::ShowLanguageSwitchScreen(UiLcdDisplay& display) {
    auto& state = display.menu_setting_;
    if (state.language_switch_screen == nullptr) {
        InitializeLanguageSwitchScreen(display);
    }

    RefreshLanguageSwitchScreen(display);
    DisplayLockGuard lock(&display);
    lv_screen_load(state.language_switch_screen);
    display.MoveStatusBarToScreen(state.language_switch_screen);
}

void UiMenuSetting::ShowAboutScreen(UiLcdDisplay& display) {
    auto& state = display.menu_setting_;
    if (state.about_screen == nullptr) {
        InitializeAboutScreen(display);
    }

    RefreshAboutScreenTexts(display);
    DisplayLockGuard lock(&display);
    if (state.about_content != nullptr) {
        lv_obj_scroll_to_y(state.about_content, 0, LV_ANIM_OFF);
    }
    lv_screen_load(state.about_screen);
    display.MoveStatusBarToScreen(state.about_screen);
}

void UiMenuSetting::RefreshServerSwitchScreen(UiLcdDisplay& display) {
    auto& state = display.menu_setting_;
    if (state.server_switch_title_label == nullptr) {
        return;
    }

    DisplayLockGuard lock(&display);
    lv_label_set_text(state.server_switch_title_label, GetSettingsItemName(0));

    const int current_index = GetCurrentServerSwitchOptionIndex();
    for (int i = 0; i < static_cast<int>(kUiMenuSettingServerOptionCount); ++i) {
        if (state.server_switch_option_labels[i] == nullptr ||
            state.server_switch_option_state_labels[i] == nullptr) {
            continue;
        }

        lv_label_set_text(state.server_switch_option_labels[i], GetServerSwitchOptionName(i));
        lv_label_set_text(state.server_switch_option_state_labels[i],
                          i == current_index ? kCheckMarkText : "");
    }
}

void UiMenuSetting::SelectServerSwitchOption(UiLcdDisplay& display, int index) {
    if (index < 0 || index >= static_cast<int>(kUiMenuSettingServerOptionCount)) {
        return;
    }

    Settings server_settings("server", true);
    Settings wifi_settings("wifi", true);
    const int current_index = GetCurrentServerSwitchOptionIndex();

    if (index == current_index) {
        RefreshServerSwitchScreen(display);
        return;
    }

    if (index == 1) {
        const std::string backup_url = server_settings.GetString("backup_ota_url", kBackupServerOtaUrl);
        if (backup_url.empty()) {
            display.ShowNotification("备选服务器未配置", 1500);
            RefreshServerSwitchScreen(display);
            return;
        }
        server_settings.SetInt("type", 1);
        wifi_settings.SetString("ota_url", backup_url);
    } else {
        server_settings.SetInt("type", 0);
        wifi_settings.SetString("ota_url", CONFIG_OTA_URL);
    }

    display.ShowNotification("正在切换服务器...", 1000);
    vTaskDelay(pdMS_TO_TICKS(1000));
    Application::GetInstance().Reboot();
}

void UiMenuSetting::RefreshLanguageSwitchScreen(UiLcdDisplay& display) {
    auto& state = display.menu_setting_;
    if (state.language_switch_title_label == nullptr) {
        return;
    }

    DisplayLockGuard lock(&display);
    lv_label_set_text(state.language_switch_title_label, GetSettingsItemName(1));

    const int current_index = GetCurrentLanguageSwitchOptionIndex();
    for (int i = 0; i < static_cast<int>(kUiMenuSettingLanguageOptionCount); ++i) {
        if (state.language_switch_option_labels[i] == nullptr ||
            state.language_switch_option_state_labels[i] == nullptr) {
            continue;
        }

        lv_label_set_text(state.language_switch_option_labels[i], GetLanguageSwitchOptionName(i));
        lv_label_set_text(state.language_switch_option_state_labels[i],
                          i == current_index ? kCheckMarkText : "");
    }
}

void UiMenuSetting::SelectLanguageSwitchOption(UiLcdDisplay& display, int index) {
    if (index < 0 || index >= static_cast<int>(kUiMenuSettingLanguageOptionCount)) {
        return;
    }

    const int current_index = GetCurrentLanguageSwitchOptionIndex();
    if (index == current_index) {
        RefreshLanguageSwitchScreen(display);
        return;
    }

    const char* language_code = "zh-CN";
    if (index == 1) {
        language_code = "zh-TW";
    } else if (index == 2) {
        language_code = "en-US";
    }

    Settings settings("ui", true);
    settings.SetString("language", language_code);

    display.ShowNotification("语言设置已保存，正在重启...", 1000);
    vTaskDelay(pdMS_TO_TICKS(1000));
    Application::GetInstance().Reboot();
}

void UiMenuSetting::RefreshAboutScreenTexts(UiLcdDisplay& display) {
    auto& state = display.menu_setting_;
    if (state.about_title_label == nullptr || state.about_content == nullptr) {
        return;
    }

    DisplayLockGuard lock(&display);
    lv_label_set_text(state.about_title_label, GetAboutTitle());

    const std::array<std::string, kUiMenuSettingAboutInfoItemCount> values = {
        GetFirmwareVersionForDisplay(),
        GetActivationCodeForDisplay(),
        GetServerAddressForDisplay(),
        GetWifiMacAddressForDisplay(),
        GetIpAddressForDisplay(),
        GetUuidForDisplay(),
    };

    int y = 0;
    for (int i = 0; i < static_cast<int>(kUiMenuSettingAboutInfoItemCount); ++i) {
        if (state.about_info_title_labels[i] == nullptr || state.about_info_value_labels[i] == nullptr) {
            continue;
        }

        lv_label_set_text(state.about_info_title_labels[i], GetAboutInfoTitle(i));
        lv_label_set_text(state.about_info_value_labels[i], values[i].c_str());
        lv_obj_update_layout(state.about_content);

        lv_obj_set_pos(state.about_info_title_labels[i], 0, y);
        y += lv_obj_get_height(state.about_info_title_labels[i]) + 4;

        lv_obj_set_pos(state.about_info_value_labels[i], 0, y);
        y += lv_obj_get_height(state.about_info_value_labels[i]) + 10;

        if (i < static_cast<int>(kUiMenuSettingAboutInfoItemCount) - 1 &&
            state.about_info_separator_labels[i] != nullptr) {
            lv_obj_set_pos(state.about_info_separator_labels[i], 0, y);
            y += 13;
        }
    }

    lv_obj_update_layout(state.about_content);
}

void UiMenuSetting::OnSettingsItemClicked(lv_event_t* e) {
    auto* target = static_cast<lv_obj_t*>(lv_event_get_current_target(e));
    auto* data = static_cast<UiMenuSettingItemData*>(lv_obj_get_user_data(target));
    if (data == nullptr || data->display == nullptr) {
        return;
    }

    if (data->item_id == 0) {
        ShowServerSwitchScreen(*data->display);
        return;
    }

    if (data->item_id == 1) {
        ShowLanguageSwitchScreen(*data->display);
        return;
    }

    if (data->item_id == 2) {
        ShowAboutScreen(*data->display);
        return;
    }

    std::string notification = GetSettingsItemName(data->item_id);
    notification += " 开发中";
    data->display->ShowNotification(notification.c_str(), 1200);
}

void UiMenuSetting::OnServerSwitchOptionClicked(lv_event_t* e) {
    auto* target = static_cast<lv_obj_t*>(lv_event_get_current_target(e));
    auto* data = static_cast<UiMenuSettingServerOptionData*>(lv_obj_get_user_data(target));
    if (data == nullptr || data->display == nullptr) {
        return;
    }

    SelectServerSwitchOption(*data->display, data->option_id);
}

void UiMenuSetting::OnLanguageSwitchOptionClicked(lv_event_t* e) {
    auto* target = static_cast<lv_obj_t*>(lv_event_get_current_target(e));
    auto* data = static_cast<UiMenuSettingLanguageOptionData*>(lv_obj_get_user_data(target));
    if (data == nullptr || data->display == nullptr) {
        return;
    }

    SelectLanguageSwitchOption(*data->display, data->option_id);
}
