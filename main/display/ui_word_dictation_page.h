#ifndef UI_WORD_DICTATION_PAGE_H
#define UI_WORD_DICTATION_PAGE_H

#include "ui_page_common.h"

#include <string>
#include <vector>

struct WordDictationWord {
    std::string text;
    std::string note;
    bool selected = true;
};

struct WordDictationStage {
    std::string name;
    std::vector<WordDictationWord> words;
};

struct WordDictationTextbook {
    std::string category;
    std::string grade;
    std::string version;
    std::string subtitle;
    std::string cover_title;
    uint32_t cover_color = 0;
    std::vector<WordDictationStage> stages;
};

struct WordDictationListItemData {
    UiLcdDisplay* display = nullptr;
    int index = 0;
};

struct WordDictationActionData {
    UiLcdDisplay* display = nullptr;
    int action = 0;
};

struct WordDictationSettings {
    bool random_order = false;
    int interval_seconds = 10;
    int repeat_count = 1;
    bool bilingual_mode = false;
};

enum class WordDictationView {
    kHidden = 0,
    kTextbooks,
    kStages,
    kWords,
    kSummary,
    kSettings,
    kCountdown,
    kRunning,
};

struct UiWordDictationPage {
    lv_obj_t* word_dictation_screen_ = nullptr;
    lv_obj_t* word_dictation_title_label_ = nullptr;
    lv_obj_t* word_dictation_content_ = nullptr;
    lv_obj_t* word_dictation_back_button_ = nullptr;
    lv_obj_t* word_dictation_running_count_label_ = nullptr;
    lv_obj_t* word_dictation_running_hint_label_ = nullptr;
    WordDictationView word_dictation_view_ = WordDictationView::kHidden;
    WordDictationSettings word_dictation_settings_ = {};
    std::vector<WordDictationTextbook> word_dictation_textbooks_;
    std::vector<WordDictationListItemData> word_dictation_list_item_data_;
    std::vector<WordDictationActionData> word_dictation_action_data_;
    std::vector<int> word_dictation_play_order_;
    int word_dictation_selected_textbook_index_ = -1;
    int word_dictation_selected_stage_index_ = -1;
    int word_dictation_countdown_value_ = 3;
    size_t word_dictation_current_play_index_ = 0;
    BackButtonData word_dictation_back_button_data_ = {};
};

#endif // UI_WORD_DICTATION_PAGE_H
