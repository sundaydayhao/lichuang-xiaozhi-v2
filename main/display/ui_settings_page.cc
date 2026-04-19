#include "ui_display.h"

#include "application.h"
#include "board.h"
#include "dual_network_board.h"
#include "lvgl_theme.h"
#include "settings.h"
#include "system_info.h"
#include "assets/lang_config.h"

#include <at_modem.h>
#include <esp_app_desc.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <wifi_station.h>

namespace {

constexpr int kBackTargetMainMenu = 0;
constexpr int kBackTargetSettings = 1;
constexpr const char* kCheckMarkText = "√";
constexpr const char* kBackupServerOtaUrl = "";

} // namespace

const char* UiLcdDisplay::GetSettingsTitle() const {
    return "设置";
}

const char* UiLcdDisplay::GetSettingsItemName(int index) const {
    static const char* kTexts[kSettingsItemCount] = {
        "网络切换",
        "服务器切换",
        "语言切换",
        "关于本机",
    };
    return (index >= 0 && index < static_cast<int>(kSettingsItemCount)) ? kTexts[index] : "";
}

const char* UiLcdDisplay::GetNetworkSwitchOptionName(int index) const {
    static const char* kTexts[kNetworkSwitchOptionCount] = {
        "WiFi",
        "4G",
    };
    return (index >= 0 && index < static_cast<int>(kNetworkSwitchOptionCount)) ? kTexts[index] : "";
}

int UiLcdDisplay::GetCurrentNetworkSwitchOptionIndex() const {
    const auto* dual_network_board = dynamic_cast<DualNetworkBoard*>(&Board::GetInstance());
    if (dual_network_board == nullptr) {
        return -1;
    }

    return dual_network_board->GetNetworkType() == NetworkType::WIFI ? 0 : 1;
}

const char* UiLcdDisplay::GetServerSwitchOptionName(int index) const {
    static const char* kTexts[kServerSwitchOptionCount] = {
        "默认服务器",
        "备选服务器",
    };
    return (index >= 0 && index < static_cast<int>(kServerSwitchOptionCount)) ? kTexts[index] : "";
}

const char* UiLcdDisplay::GetLanguageSwitchOptionName(int index) const {
    static const char* kTexts[kLanguageSwitchOptionCount] = {
        "简体中文",
        "繁体中文",
        "英语",
    };
    return (index >= 0 && index < static_cast<int>(kLanguageSwitchOptionCount)) ? kTexts[index] : "";
}

std::string UiLcdDisplay::GetSelectedLanguageCode() const {
    Settings settings("ui", false);
    std::string language = settings.GetString("language");
    if (language == "zh-CN" || language == "zh-TW" || language == "en-US") {
        return language;
    }
    return Lang::CODE;
}

int UiLcdDisplay::GetCurrentServerSwitchOptionIndex() const {
    Settings settings("server", false);
    const int current_index = settings.GetInt("type", 0);
    std::string backup_url = settings.GetString("backup_ota_url", kBackupServerOtaUrl);
    if (current_index == 1 && !backup_url.empty()) {
        return 1;
    }
    return 0;
}

int UiLcdDisplay::GetCurrentLanguageSwitchOptionIndex() const {
    const std::string language = GetSelectedLanguageCode();
    if (language == "zh-TW") {
        return 1;
    }
    if (language == "en-US") {
        return 2;
    }
    return 0;
}

const char* UiLcdDisplay::GetAboutTitle() const {
    return "关于本机";
}

const char* UiLcdDisplay::GetAboutInfoTitle(int index) const {
    static const char* kTexts[kAboutInfoItemCount] = {
        "固件版本",
        "绑定验证码",
        "服务器地址",
        "WiFi MAC",
        "4G IMEI",
        "IP地址",
        "UUID",
    };
    return (index >= 0 && index < static_cast<int>(kAboutInfoItemCount)) ? kTexts[index] : "";
}

std::string UiLcdDisplay::GetFirmwareVersionForDisplay() const {
    const esp_app_desc_t* app_desc = esp_app_get_description();
    if (app_desc == nullptr || app_desc->version[0] == '\0') {
        return "unknown";
    }
    return app_desc->version;
}

std::string UiLcdDisplay::GetActivationCodeForDisplay() const {
    return "暂无";
}

std::string UiLcdDisplay::GetServerAddressForDisplay() const {
    Settings wifi_settings("wifi", false);
    std::string url = wifi_settings.GetString("ota_url");
    if (!url.empty()) {
        return url;
    }

    Settings websocket_settings("websocket", false);
    url = websocket_settings.GetString("url");
    return url.empty() ? "未配置" : url;
}

