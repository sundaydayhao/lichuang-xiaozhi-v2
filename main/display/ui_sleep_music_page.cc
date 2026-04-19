#include "ui_display.h"

#include "application.h"
#include "lvgl_theme.h"

#include <esp_err.h>
#include <esp_log.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <vector>

namespace {

constexpr char kSleepMusicTag[] = "SleepMusicFeature";
constexpr int kSleepMusicSampleRate = 16000;
constexpr int kSleepMusicChunkSamples = kSleepMusicSampleRate;
constexpr double kPi = 3.14159265358979323846;
constexpr std::array<int, 3> kSleepMusicDurations = {10 * 60, 20 * 60, 30 * 60};

int GetSleepMusicDurationSeconds(int duration_index) {
    if (duration_index < 0 || duration_index >= static_cast<int>(kSleepMusicDurations.size())) {
        return kSleepMusicDurations.front();
    }
    return kSleepMusicDurations[duration_index];
}

std::vector<int16_t> GenerateSleepMusicChunk(int selected_mode_index, int selected_duration_index,
                                             int remaining_seconds, uint32_t& noise_seed) {
    std::vector<int16_t> pcm(kSleepMusicChunkSamples, 0);
    const int total_duration = GetSleepMusicDurationSeconds(selected_duration_index);
    const int elapsed_seconds = std::max(0, total_duration - remaining_seconds);

    if (selected_mode_index == 0) {
        int filtered = 0;
        for (int i = 0; i < kSleepMusicChunkSamples; ++i) {
            noise_seed = noise_seed * 1664525u + 1013904223u;
            const int noise = static_cast<int>((noise_seed >> 16) & 0x7FFF) - 16384;
            noise_seed = noise_seed * 1664525u + 1013904223u;
            const int splash = (((noise_seed >> 24) & 0xFF) < 10) ? 2800 : 0;
            const int sample = noise / 10 + splash;
            filtered = (filtered * 7 + sample) / 8;
            pcm[i] = static_cast<int16_t>(std::clamp(filtered, -9000, 9000));
        }
        return pcm;
    }

    if (selected_mode_index == 1) {
        for (int i = 0; i < kSleepMusicChunkSamples; ++i) {
            noise_seed = noise_seed * 1103515245u + 12345u;
            const int noise = static_cast<int>((noise_seed >> 16) & 0x7FFF) - 16384;
            pcm[i] = static_cast<int16_t>(noise / 3);
        }
        return pcm;
    }

    static constexpr std::array<double, 8> kNotes = {261.63, 329.63, 392.00, 329.63,
                                                      293.66, 261.63, 293.66, 329.63};
    const int note_index = (elapsed_seconds / 2) % static_cast<int>(kNotes.size());
    const double frequency = kNotes[note_index];

    for (int i = 0; i < kSleepMusicChunkSamples; ++i) {
        const double t = static_cast<double>(elapsed_seconds) +
                         static_cast<double>(i) / static_cast<double>(kSleepMusicSampleRate);
        const double primary = std::sin(2.0 * kPi * frequency * t);
        const double harmony = 0.45 * std::sin(2.0 * kPi * (frequency * 1.5) * t);
        const double pulse = 0.15 * std::sin(2.0 * kPi * 2.0 * t);
        const double value = (primary + harmony) * (0.65 + pulse);
        pcm[i] = static_cast<int16_t>(std::clamp(value * 4800.0, -11000.0, 11000.0));
    }

    return pcm;
}

} // namespace

void UiLcdDisplay::InitializeSleepMusicSupport() {
    esp_timer_create_args_t sleep_music_timer_args = {
        .callback = [](void* arg) {
            auto* display = static_cast<UiLcdDisplay*>(arg);
            if (display != nullptr) {
                display->HandleSleepMusicTick();
            }
        },
        .arg = this,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "sleep_music_ui_timer",
        .skip_unhandled_events = true,
    };
    ESP_ERROR_CHECK(esp_timer_create(&sleep_music_timer_args, &sleep_music_timer_));
}

void UiLcdDisplay::DeinitializeSleepMusicSupport() {
    StopSleepMusicPlayback(false);
    DestroySleepMusicScreen();
    if (sleep_music_timer_ != nullptr) {
        esp_timer_delete(sleep_music_timer_);
        sleep_music_timer_ = nullptr;
    }
}

