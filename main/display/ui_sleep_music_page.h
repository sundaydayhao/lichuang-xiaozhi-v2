#ifndef UI_SLEEP_MUSIC_PAGE_H
#define UI_SLEEP_MUSIC_PAGE_H

#include "ui_page_common.h"

#include <array>

struct SleepMusicOptionData {
    UiLcdDisplay* display = nullptr;
    int index = 0;
};

struct UiSleepMusicPage {
    lv_obj_t* sleep_music_screen_ = nullptr;
    lv_obj_t* sleep_music_title_label_ = nullptr;
    lv_obj_t* sleep_music_status_label_ = nullptr;
    std::array<lv_obj_t*, 3> sleep_music_mode_buttons_ = {};
    std::array<lv_obj_t*, 3> sleep_music_mode_labels_ = {};
    std::array<SleepMusicOptionData, 3> sleep_music_mode_button_data_ = {};
    std::array<lv_obj_t*, 3> sleep_music_duration_buttons_ = {};
    std::array<lv_obj_t*, 3> sleep_music_duration_labels_ = {};
    std::array<SleepMusicOptionData, 3> sleep_music_duration_button_data_ = {};
    lv_obj_t* sleep_music_countdown_label_ = nullptr;
    lv_obj_t* sleep_music_action_button_ = nullptr;
    lv_obj_t* sleep_music_action_label_ = nullptr;
    int sleep_music_selected_mode_index_ = 0;
    int sleep_music_selected_duration_index_ = 0;
    bool sleep_music_playing_ = false;
    int sleep_music_remaining_seconds_ = 0;
    uint32_t sleep_music_noise_seed_ = 0x13572468u;
    BackButtonData sleep_music_back_button_data_ = {};
};

#endif // UI_SLEEP_MUSIC_PAGE_H