std::string UiLcdDisplay::GetWifiMacAddressForDisplay() const {
    std::string mac = SystemInfo::GetMacAddress();
    return mac.empty() ? "unknown" : mac;
}

std::string UiLcdDisplay::Get4gImeiForDisplay() const {
    auto* modem = dynamic_cast<AtModem*>(Board::GetInstance().GetNetwork());
    if (modem == nullptr) {
        return "未配置";
    }

    std::string imei = modem->GetImei();
    return imei.empty() ? "未配置" : imei;
}

std::string UiLcdDisplay::GetIpAddressForDisplay() const {
    const std::string& ip = WifiStation::GetInstance().GetIpAddress();
    return ip.empty() ? "未配置" : ip;
}

std::string UiLcdDisplay::GetUuidForDisplay() const {
    std::string uuid = Board::GetInstance().GetUuid();
    return uuid.empty() ? "unknown" : uuid;
}

void UiLcdDisplay::AllocateSettingsPageResources() {
    DisplayLockGuard lock(this);

    if (settings_screen_ != nullptr) {
        return;
    }

    auto* lvgl_theme = static_cast<LvglTheme*>(GetTheme());

    settings_screen_ = lv_obj_create(nullptr);
    lv_obj_set_style_bg_color(settings_screen_, lv_color_white(), 0);
    lv_obj_set_style_border_width(settings_screen_, 0, 0);
    lv_obj_set_style_pad_all(settings_screen_, 0, 0);
    lv_obj_set_scrollbar_mode(settings_screen_, LV_SCROLLBAR_MODE_OFF);

    settings_title_label_ = lv_label_create(settings_screen_);
    lv_label_set_text(settings_title_label_, GetSettingsTitle());
    lv_obj_align(settings_title_label_, LV_ALIGN_TOP_MID, 0, 40);
    lv_obj_set_style_text_font(settings_title_label_, lvgl_theme->text_font()->font(), 0);
    lv_obj_set_style_text_color(settings_title_label_, lv_color_black(), 0);

    lv_obj_t* content = lv_obj_create(settings_screen_);
    lv_obj_set_size(content, LV_HOR_RES, LV_VER_RES - 84);
    lv_obj_align(content, LV_ALIGN_TOP_MID, 0, 84);
    lv_obj_set_style_bg_color(content, lv_color_white(), 0);
    lv_obj_set_style_border_width(content, 0, 0);
    lv_obj_set_style_pad_all(content, 0, 0);
    lv_obj_set_scroll_dir(content, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(content, LV_SCROLLBAR_MODE_OFF);

    const int item_height = 46;
    for (int i = 0; i < static_cast<int>(kSettingsItemCount); ++i) {
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

        settings_item_name_labels_[i] = lv_label_create(item);
        lv_label_set_text(settings_item_name_labels_[i], GetSettingsItemName(i));
        lv_obj_align(settings_item_name_labels_[i], LV_ALIGN_LEFT_MID, 0, 0);
        lv_obj_set_style_text_color(settings_item_name_labels_[i], lv_color_black(), 0);
        lv_obj_set_style_text_font(settings_item_name_labels_[i], lvgl_theme->text_font()->font(), 0);

        lv_obj_t* arrow_label = lv_label_create(item);
        lv_label_set_text(arrow_label, ">");
        lv_obj_align(arrow_label, LV_ALIGN_RIGHT_MID, 0, 0);
        lv_obj_set_style_text_color(arrow_label, lv_color_hex(0xB0B0B0), 0);
        lv_obj_set_style_text_font(arrow_label, lvgl_theme->text_font()->font(), 0);

        settings_item_data_[i] = SettingsItemData{this, i};
        lv_obj_set_user_data(item, &settings_item_data_[i]);
        lv_obj_add_event_cb(item, OnSettingsItemClicked, LV_EVENT_CLICKED, nullptr);

        if (i < static_cast<int>(kSettingsItemCount) - 1) {
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

    settings_back_button_data_ = BackButtonData{this, kBackTargetMainMenu};
    CreateBackButton(settings_screen_, &settings_back_button_data_);
}

void UiLcdDisplay::AllocateNetworkSwitchPageResources() {
    DisplayLockGuard lock(this);

    if (network_switch_screen_ != nullptr) {
        return;
    }

    auto* lvgl_theme = static_cast<LvglTheme*>(GetTheme());

    network_switch_screen_ = lv_obj_create(nullptr);
    lv_obj_set_style_bg_color(network_switch_screen_, lv_color_white(), 0);
    lv_obj_set_style_border_width(network_switch_screen_, 0, 0);
    lv_obj_set_style_pad_all(network_switch_screen_, 0, 0);
    lv_obj_set_scrollbar_mode(network_switch_screen_, LV_SCROLLBAR_MODE_OFF);

    network_switch_title_label_ = lv_label_create(network_switch_screen_);
    lv_label_set_text(network_switch_title_label_, GetSettingsItemName(0));
    lv_obj_align(network_switch_title_label_, LV_ALIGN_TOP_MID, 0, 40);
    lv_obj_set_style_text_font(network_switch_title_label_, lvgl_theme->text_font()->font(), 0);
    lv_obj_set_style_text_color(network_switch_title_label_, lv_color_black(), 0);

    network_switch_hint_label_ = lv_label_create(network_switch_screen_);
    lv_label_set_text(network_switch_hint_label_, "选择切换到哪个网络");
    lv_obj_align(network_switch_hint_label_, LV_ALIGN_TOP_MID, 0, 68);
    lv_obj_set_style_text_font(network_switch_hint_label_, lvgl_theme->text_font()->font(), 0);
    lv_obj_set_style_text_color(network_switch_hint_label_, lv_color_hex(0x8A8A8A), 0);

    lv_obj_t* content = lv_obj_create(network_switch_screen_);
    lv_obj_set_size(content, LV_HOR_RES, LV_VER_RES - 120);
    lv_obj_align(content, LV_ALIGN_TOP_MID, 0, 108);
    lv_obj_set_style_bg_color(content, lv_color_white(), 0);
    lv_obj_set_style_border_width(content, 0, 0);
    lv_obj_set_style_pad_all(content, 0, 0);
    lv_obj_set_scroll_dir(content, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(content, LV_SCROLLBAR_MODE_OFF);

    const int item_height = 54;
    for (int i = 0; i < static_cast<int>(kNetworkSwitchOptionCount); ++i) {
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

        network_switch_option_buttons_[i] = item;
        network_switch_option_labels_[i] = lv_label_create(item);
        lv_label_set_text(network_switch_option_labels_[i], GetNetworkSwitchOptionName(i));
        lv_obj_align(network_switch_option_labels_[i], LV_ALIGN_LEFT_MID, 0, 0);
        lv_obj_set_style_text_color(network_switch_option_labels_[i], lv_color_black(), 0);
        lv_obj_set_style_text_font(network_switch_option_labels_[i], lvgl_theme->text_font()->font(), 0);

        network_switch_option_state_labels_[i] = lv_label_create(item);
        lv_label_set_text(network_switch_option_state_labels_[i], "");
        lv_obj_align(network_switch_option_state_labels_[i], LV_ALIGN_RIGHT_MID, 0, 0);
        lv_obj_set_style_text_color(network_switch_option_state_labels_[i], lv_color_hex(0x2A8F63), 0);
        lv_obj_set_style_text_font(network_switch_option_state_labels_[i], lvgl_theme->text_font()->font(), 0);

        network_switch_option_data_[i] = NetworkSwitchOptionData{this, i};
        lv_obj_set_user_data(item, &network_switch_option_data_[i]);
        lv_obj_add_event_cb(item, OnNetworkSwitchOptionClicked, LV_EVENT_CLICKED, nullptr);

        if (i < static_cast<int>(kNetworkSwitchOptionCount) - 1) {
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

    network_switch_back_button_data_ = BackButtonData{this, kBackTargetSettings};
    CreateBackButton(network_switch_screen_, &network_switch_back_button_data_);
}

void UiLcdDisplay::AllocateServerSwitchPageResources() {
    DisplayLockGuard lock(this);

    if (server_switch_screen_ != nullptr) {
        return;
    }

    auto* lvgl_theme = static_cast<LvglTheme*>(GetTheme());

    server_switch_screen_ = lv_obj_create(nullptr);
    lv_obj_set_style_bg_color(server_switch_screen_, lv_color_white(), 0);
    lv_obj_set_style_border_width(server_switch_screen_, 0, 0);
    lv_obj_set_style_pad_all(server_switch_screen_, 0, 0);
    lv_obj_set_scrollbar_mode(server_switch_screen_, LV_SCROLLBAR_MODE_OFF);

    server_switch_title_label_ = lv_label_create(server_switch_screen_);
    lv_label_set_text(server_switch_title_label_, GetSettingsItemName(1));
    lv_obj_align(server_switch_title_label_, LV_ALIGN_TOP_MID, 0, 40);
    lv_obj_set_style_text_font(server_switch_title_label_, lvgl_theme->text_font()->font(), 0);
    lv_obj_set_style_text_color(server_switch_title_label_, lv_color_black(), 0);

    lv_obj_t* content = lv_obj_create(server_switch_screen_);
    lv_obj_set_size(content, LV_HOR_RES, LV_VER_RES - 84);
    lv_obj_align(content, LV_ALIGN_TOP_MID, 0, 84);
    lv_obj_set_style_bg_color(content, lv_color_white(), 0);
    lv_obj_set_style_border_width(content, 0, 0);
    lv_obj_set_style_pad_all(content, 0, 0);
    lv_obj_set_scroll_dir(content, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(content, LV_SCROLLBAR_MODE_OFF);

    const int item_height = 54;
    for (int i = 0; i < static_cast<int>(kServerSwitchOptionCount); ++i) {
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

        server_switch_option_buttons_[i] = item;
        server_switch_option_labels_[i] = lv_label_create(item);
        lv_label_set_text(server_switch_option_labels_[i], GetServerSwitchOptionName(i));
        lv_obj_align(server_switch_option_labels_[i], LV_ALIGN_LEFT_MID, 0, 0);
        lv_obj_set_style_text_color(server_switch_option_labels_[i], lv_color_black(), 0);
        lv_obj_set_style_text_font(server_switch_option_labels_[i], lvgl_theme->text_font()->font(), 0);

        server_switch_option_state_labels_[i] = lv_label_create(item);
        lv_label_set_text(server_switch_option_state_labels_[i], "");
        lv_obj_align(server_switch_option_state_labels_[i], LV_ALIGN_RIGHT_MID, 0, 0);
        lv_obj_set_style_text_color(server_switch_option_state_labels_[i], lv_color_hex(0x2A8F63), 0);
        lv_obj_set_style_text_font(server_switch_option_state_labels_[i], lvgl_theme->text_font()->font(), 0);

        server_switch_option_data_[i] = ServerSwitchOptionData{this, i};
        lv_obj_set_user_data(item, &server_switch_option_data_[i]);
        lv_obj_add_event_cb(item, OnServerSwitchOptionClicked, LV_EVENT_CLICKED, nullptr);

        if (i < static_cast<int>(kServerSwitchOptionCount) - 1) {
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

    server_switch_back_button_data_ = BackButtonData{this, kBackTargetSettings};
    CreateBackButton(server_switch_screen_, &server_switch_back_button_data_);
}

void UiLcdDisplay::AllocateAboutPageResources() {
    DisplayLockGuard lock(this);

    if (about_screen_ != nullptr) {
        return;
    }

    auto* lvgl_theme = static_cast<LvglTheme*>(GetTheme());

    about_screen_ = lv_obj_create(nullptr);
    lv_obj_set_style_bg_color(about_screen_, lv_color_white(), 0);
    lv_obj_set_style_border_width(about_screen_, 0, 0);
    lv_obj_set_style_pad_all(about_screen_, 0, 0);
    lv_obj_set_scrollbar_mode(about_screen_, LV_SCROLLBAR_MODE_OFF);

    about_title_label_ = lv_label_create(about_screen_);
    lv_label_set_text(about_title_label_, GetAboutTitle());
    lv_obj_align(about_title_label_, LV_ALIGN_TOP_MID, 0, 40);
    lv_obj_set_style_text_font(about_title_label_, lvgl_theme->text_font()->font(), 0);
    lv_obj_set_style_text_color(about_title_label_, lv_color_black(), 0);

    about_content_ = lv_obj_create(about_screen_);
    lv_obj_set_size(about_content_, LV_HOR_RES, LV_VER_RES - 84);
    lv_obj_align(about_content_, LV_ALIGN_TOP_MID, 0, 84);
    lv_obj_set_style_bg_color(about_content_, lv_color_white(), 0);
    lv_obj_set_style_border_width(about_content_, 0, 0);
    lv_obj_set_style_pad_left(about_content_, 16, 0);
    lv_obj_set_style_pad_right(about_content_, 16, 0);
    lv_obj_set_style_pad_top(about_content_, 0, 0);
    lv_obj_set_style_pad_bottom(about_content_, 16, 0);
    lv_obj_set_scroll_dir(about_content_, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(about_content_, LV_SCROLLBAR_MODE_OFF);

    for (int i = 0; i < static_cast<int>(kAboutInfoItemCount); ++i) {
        about_info_title_labels_[i] = lv_label_create(about_content_);
        lv_label_set_text(about_info_title_labels_[i], GetAboutInfoTitle(i));
        lv_obj_set_width(about_info_title_labels_[i], LV_HOR_RES - 32);
        lv_obj_set_style_text_color(about_info_title_labels_[i], lv_color_hex(0x666666), 0);
        lv_obj_set_style_text_font(about_info_title_labels_[i], lvgl_theme->text_font()->font(), 0);

        about_info_value_labels_[i] = lv_label_create(about_content_);
        lv_obj_set_width(about_info_value_labels_[i], LV_HOR_RES - 32);
        lv_label_set_long_mode(about_info_value_labels_[i], LV_LABEL_LONG_WRAP);
        lv_obj_set_style_text_color(about_info_value_labels_[i], lv_color_black(), 0);
        lv_obj_set_style_text_font(about_info_value_labels_[i], lvgl_theme->text_font()->font(), 0);

        if (i < static_cast<int>(kAboutInfoItemCount) - 1) {
            about_info_separator_labels_[i] = lv_obj_create(about_content_);
            lv_obj_set_size(about_info_separator_labels_[i], LV_HOR_RES - 32, 1);
            lv_obj_set_style_bg_color(about_info_separator_labels_[i], lv_color_hex(0xD9D9D9), 0);
            lv_obj_set_style_bg_opa(about_info_separator_labels_[i], LV_OPA_COVER, 0);
            lv_obj_set_style_border_width(about_info_separator_labels_[i], 0, 0);
            lv_obj_set_style_radius(about_info_separator_labels_[i], 0, 0);
            lv_obj_clear_flag(about_info_separator_labels_[i], LV_OBJ_FLAG_CLICKABLE);
        }
    }

    about_back_button_data_ = BackButtonData{this, 1};
    CreateBackButton(about_screen_, &about_back_button_data_);
}

void UiLcdDisplay::FreeAboutPageResources() {
    DisplayLockGuard lock(this);
    if (about_screen_ != nullptr) {
        lv_obj_del(about_screen_);
        about_screen_ = nullptr;
    }

    about_title_label_ = nullptr;
    about_content_ = nullptr;
    about_info_title_labels_.fill(nullptr);
    about_info_value_labels_.fill(nullptr);
    about_info_separator_labels_.fill(nullptr);
}

void UiLcdDisplay::ShowAboutScreen() {
    if (about_screen_ == nullptr) {
        AllocateAboutPageResources();
    }

    RefreshAboutScreenTexts();
    {
        DisplayLockGuard lock(this);
        if (about_content_ != nullptr) {
            lv_obj_scroll_to_y(about_content_, 0, LV_ANIM_OFF);
        }
    }
    LoadScreen(about_screen_);
}

void UiLcdDisplay::RefreshAboutScreenTexts() {
    if (about_title_label_ == nullptr || about_content_ == nullptr) {
        return;
    }

    DisplayLockGuard lock(this);
    lv_label_set_text(about_title_label_, GetAboutTitle());

    const std::array<std::string, kAboutInfoItemCount> values = {
        GetFirmwareVersionForDisplay(),
        GetActivationCodeForDisplay(),
        GetServerAddressForDisplay(),
        GetWifiMacAddressForDisplay(),
        Get4gImeiForDisplay(),
        GetIpAddressForDisplay(),
        GetUuidForDisplay(),
    };

    int y = 0;
    for (int i = 0; i < static_cast<int>(kAboutInfoItemCount); ++i) {
        if (about_info_title_labels_[i] == nullptr || about_info_value_labels_[i] == nullptr) {
            continue;
        }

        lv_label_set_text(about_info_title_labels_[i], GetAboutInfoTitle(i));
        lv_label_set_text(about_info_value_labels_[i], values[i].c_str());
        lv_obj_update_layout(about_content_);

        lv_obj_set_pos(about_info_title_labels_[i], 0, y);
        y += lv_obj_get_height(about_info_title_labels_[i]) + 4;

        lv_obj_set_pos(about_info_value_labels_[i], 0, y);
        y += lv_obj_get_height(about_info_value_labels_[i]) + 10;

        if (i < static_cast<int>(kAboutInfoItemCount) - 1 && about_info_separator_labels_[i] != nullptr) {
            lv_obj_set_pos(about_info_separator_labels_[i], 0, y);
            y += 13;
        }
    }

    lv_obj_update_layout(about_content_);
}

void UiLcdDisplay::AllocateLanguageSwitchPageResources() {
    DisplayLockGuard lock(this);

    if (language_switch_screen_ != nullptr) {
        return;
    }

    auto* lvgl_theme = static_cast<LvglTheme*>(GetTheme());

    language_switch_screen_ = lv_obj_create(nullptr);
    lv_obj_set_style_bg_color(language_switch_screen_, lv_color_white(), 0);
    lv_obj_set_style_border_width(language_switch_screen_, 0, 0);
    lv_obj_set_style_pad_all(language_switch_screen_, 0, 0);
    lv_obj_set_scrollbar_mode(language_switch_screen_, LV_SCROLLBAR_MODE_OFF);

    language_switch_title_label_ = lv_label_create(language_switch_screen_);
    lv_label_set_text(language_switch_title_label_, GetSettingsItemName(2));
    lv_obj_align(language_switch_title_label_, LV_ALIGN_TOP_MID, 0, 40);
    lv_obj_set_style_text_font(language_switch_title_label_, lvgl_theme->text_font()->font(), 0);
    lv_obj_set_style_text_color(language_switch_title_label_, lv_color_black(), 0);

    lv_obj_t* content = lv_obj_create(language_switch_screen_);
    lv_obj_set_size(content, LV_HOR_RES, LV_VER_RES - 84);
    lv_obj_align(content, LV_ALIGN_TOP_MID, 0, 84);
    lv_obj_set_style_bg_color(content, lv_color_white(), 0);
    lv_obj_set_style_border_width(content, 0, 0);
    lv_obj_set_style_pad_all(content, 0, 0);
    lv_obj_set_scroll_dir(content, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(content, LV_SCROLLBAR_MODE_OFF);

    const int item_height = 54;
    for (int i = 0; i < static_cast<int>(kLanguageSwitchOptionCount); ++i) {
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

        language_switch_option_buttons_[i] = item;
        language_switch_option_labels_[i] = lv_label_create(item);
        lv_label_set_text(language_switch_option_labels_[i], GetLanguageSwitchOptionName(i));
        lv_obj_align(language_switch_option_labels_[i], LV_ALIGN_LEFT_MID, 0, 0);
        lv_obj_set_style_text_color(language_switch_option_labels_[i], lv_color_black(), 0);
        lv_obj_set_style_text_font(language_switch_option_labels_[i], lvgl_theme->text_font()->font(), 0);

        language_switch_option_state_labels_[i] = lv_label_create(item);
        lv_label_set_text(language_switch_option_state_labels_[i], "");
        lv_obj_align(language_switch_option_state_labels_[i], LV_ALIGN_RIGHT_MID, 0, 0);
        lv_obj_set_style_text_color(language_switch_option_state_labels_[i], lv_color_hex(0x2A8F63), 0);
        lv_obj_set_style_text_font(language_switch_option_state_labels_[i], lvgl_theme->text_font()->font(), 0);

        language_switch_option_data_[i] = LanguageSwitchOptionData{this, i};
        lv_obj_set_user_data(item, &language_switch_option_data_[i]);
        lv_obj_add_event_cb(item, OnLanguageSwitchOptionClicked, LV_EVENT_CLICKED, nullptr);

        if (i < static_cast<int>(kLanguageSwitchOptionCount) - 1) {
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

    language_switch_back_button_data_ = BackButtonData{this, kBackTargetSettings};
    CreateBackButton(language_switch_screen_, &language_switch_back_button_data_);
}

void UiLcdDisplay::ShowSettingsScreen() {
    if (settings_screen_ == nullptr) {
        AllocateSettingsPageResources();
    }

    LoadScreen(settings_screen_);
}

void UiLcdDisplay::ShowNetworkSwitchScreen() {
    if (network_switch_screen_ == nullptr) {
        AllocateNetworkSwitchPageResources();
    }

    RefreshNetworkSwitchScreen();
    LoadScreen(network_switch_screen_);
}

void UiLcdDisplay::ShowServerSwitchScreen() {
    if (server_switch_screen_ == nullptr) {
        AllocateServerSwitchPageResources();
    }

    RefreshServerSwitchScreen();
    LoadScreen(server_switch_screen_);
}

void UiLcdDisplay::ShowLanguageSwitchScreen() {
    if (language_switch_screen_ == nullptr) {
        AllocateLanguageSwitchPageResources();
    }

    RefreshLanguageSwitchScreen();
    LoadScreen(language_switch_screen_);
}

void UiLcdDisplay::FreeSettingsPageResources() {
    DisplayLockGuard lock(this);
    if (settings_screen_ != nullptr) {
        lv_obj_del(settings_screen_);
        settings_screen_ = nullptr;
    }

    settings_title_label_ = nullptr;
    settings_item_name_labels_.fill(nullptr);
}

void UiLcdDisplay::FreeNetworkSwitchPageResources() {
    DisplayLockGuard lock(this);
    if (network_switch_screen_ != nullptr) {
        lv_obj_del(network_switch_screen_);
        network_switch_screen_ = nullptr;
    }

    network_switch_title_label_ = nullptr;
    network_switch_hint_label_ = nullptr;
    network_switch_option_buttons_.fill(nullptr);
    network_switch_option_labels_.fill(nullptr);
    network_switch_option_state_labels_.fill(nullptr);
}

void UiLcdDisplay::FreeServerSwitchPageResources() {
    DisplayLockGuard lock(this);
    if (server_switch_screen_ != nullptr) {
        lv_obj_del(server_switch_screen_);
        server_switch_screen_ = nullptr;
    }

    server_switch_title_label_ = nullptr;
    server_switch_option_buttons_.fill(nullptr);
    server_switch_option_labels_.fill(nullptr);
    server_switch_option_state_labels_.fill(nullptr);
}

void UiLcdDisplay::FreeLanguageSwitchPageResources() {
    DisplayLockGuard lock(this);
    if (language_switch_screen_ != nullptr) {
        lv_obj_del(language_switch_screen_);
        language_switch_screen_ = nullptr;
    }

    language_switch_title_label_ = nullptr;
    language_switch_option_buttons_.fill(nullptr);
    language_switch_option_labels_.fill(nullptr);
    language_switch_option_state_labels_.fill(nullptr);
}

void UiLcdDisplay::RefreshNetworkSwitchScreen() {
    if (network_switch_title_label_ == nullptr || network_switch_hint_label_ == nullptr) {
        return;
    }

    DisplayLockGuard lock(this);
    lv_label_set_text(network_switch_title_label_, GetSettingsItemName(0));

    const int current_index = GetCurrentNetworkSwitchOptionIndex();
    if (current_index >= 0) {
        lv_label_set_text(network_switch_hint_label_, "选择切换到哪个网络");
    } else {
        lv_label_set_text(network_switch_hint_label_, "当前设备不支持网络切换");
    }

    for (int i = 0; i < static_cast<int>(kNetworkSwitchOptionCount); ++i) {
        if (network_switch_option_labels_[i] == nullptr || network_switch_option_state_labels_[i] == nullptr) {
            continue;
        }

        lv_label_set_text(network_switch_option_labels_[i], GetNetworkSwitchOptionName(i));
        lv_label_set_text(network_switch_option_state_labels_[i], i == current_index ? kCheckMarkText : "");
    }
}

void UiLcdDisplay::SelectNetworkSwitchOption(int index) {
    if (index < 0 || index >= static_cast<int>(kNetworkSwitchOptionCount)) {
        return;
    }

    auto* dual_network_board = dynamic_cast<DualNetworkBoard*>(&Board::GetInstance());
    if (dual_network_board == nullptr) {
        ShowNotification("当前设备不支持网络切换", 1500);
        return;
    }

    const NetworkType target_type = (index == 0) ? NetworkType::WIFI : NetworkType::ML307;
    if (dual_network_board->GetNetworkType() == target_type) {
        RefreshNetworkSwitchScreen();
        return;
    }

    dual_network_board->SwitchNetworkType(target_type);
}

void UiLcdDisplay::RefreshServerSwitchScreen() {
    if (server_switch_title_label_ == nullptr) {
        return;
    }

    DisplayLockGuard lock(this);
    lv_label_set_text(server_switch_title_label_, GetSettingsItemName(1));

    const int current_index = GetCurrentServerSwitchOptionIndex();
    for (int i = 0; i < static_cast<int>(kServerSwitchOptionCount); ++i) {
        if (server_switch_option_labels_[i] == nullptr || server_switch_option_state_labels_[i] == nullptr) {
            continue;
        }

        lv_label_set_text(server_switch_option_labels_[i], GetServerSwitchOptionName(i));
        lv_label_set_text(server_switch_option_state_labels_[i], i == current_index ? kCheckMarkText : "");
    }
}

void UiLcdDisplay::SelectServerSwitchOption(int index) {
    if (index < 0 || index >= static_cast<int>(kServerSwitchOptionCount)) {
        return;
    }

    Settings server_settings("server", true);
    Settings wifi_settings("wifi", true);
    const int current_index = GetCurrentServerSwitchOptionIndex();

    if (index == current_index) {
        RefreshServerSwitchScreen();
        return;
    }

    if (index == 1) {
        const std::string backup_url = server_settings.GetString("backup_ota_url", kBackupServerOtaUrl);
        if (backup_url.empty()) {
            ShowNotification("备选服务器未配置", 1500);
            RefreshServerSwitchScreen();
            return;
        }
        server_settings.SetInt("type", 1);
        wifi_settings.SetString("ota_url", backup_url);
    } else {
        server_settings.SetInt("type", 0);
        wifi_settings.SetString("ota_url", CONFIG_OTA_URL);
    }

    ShowNotification("正在切换服务器...", 1000);
    vTaskDelay(pdMS_TO_TICKS(1000));
    Application::GetInstance().Reboot();
}

void UiLcdDisplay::RefreshLanguageSwitchScreen() {
    if (language_switch_title_label_ == nullptr) {
        return;
    }

    DisplayLockGuard lock(this);
    lv_label_set_text(language_switch_title_label_, GetSettingsItemName(2));

    const int current_index = GetCurrentLanguageSwitchOptionIndex();
    for (int i = 0; i < static_cast<int>(kLanguageSwitchOptionCount); ++i) {
        if (language_switch_option_labels_[i] == nullptr || language_switch_option_state_labels_[i] == nullptr) {
            continue;
        }

        lv_label_set_text(language_switch_option_labels_[i], GetLanguageSwitchOptionName(i));
        lv_label_set_text(language_switch_option_state_labels_[i], i == current_index ? kCheckMarkText : "");
    }
}

void UiLcdDisplay::SelectLanguageSwitchOption(int index) {
    if (index < 0 || index >= static_cast<int>(kLanguageSwitchOptionCount)) {
        return;
    }

    const int current_index = GetCurrentLanguageSwitchOptionIndex();
    if (index == current_index) {
        RefreshLanguageSwitchScreen();
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

    ShowNotification("语言设置已保存，正在重启...", 1000);
    vTaskDelay(pdMS_TO_TICKS(1000));
    Application::GetInstance().Reboot();
}

void UiLcdDisplay::OnSettingsItemClicked(lv_event_t* e) {
    auto* target = static_cast<lv_obj_t*>(lv_event_get_current_target(e));
    auto* data = static_cast<SettingsItemData*>(lv_obj_get_user_data(target));
    if (data == nullptr || data->display == nullptr) {
        return;
    }

    if (data->item_id == 0) {
        data->display->ShowNetworkSwitchScreen();
        return;
    }

    if (data->item_id == 1) {
        data->display->ShowServerSwitchScreen();
        return;
    }

    if (data->item_id == 2) {
        data->display->ShowLanguageSwitchScreen();
        return;
    }

    if (data->item_id == 3) {
        data->display->ShowAboutScreen();
        return;
    }

    std::string notification = data->display->GetSettingsItemName(data->item_id);
    notification += " 开发中";
    data->display->ShowNotification(notification.c_str(), 1200);
}

void UiLcdDisplay::OnNetworkSwitchOptionClicked(lv_event_t* e) {
    auto* target = static_cast<lv_obj_t*>(lv_event_get_current_target(e));
    auto* data = static_cast<NetworkSwitchOptionData*>(lv_obj_get_user_data(target));
    if (data == nullptr || data->display == nullptr) {
        return;
    }

    data->display->SelectNetworkSwitchOption(data->option_id);
}

void UiLcdDisplay::OnServerSwitchOptionClicked(lv_event_t* e) {
    auto* target = static_cast<lv_obj_t*>(lv_event_get_current_target(e));
    auto* data = static_cast<ServerSwitchOptionData*>(lv_obj_get_user_data(target));
    if (data == nullptr || data->display == nullptr) {
        return;
    }

    data->display->SelectServerSwitchOption(data->option_id);
}

void UiLcdDisplay::OnLanguageSwitchOptionClicked(lv_event_t* e) {
    auto* target = static_cast<lv_obj_t*>(lv_event_get_current_target(e));
    auto* data = static_cast<LanguageSwitchOptionData*>(lv_obj_get_user_data(target));
    if (data == nullptr || data->display == nullptr) {
        return;
    }

    data->display->SelectLanguageSwitchOption(data->option_id);
}