void UiLcdDisplay::InitializeSleepMusicScreen() {
    if (sleep_music_screen_ != nullptr) {
        return;
    }

    DisplayLockGuard lock(this);
    auto* lvgl_theme = static_cast<LvglTheme*>(GetTheme());

    sleep_music_screen_ = lv_obj_create(nullptr);
    lv_obj_set_style_bg_color(sleep_music_screen_, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(sleep_music_screen_, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(sleep_music_screen_, 0, 0);
    lv_obj_set_style_pad_all(sleep_music_screen_, 0, 0);
    lv_obj_set_scrollbar_mode(sleep_music_screen_, LV_SCROLLBAR_MODE_OFF);
    lv_obj_remove_flag(sleep_music_screen_, static_cast<lv_obj_flag_t>(
                                                LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_SCROLL_ELASTIC |
                                                LV_OBJ_FLAG_SCROLL_MOMENTUM | LV_OBJ_FLAG_SCROLL_CHAIN));

    if (const lv_image_dsc_t* background = LoadAssetImage("bg_sleep_music.png");
        background != nullptr) {
        lv_obj_t* background_image = lv_image_create(sleep_music_screen_);
        lv_obj_set_size(background_image, LV_HOR_RES, LV_VER_RES);
        lv_obj_add_flag(background_image, LV_OBJ_FLAG_FLOATING);
        lv_obj_add_flag(background_image, LV_OBJ_FLAG_IGNORE_LAYOUT);
        lv_obj_set_style_bg_opa(background_image, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(background_image, 0, 0);
        lv_obj_set_pos(background_image, 0, 0);
        lv_image_set_inner_align(background_image, LV_IMAGE_ALIGN_STRETCH);
        lv_image_set_src(background_image, background);
        lv_obj_move_to_index(background_image, 0);
    }

    sleep_music_title_label_ = lv_label_create(sleep_music_screen_);
    lv_obj_align(sleep_music_title_label_, LV_ALIGN_TOP_MID, 0, 40);
    lv_obj_set_style_text_font(sleep_music_title_label_, lvgl_theme->text_font()->font(), 0);
    lv_obj_set_style_text_color(sleep_music_title_label_, lv_color_black(), 0);

    lv_obj_t* content = lv_obj_create(sleep_music_screen_);
    lv_obj_set_size(content, LV_HOR_RES, LV_VER_RES - 84);
    lv_obj_align(content, LV_ALIGN_TOP_MID, 0, 84);
    lv_obj_set_style_bg_color(content, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(content, LV_OPA_80, 0);
    lv_obj_set_style_border_width(content, 0, 0);
    lv_obj_set_style_pad_all(content, 16, 0);
    lv_obj_set_style_pad_row(content, 12, 0);
    lv_obj_set_scrollbar_mode(content, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_flex_flow(content, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(content, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_remove_flag(content, static_cast<lv_obj_flag_t>(
                                    LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_SCROLL_ELASTIC |
                                    LV_OBJ_FLAG_SCROLL_MOMENTUM | LV_OBJ_FLAG_SCROLL_CHAIN));

    sleep_music_status_label_ = lv_label_create(content);
    lv_obj_set_width(sleep_music_status_label_, LV_HOR_RES - 32);
    lv_label_set_long_mode(sleep_music_status_label_, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(sleep_music_status_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(sleep_music_status_label_, lv_color_hex(0x5A5A5A), 0);
    lv_obj_set_style_text_font(sleep_music_status_label_, lvgl_theme->text_font()->font(), 0);

    lv_obj_t* mode_row = lv_obj_create(content);
    lv_obj_set_size(mode_row, LV_HOR_RES - 32, 64);
    lv_obj_set_style_bg_opa(mode_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(mode_row, 0, 0);
    lv_obj_set_style_pad_all(mode_row, 0, 0);
    lv_obj_set_style_pad_column(mode_row, 8, 0);
    lv_obj_set_flex_flow(mode_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(mode_row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_remove_flag(mode_row, static_cast<lv_obj_flag_t>(
                                     LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_SCROLL_ELASTIC |
                                     LV_OBJ_FLAG_SCROLL_MOMENTUM | LV_OBJ_FLAG_SCROLL_CHAIN));

    for (int i = 0; i < static_cast<int>(sleep_music_mode_buttons_.size()); ++i) {
        sleep_music_mode_buttons_[i] = lv_btn_create(mode_row);
        lv_obj_set_size(sleep_music_mode_buttons_[i], 64, 64);
        lv_obj_set_style_radius(sleep_music_mode_buttons_[i], 16, 0);
        lv_obj_set_style_border_width(sleep_music_mode_buttons_[i], 0, 0);
        lv_obj_set_style_shadow_width(sleep_music_mode_buttons_[i], 0, 0);

        sleep_music_mode_labels_[i] = lv_label_create(sleep_music_mode_buttons_[i]);
        lv_obj_set_width(sleep_music_mode_labels_[i], 54);
        lv_label_set_long_mode(sleep_music_mode_labels_[i], LV_LABEL_LONG_WRAP);
        lv_obj_set_style_text_align(sleep_music_mode_labels_[i], LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_font(sleep_music_mode_labels_[i], lvgl_theme->text_font()->font(), 0);
        lv_obj_center(sleep_music_mode_labels_[i]);

        sleep_music_mode_button_data_[i] = SleepMusicOptionData{this, i};
        lv_obj_set_user_data(sleep_music_mode_buttons_[i], &sleep_music_mode_button_data_[i]);
        lv_obj_add_event_cb(
            sleep_music_mode_buttons_[i],
            [](lv_event_t* e) {
                if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
                    return;
                }
                auto* target = static_cast<lv_obj_t*>(lv_event_get_current_target(e));
                auto* data = static_cast<SleepMusicOptionData*>(lv_obj_get_user_data(target));
                if (data != nullptr && data->display != nullptr) {
                    data->display->SelectSleepMusicMode(data->index);
                }
            },
            LV_EVENT_CLICKED, nullptr);
    }

    lv_obj_t* duration_row = lv_obj_create(content);
    lv_obj_set_size(duration_row, LV_HOR_RES - 32, 48);
    lv_obj_set_style_bg_opa(duration_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(duration_row, 0, 0);
    lv_obj_set_style_pad_all(duration_row, 0, 0);
    lv_obj_set_style_pad_column(duration_row, 8, 0);
    lv_obj_set_flex_flow(duration_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(duration_row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_remove_flag(duration_row, static_cast<lv_obj_flag_t>(
                                         LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_SCROLL_ELASTIC |
                                         LV_OBJ_FLAG_SCROLL_MOMENTUM | LV_OBJ_FLAG_SCROLL_CHAIN));

    for (int i = 0; i < static_cast<int>(sleep_music_duration_buttons_.size()); ++i) {
        sleep_music_duration_buttons_[i] = lv_btn_create(duration_row);
        lv_obj_set_size(sleep_music_duration_buttons_[i], 64, 40);
        lv_obj_set_style_radius(sleep_music_duration_buttons_[i], 14, 0);
        lv_obj_set_style_border_width(sleep_music_duration_buttons_[i], 0, 0);
        lv_obj_set_style_shadow_width(sleep_music_duration_buttons_[i], 0, 0);

        sleep_music_duration_labels_[i] = lv_label_create(sleep_music_duration_buttons_[i]);
        lv_obj_set_width(sleep_music_duration_labels_[i], 54);
        lv_label_set_long_mode(sleep_music_duration_labels_[i], LV_LABEL_LONG_WRAP);
        lv_obj_set_style_text_align(sleep_music_duration_labels_[i], LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_font(sleep_music_duration_labels_[i], lvgl_theme->text_font()->font(),
                                   0);
        lv_obj_center(sleep_music_duration_labels_[i]);

        sleep_music_duration_button_data_[i] = SleepMusicOptionData{this, i};
        lv_obj_set_user_data(sleep_music_duration_buttons_[i], &sleep_music_duration_button_data_[i]);
        lv_obj_add_event_cb(
            sleep_music_duration_buttons_[i],
            [](lv_event_t* e) {
                if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
                    return;
                }
                auto* target = static_cast<lv_obj_t*>(lv_event_get_current_target(e));
                auto* data = static_cast<SleepMusicOptionData*>(lv_obj_get_user_data(target));
                if (data != nullptr && data->display != nullptr) {
                    data->display->SelectSleepMusicDuration(data->index);
                }
            },
            LV_EVENT_CLICKED, nullptr);
    }

    sleep_music_countdown_label_ = lv_label_create(content);
    lv_obj_set_width(sleep_music_countdown_label_, LV_HOR_RES - 32);
    lv_obj_set_style_text_align(sleep_music_countdown_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(sleep_music_countdown_label_, lvgl_theme->text_font()->font(), 0);
    lv_obj_set_style_text_color(sleep_music_countdown_label_, lv_color_hex(0x2F80ED), 0);

    sleep_music_action_button_ = lv_btn_create(content);
    lv_obj_set_size(sleep_music_action_button_, LV_HOR_RES - 64, 44);
    lv_obj_set_style_radius(sleep_music_action_button_, 18, 0);
    lv_obj_set_style_border_width(sleep_music_action_button_, 0, 0);
    lv_obj_set_style_shadow_width(sleep_music_action_button_, 0, 0);
    lv_obj_set_user_data(sleep_music_action_button_, this);
    lv_obj_add_event_cb(
        sleep_music_action_button_,
        [](lv_event_t* e) {
            if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
                return;
            }
            auto* target = static_cast<lv_obj_t*>(lv_event_get_current_target(e));
            auto* display = static_cast<UiLcdDisplay*>(lv_obj_get_user_data(target));
            if (display != nullptr) {
                display->ToggleSleepMusicPlayback();
            }
        },
        LV_EVENT_CLICKED, nullptr);

    sleep_music_action_label_ = lv_label_create(sleep_music_action_button_);
    lv_obj_set_style_text_font(sleep_music_action_label_, lvgl_theme->text_font()->font(), 0);
    lv_obj_center(sleep_music_action_label_);

    sleep_music_back_button_data_ = BackButtonData{this, 0};
    CreateBackButton(sleep_music_screen_, &sleep_music_back_button_data_);
    RefreshSleepMusicScreen();
}

void UiLcdDisplay::ShowSleepMusicScreen() {
    if (sleep_music_screen_ == nullptr) {
        InitializeSleepMusicScreen();
    }

    RefreshSleepMusicScreen();
    LoadScreen(sleep_music_screen_);
}

void UiLcdDisplay::DestroySleepMusicScreen() {
    DisplayLockGuard lock(this);
    if (sleep_music_screen_ != nullptr) {
        lv_obj_del(sleep_music_screen_);
        sleep_music_screen_ = nullptr;
    }

    sleep_music_title_label_ = nullptr;
    sleep_music_status_label_ = nullptr;
    sleep_music_mode_buttons_.fill(nullptr);
    sleep_music_mode_labels_.fill(nullptr);
    sleep_music_duration_buttons_.fill(nullptr);
    sleep_music_duration_labels_.fill(nullptr);
    sleep_music_countdown_label_ = nullptr;
    sleep_music_action_button_ = nullptr;
    sleep_music_action_label_ = nullptr;
}

void UiLcdDisplay::RefreshSleepMusicScreen() {
    if (sleep_music_screen_ == nullptr) {
        return;
    }

    static constexpr std::array<const char*, 3> kModeNames = {"雨声", "白噪音", "摇篮曲"};
    static constexpr std::array<const char*, 3> kDurationNames = {"10分钟", "20分钟", "30分钟"};
    static constexpr char kIdleStatus[] = "选择音效和时长后开始播放，本地合成音频会在倒计时结束后停止。";
    static constexpr char kPlayingStatus[] = "正在播放，可随时停止。";

    DisplayLockGuard lock(this);
    lv_label_set_text(sleep_music_title_label_, "助眠音乐");
    lv_label_set_text(sleep_music_status_label_, sleep_music_playing_ ? kPlayingStatus : kIdleStatus);

    for (int i = 0; i < static_cast<int>(sleep_music_mode_labels_.size()); ++i) {
        lv_label_set_text(sleep_music_mode_labels_[i], kModeNames[i]);
        lv_label_set_text(sleep_music_duration_labels_[i], kDurationNames[i]);
    }

    if (sleep_music_playing_ && sleep_music_remaining_seconds_ > 0) {
        char buffer[16];
        snprintf(buffer, sizeof(buffer), "%02d:%02d", sleep_music_remaining_seconds_ / 60,
                 sleep_music_remaining_seconds_ % 60);
        lv_label_set_text(sleep_music_countdown_label_, buffer);
    } else {
        lv_label_set_text(sleep_music_countdown_label_, "--:--");
    }

    lv_label_set_text(sleep_music_action_label_, sleep_music_playing_ ? "停止播放" : "开始播放");
    UpdateSleepMusicSelectionStyles();
}

void UiLcdDisplay::UpdateSleepMusicSelectionStyles() {
    for (int i = 0; i < static_cast<int>(sleep_music_mode_buttons_.size()); ++i) {
        const bool selected = i == sleep_music_selected_mode_index_;
        lv_obj_set_style_bg_color(sleep_music_mode_buttons_[i],
                                  selected ? lv_color_hex(0xDFF1FF) : lv_color_hex(0xF1F3F5), 0);
        lv_obj_set_style_text_color(sleep_music_mode_labels_[i],
                                    selected ? lv_color_hex(0x0C63E7) : lv_color_black(), 0);
    }

    for (int i = 0; i < static_cast<int>(sleep_music_duration_buttons_.size()); ++i) {
        const bool selected = i == sleep_music_selected_duration_index_;
        lv_obj_set_style_bg_color(sleep_music_duration_buttons_[i],
                                  selected ? lv_color_hex(0xE8F5E9) : lv_color_hex(0xF1F3F5), 0);
        lv_obj_set_style_text_color(sleep_music_duration_labels_[i],
                                    selected ? lv_color_hex(0x2E7D32) : lv_color_black(), 0);
    }

    lv_obj_set_style_bg_color(sleep_music_action_button_,
                              sleep_music_playing_ ? lv_color_hex(0xE53935) : lv_color_hex(0x2F80ED), 0);
    lv_obj_set_style_text_color(sleep_music_action_label_, lv_color_white(), 0);
}

void UiLcdDisplay::SelectSleepMusicMode(int index) {
    if (index < 0 || index >= static_cast<int>(sleep_music_mode_buttons_.size()) ||
        sleep_music_selected_mode_index_ == index) {
        return;
    }

    sleep_music_selected_mode_index_ = index;
    RefreshSleepMusicScreen();
}

void UiLcdDisplay::SelectSleepMusicDuration(int index) {
    if (index < 0 || index >= static_cast<int>(sleep_music_duration_buttons_.size()) ||
        sleep_music_selected_duration_index_ == index) {
        return;
    }

    sleep_music_selected_duration_index_ = index;
    RefreshSleepMusicScreen();
}

void UiLcdDisplay::ToggleSleepMusicPlayback() {
    if (sleep_music_playing_) {
        StopSleepMusicPlayback(true);
        return;
    }
    StartSleepMusicPlayback();
}

void UiLcdDisplay::StartSleepMusicPlayback() {
    if (sleep_music_playing_) {
        return;
    }

    sleep_music_playing_ = true;
    sleep_music_remaining_seconds_ =
        GetSleepMusicDurationSeconds(sleep_music_selected_duration_index_);
    RefreshSleepMusicScreen();
    StartSleepMusicTimer();
    HandleSleepMusicTick();
}

void UiLcdDisplay::StopSleepMusicPlayback(bool notify) {
    if (!sleep_music_playing_ && sleep_music_remaining_seconds_ == 0) {
        return;
    }

    StopSleepMusicTimer();
    sleep_music_playing_ = false;
    sleep_music_remaining_seconds_ = 0;
    RefreshSleepMusicScreen();

    if (notify) {
        ShowNotification("助眠音乐已停止", 1200);
    }
}

void UiLcdDisplay::StartSleepMusicTimer() {
    if (sleep_music_timer_ == nullptr) {
        return;
    }

    esp_err_t err = esp_timer_stop(sleep_music_timer_);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGW(kSleepMusicTag, "Stop sleep music timer failed: %s", esp_err_to_name(err));
    }
    ESP_ERROR_CHECK(esp_timer_start_periodic(sleep_music_timer_, 1000 * 1000));
}

void UiLcdDisplay::StopSleepMusicTimer() {
    if (sleep_music_timer_ == nullptr) {
        return;
    }

    esp_err_t err = esp_timer_stop(sleep_music_timer_);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGW(kSleepMusicTag, "Stop sleep music timer failed: %s", esp_err_to_name(err));
    }
}

void UiLcdDisplay::HandleSleepMusicTick() {
    if (!sleep_music_playing_) {
        return;
    }

    auto pcm = GenerateSleepMusicChunk(sleep_music_selected_mode_index_,
                                       sleep_music_selected_duration_index_,
                                       sleep_music_remaining_seconds_, sleep_music_noise_seed_);
    Application::GetInstance().GetAudioService().PlayPcm(std::move(pcm), kSleepMusicSampleRate);

    --sleep_music_remaining_seconds_;
    if (sleep_music_remaining_seconds_ <= 0) {
        StopSleepMusicTimer();
        sleep_music_playing_ = false;
        sleep_music_remaining_seconds_ = 0;
        RefreshSleepMusicScreen();
        ShowNotification("助眠音乐已结束", 1500);
        return;
    }

    RefreshSleepMusicScreen();
}
