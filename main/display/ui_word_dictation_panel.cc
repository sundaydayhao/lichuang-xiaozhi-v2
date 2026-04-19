#include "ui_display.h"

#include "application.h"
#include "assets/lang_config.h"
#include "lvgl_theme.h"

#include <array>
#include <algorithm>
#include <cstdio>
#include <random>
#include <string>
#include <vector>

#include <esp_err.h>

namespace {

constexpr int kWordDictationBlue = 0x2F80FF;
constexpr int kWordDictationLightBlue = 0xEAF3FF;
constexpr int kWordDictationCardBorder = 0xDCE7F9;
constexpr int kWordDictationMutedText = 0x7E8AA0;
constexpr int kWordDictationTitleText = 0x1F2D3D;
constexpr int kWordDictationDangerText = 0x8FA2C2;

constexpr int kWordDictationActionSelectAll = 1;
constexpr int kWordDictationActionClearSelection = 2;
constexpr int kWordDictationActionConfirmSelection = 3;
constexpr int kWordDictationActionOpenSettings = 4;
constexpr int kWordDictationActionStart = 5;
constexpr int kWordDictationActionSaveSettings = 6;
constexpr int kWordDictationActionToggleOrder = 7;
constexpr int kWordDictationActionToggleInterval = 8;
constexpr int kWordDictationActionToggleRepeat = 9;
constexpr int kWordDictationActionToggleMode = 10;

std::string_view GetDigitSound(int value) {
    switch (value) {
    case 1:
        return Lang::Sounds::OGG_1;
    case 2:
        return Lang::Sounds::OGG_2;
    case 3:
        return Lang::Sounds::OGG_3;
    default:
        return {};
    }
}

lv_obj_t* CreatePageButton(lv_obj_t* parent, int width, int height) {
    lv_obj_t* button = lv_btn_create(parent);
    lv_obj_set_size(button, width, height);
    lv_obj_set_style_radius(button, 22, 0);
    lv_obj_set_style_bg_color(button, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(button, lv_color_hex(kWordDictationLightBlue), LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER, LV_STATE_PRESSED);
    lv_obj_set_style_border_width(button, 1, 0);
    lv_obj_set_style_border_color(button, lv_color_hex(kWordDictationCardBorder), 0);
    lv_obj_set_style_shadow_width(button, 0, 0);
    lv_obj_set_style_pad_all(button, 0, 0);
    lv_obj_clear_flag(button, LV_OBJ_FLAG_SCROLLABLE);
    return button;
}

lv_obj_t* CreatePrimaryButton(lv_obj_t* parent, int width, int height,
                              const char* text, const lv_font_t* font) {
    lv_obj_t* button = lv_btn_create(parent);
    lv_obj_set_size(button, width, height);
    lv_obj_set_style_radius(button, height / 2, 0);
    lv_obj_set_style_bg_color(button, lv_color_hex(kWordDictationBlue), 0);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(button, lv_color_hex(0x2367D1), LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER, LV_STATE_PRESSED);
    lv_obj_set_style_border_width(button, 0, 0);
    lv_obj_set_style_shadow_width(button, 0, 0);
    lv_obj_clear_flag(button, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* label = lv_label_create(button);
    lv_label_set_text(label, text);
    lv_obj_center(label);
    lv_obj_set_style_text_color(label, lv_color_white(), 0);
    lv_obj_set_style_text_font(label, font, 0);
    return button;
}

lv_obj_t* CreateSecondaryButton(lv_obj_t* parent, int width, int height,
                                const char* text, const lv_font_t* font) {
    lv_obj_t* button = lv_btn_create(parent);
    lv_obj_set_size(button, width, height);
    lv_obj_set_style_radius(button, height / 2, 0);
    lv_obj_set_style_bg_color(button, lv_color_hex(0xF4F8FF), 0);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(button, lv_color_hex(0xE8F0FD), LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER, LV_STATE_PRESSED);
    lv_obj_set_style_border_width(button, 1, 0);
    lv_obj_set_style_border_color(button, lv_color_hex(kWordDictationCardBorder), 0);
    lv_obj_set_style_shadow_width(button, 0, 0);
    lv_obj_clear_flag(button, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* label = lv_label_create(button);
    lv_label_set_text(label, text);
    lv_obj_center(label);
    lv_obj_set_style_text_color(label, lv_color_hex(kWordDictationTitleText), 0);
    lv_obj_set_style_text_font(label, font, 0);
    return button;
}

lv_obj_t* GetButtonLabel(lv_obj_t* button) {
    return (button != nullptr && lv_obj_get_child_cnt(button) > 0) ? lv_obj_get_child(button, 0) : nullptr;
}

} // namespace

void UiLcdDisplay::InitializeWordDictationSupport() {
    if (!word_dictation_textbooks_.empty()) {
        return;
    }

    auto make_words = [](std::initializer_list<std::pair<const char*, const char*>> entries) {
        std::vector<WordDictationWord> words;
        words.reserve(entries.size());
        for (const auto& entry : entries) {
            WordDictationWord word;
            word.text = entry.first;
            word.note = entry.second;
            word.selected = true;
            words.push_back(std::move(word));
        }
        return words;
    };

    WordDictationTextbook chinese_book;
    chinese_book.category = "语文生字词";
    chinese_book.grade = "五年级";
    chinese_book.version = "人教编版";
    chinese_book.subtitle = "五年级下";
    chinese_book.cover_title = "语文";
    chinese_book.cover_color = 0xFFB457;
    chinese_book.stages = {
        WordDictationStage{
            .name = "第一单元",
            .words = make_words({
                {"蝴蝶", "常见昆虫"},
                {"耕耘", "辛勤劳作"},
                {"锄头", "农具"},
                {"桑叶", "养蚕相关"},
                {"蜻蜓", "夏日昆虫"},
                {"樱桃", "水果名称"},
                {"倭瓜", "蔬菜名称"},
                {"闪烁", "光亮忽明忽暗"},
            }),
        },
        WordDictationStage{
            .name = "第二单元",
            .words = make_words({
                {"胸膛", "身体部位"},
                {"妒忌", "心理活动"},
                {"神机妙算", "形容计策高明"},
                {"委托", "托付他人"},
                {"呐喊", "大声呼喊"},
                {"擂鼓", "敲击战鼓"},
                {"探听", "打听消息"},
                {"军令状", "立下承诺"},
            }),
        },
        WordDictationStage{
            .name = "期中",
            .words = make_words({
                {"慈祥", "和善安详"},
                {"眷恋", "深切留恋"},
                {"辽阔", "广阔无边"},
                {"秉性", "性情品格"},
                {"衰老", "逐渐老去"},
                {"郑重", "严肃认真"},
                {"手绢", "手帕"},
                {"华侨", "侨居海外的人"},
            }),
        },
    };

    WordDictationTextbook english_book;
    english_book.category = "英语单词";
    english_book.grade = "九年级";
    english_book.version = "牛津上海版";
    english_book.subtitle = "九年级下";
    english_book.cover_title = "English";
    english_book.cover_color = 0xFF6F7B;
    english_book.stages = {
        WordDictationStage{
            .name = "Module 1",
            .words = make_words({
                {"atmosphere", "围绕地球的大气"},
                {"global", "全球的；全世界的"},
                {"global warming", "全球变暖"},
                {"greenhouse", "温室"},
                {"greenhouse effect", "温室效应"},
                {"habit", "习惯"},
                {"energy", "能源；精力"},
                {"electricity", "电"},
                {"plastic", "塑料"},
                {"recycle", "回收利用"},
                {"reduce", "减少"},
                {"environment", "环境"},
            }),
        },
        WordDictationStage{
            .name = "阶段1",
            .words = make_words({
                {"pollution", "污染"},
                {"factory", "工厂"},
                {"protect", "保护"},
                {"natural", "自然的"},
                {"waste", "浪费；废物"},
                {"resource", "资源"},
                {"serious", "严重的"},
                {"temperature", "温度"},
            }),
        },
        WordDictationStage{
            .name = "期中",
            .words = make_words({
                {"chemical", "化学的"},
                {"product", "产品"},
                {"transport", "运输"},
                {"scientist", "科学家"},
                {"research", "研究"},
                {"solution", "解决办法"},
                {"citizen", "市民"},
                {"public", "公共的"},
            }),
        },
        WordDictationStage{
            .name = "Module 2",
            .words = make_words({
                {"disappear", "消失"},
                {"creature", "生物"},
                {"wildlife", "野生生物"},
                {"protective", "保护性的"},
                {"balance", "平衡"},
                {"forest", "森林"},
                {"ocean", "海洋"},
                {"planet", "行星；地球"},
            }),
        },
        WordDictationStage{
            .name = "阶段2",
            .words = make_words({
                {"effective", "有效的"},
                {"action", "行动"},
                {"community", "社区"},
                {"responsibility", "责任"},
                {"future", "未来"},
                {"challenge", "挑战"},
                {"encourage", "鼓励"},
                {"support", "支持"},
            }),
        },
    };

    word_dictation_textbooks_.push_back(std::move(chinese_book));
    word_dictation_textbooks_.push_back(std::move(english_book));

    esp_timer_create_args_t countdown_timer_args = {
        .callback = [](void* arg) {
            auto* display = static_cast<UiLcdDisplay*>(arg);
            Application::GetInstance().Schedule([display]() {
                display->AdvanceWordDictationCountdown();
            });
        },
        .arg = this,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "wd_countdown",
        .skip_unhandled_events = false,
    };
    ESP_ERROR_CHECK(esp_timer_create(&countdown_timer_args, &word_dictation_countdown_timer_));

    esp_timer_create_args_t progress_timer_args = {
        .callback = [](void* arg) {
            auto* display = static_cast<UiLcdDisplay*>(arg);
            Application::GetInstance().Schedule([display]() {
                display->AdvanceWordDictationPlayback();
            });
        },
        .arg = this,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "wd_progress",
        .skip_unhandled_events = false,
    };
    ESP_ERROR_CHECK(esp_timer_create(&progress_timer_args, &word_dictation_progress_timer_));
}

void UiLcdDisplay::DeinitializeWordDictationSupport() {
    StopWordDictationPlayback();

    if (word_dictation_countdown_timer_ != nullptr) {
        esp_timer_delete(word_dictation_countdown_timer_);
        word_dictation_countdown_timer_ = nullptr;
    }
    if (word_dictation_progress_timer_ != nullptr) {
        esp_timer_delete(word_dictation_progress_timer_);
        word_dictation_progress_timer_ = nullptr;
    }

    DestroyWordDictationScreen();
}

void UiLcdDisplay::DestroyWordDictationScreen() {
    DisplayLockGuard lock(this);
    if (word_dictation_screen_ != nullptr) {
        lv_obj_del(word_dictation_screen_);
        word_dictation_screen_ = nullptr;
    }

    word_dictation_title_label_ = nullptr;
    word_dictation_content_ = nullptr;
    word_dictation_back_button_ = nullptr;
    word_dictation_running_count_label_ = nullptr;
    word_dictation_running_hint_label_ = nullptr;
}

void UiLcdDisplay::UpdateWordDictationBackButton(bool close_mode) {
    if (word_dictation_back_button_ == nullptr) {
        return;
    }

    lv_obj_t* label = GetButtonLabel(word_dictation_back_button_);
    if (label == nullptr) {
        return;
    }

    if (close_mode) {
        lv_label_set_text(label, "x");
        lv_obj_align(label, LV_ALIGN_CENTER, 0, -1);
    } else {
        lv_label_set_text(label, "<");
        lv_obj_align(label, LV_ALIGN_CENTER, 6, -1);
    }
}

int UiLcdDisplay::GetWordDictationSelectedWordCount() const {
    if (word_dictation_selected_textbook_index_ < 0 || word_dictation_selected_stage_index_ < 0) {
        return 0;
    }

    const auto& stage =
        word_dictation_textbooks_[word_dictation_selected_textbook_index_].stages[word_dictation_selected_stage_index_];
    return static_cast<int>(std::count_if(stage.words.begin(), stage.words.end(),
                                          [](const WordDictationWord& word) { return word.selected; }));
}

int UiLcdDisplay::GetWordDictationPlaybackStepCount() const {
    return static_cast<int>(word_dictation_play_order_.size());
}

std::string UiLcdDisplay::GetWordDictationStageTitle() const {
    if (word_dictation_selected_textbook_index_ < 0 || word_dictation_selected_stage_index_ < 0) {
        return "听写";
    }

    const auto& textbook = word_dictation_textbooks_[word_dictation_selected_textbook_index_];
    const auto& stage = textbook.stages[word_dictation_selected_stage_index_];
    if (word_dictation_view_ == WordDictationView::kStages) {
        return textbook.subtitle + "（" + textbook.version + "）";
    }
    return stage.name;
}

std::string UiLcdDisplay::GetWordDictationSummaryText() const {
    std::string text = "听写顺序: ";
    text += word_dictation_settings_.random_order ? "随机听写" : "顺序听写";
    text += "\n听写间隔: ";
    text += std::to_string(word_dictation_settings_.interval_seconds);
    text += "s";
    text += "\n播报次数: ";
    text += std::to_string(word_dictation_settings_.repeat_count);
    text += "次";
    text += "\n播报模式: ";
    text += word_dictation_settings_.bilingual_mode ? "中英" : "英文";
    text += "\n已选单词: ";
    text += std::to_string(GetWordDictationSelectedWordCount());
    text += "个";
    return text;
}

void UiLcdDisplay::RebuildWordDictationPlayOrder() {
    word_dictation_play_order_.clear();
    word_dictation_current_play_index_ = 0;

    if (word_dictation_selected_textbook_index_ < 0 || word_dictation_selected_stage_index_ < 0) {
        return;
    }

    const auto& stage =
        word_dictation_textbooks_[word_dictation_selected_textbook_index_].stages[word_dictation_selected_stage_index_];

    std::vector<int> selected_indices;
    selected_indices.reserve(stage.words.size());
    for (size_t i = 0; i < stage.words.size(); ++i) {
        if (stage.words[i].selected) {
            selected_indices.push_back(static_cast<int>(i));
        }
    }
    if (selected_indices.empty()) {
        return;
    }

    std::random_device rd;
    std::mt19937 rng(rd());
    for (int repeat = 0; repeat < word_dictation_settings_.repeat_count; ++repeat) {
        auto current_round = selected_indices;
        if (word_dictation_settings_.random_order) {
            std::shuffle(current_round.begin(), current_round.end(), rng);
        }
        word_dictation_play_order_.insert(word_dictation_play_order_.end(), current_round.begin(), current_round.end());
    }
}

void UiLcdDisplay::ShowWordDictationTextbookScreen() {
    InitializeWordDictationSupport();

    auto* lvgl_theme = static_cast<LvglTheme*>(GetTheme());
    auto* text_font = lvgl_theme->text_font()->font();

    {
        DisplayLockGuard lock(this);
        if (word_dictation_screen_ == nullptr) {
            word_dictation_screen_ = lv_obj_create(nullptr);
            lv_obj_set_style_bg_color(word_dictation_screen_, lv_color_white(), 0);
            lv_obj_set_style_border_width(word_dictation_screen_, 0, 0);
            lv_obj_set_style_pad_all(word_dictation_screen_, 0, 0);
            lv_obj_set_scrollbar_mode(word_dictation_screen_, LV_SCROLLBAR_MODE_OFF);

            word_dictation_title_label_ = lv_label_create(word_dictation_screen_);
            lv_obj_align(word_dictation_title_label_, LV_ALIGN_TOP_MID, 0, 40);
            lv_obj_set_style_text_font(word_dictation_title_label_, text_font, 0);
            lv_obj_set_style_text_color(word_dictation_title_label_, lv_color_hex(kWordDictationTitleText), 0);

            word_dictation_content_ = lv_obj_create(word_dictation_screen_);
            lv_obj_set_size(word_dictation_content_, LV_HOR_RES, LV_VER_RES - 84);
            lv_obj_align(word_dictation_content_, LV_ALIGN_TOP_MID, 0, 84);
            lv_obj_set_style_bg_color(word_dictation_content_, lv_color_white(), 0);
            lv_obj_set_style_border_width(word_dictation_content_, 0, 0);
            lv_obj_set_style_pad_left(word_dictation_content_, 16, 0);
            lv_obj_set_style_pad_right(word_dictation_content_, 16, 0);
            lv_obj_set_style_pad_top(word_dictation_content_, 0, 0);
            lv_obj_set_style_pad_bottom(word_dictation_content_, 20, 0);
            lv_obj_set_scroll_dir(word_dictation_content_, LV_DIR_VER);
            lv_obj_set_scrollbar_mode(word_dictation_content_, LV_SCROLLBAR_MODE_OFF);

            word_dictation_back_button_data_ = BackButtonData{this, 4};
            word_dictation_back_button_ = CreateBackButton(word_dictation_screen_, &word_dictation_back_button_data_);
        }

        StopWordDictationPlayback();
        current_feature_option_id_ = 8;
        word_dictation_view_ = WordDictationView::kTextbooks;
        word_dictation_running_count_label_ = nullptr;
        word_dictation_running_hint_label_ = nullptr;
        word_dictation_list_item_data_.clear();
        word_dictation_action_data_.clear();
        lv_obj_clean(word_dictation_content_);
        lv_label_set_text(word_dictation_title_label_, "听写");
        lv_obj_clear_flag(word_dictation_back_button_, LV_OBJ_FLAG_HIDDEN);
        UpdateWordDictationBackButton(false);

        int y = 0;
        word_dictation_list_item_data_.resize(word_dictation_textbooks_.size());
        for (size_t i = 0; i < word_dictation_textbooks_.size(); ++i) {
            const auto& textbook = word_dictation_textbooks_[i];

            lv_obj_t* section_label = lv_label_create(word_dictation_content_);
            lv_label_set_text_fmt(section_label, "%s", textbook.category.c_str());
            lv_obj_set_pos(section_label, 0, y);
            lv_obj_set_style_text_font(section_label, text_font, 0);
            lv_obj_set_style_text_color(section_label, lv_color_hex(kWordDictationTitleText), 0);

            lv_obj_t* grade_label = lv_label_create(word_dictation_content_);
            lv_label_set_text_fmt(grade_label, "%s", textbook.grade.c_str());
            lv_obj_set_style_text_font(grade_label, text_font, 0);
            lv_obj_set_style_text_color(grade_label, lv_color_hex(kWordDictationMutedText), 0);
            lv_obj_align_to(grade_label, section_label, LV_ALIGN_OUT_RIGHT_MID, 104, 0);
            y += 14;

            lv_obj_t* item = CreatePageButton(word_dictation_content_, LV_HOR_RES - 32, 92);
            lv_obj_set_pos(item, 0, y + 10);
            word_dictation_list_item_data_[i] = WordDictationListItemData{this, static_cast<int>(i)};
            lv_obj_set_user_data(item, &word_dictation_list_item_data_[i]);
            lv_obj_add_event_cb(item, OnWordDictationListItemClicked, LV_EVENT_CLICKED, nullptr);

            lv_obj_t* cover = lv_obj_create(item);
            lv_obj_set_size(cover, 64, 64);
            lv_obj_align(cover, LV_ALIGN_LEFT_MID, 14, 0);
            lv_obj_set_style_radius(cover, 16, 0);
            lv_obj_set_style_bg_color(cover, lv_color_hex(textbook.cover_color), 0);
            lv_obj_set_style_bg_opa(cover, LV_OPA_COVER, 0);
            lv_obj_set_style_border_width(cover, 0, 0);
            lv_obj_clear_flag(cover, LV_OBJ_FLAG_CLICKABLE);

            lv_obj_t* cover_label = lv_label_create(cover);
            lv_label_set_text(cover_label, textbook.cover_title.c_str());
            lv_obj_center(cover_label);
            lv_obj_set_style_text_font(cover_label, text_font, 0);
            lv_obj_set_style_text_color(cover_label, lv_color_white(), 0);

            lv_obj_t* version_label = lv_label_create(item);
            lv_label_set_text(version_label, textbook.version.c_str());
            lv_obj_set_pos(version_label, 92, 24);
            lv_obj_set_style_text_font(version_label, text_font, 0);
            lv_obj_set_style_text_color(version_label, lv_color_hex(kWordDictationTitleText), 0);

            lv_obj_t* subtitle_label = lv_label_create(item);
            lv_label_set_text(subtitle_label, textbook.subtitle.c_str());
            lv_obj_set_pos(subtitle_label, 92, 50);
            lv_obj_set_style_text_font(subtitle_label, text_font, 0);
            lv_obj_set_style_text_color(subtitle_label, lv_color_hex(kWordDictationMutedText), 0);

            y += 118;
        }
    }

    LoadScreen(word_dictation_screen_);
}

void UiLcdDisplay::ShowWordDictationStageScreen() {
    if (word_dictation_selected_textbook_index_ < 0) {
        ShowWordDictationTextbookScreen();
        return;
    }

    auto* lvgl_theme = static_cast<LvglTheme*>(GetTheme());
    auto* text_font = lvgl_theme->text_font()->font();
    const auto& textbook = word_dictation_textbooks_[word_dictation_selected_textbook_index_];

    {
        DisplayLockGuard lock(this);
        word_dictation_view_ = WordDictationView::kStages;
        word_dictation_running_count_label_ = nullptr;
        word_dictation_running_hint_label_ = nullptr;
        word_dictation_list_item_data_.clear();
        word_dictation_action_data_.clear();
        lv_obj_clean(word_dictation_content_);
        lv_label_set_text(word_dictation_title_label_, (textbook.subtitle + "（" + textbook.version + "）").c_str());
        lv_obj_clear_flag(word_dictation_back_button_, LV_OBJ_FLAG_HIDDEN);
        UpdateWordDictationBackButton(false);

        word_dictation_list_item_data_.resize(textbook.stages.size());
        int y = 6;
        for (size_t i = 0; i < textbook.stages.size(); ++i) {
            lv_obj_t* item = CreatePageButton(word_dictation_content_, LV_HOR_RES - 32, 56);
            lv_obj_set_pos(item, 0, y);
            word_dictation_list_item_data_[i] = WordDictationListItemData{this, static_cast<int>(i)};
            lv_obj_set_user_data(item, &word_dictation_list_item_data_[i]);
            lv_obj_add_event_cb(item, OnWordDictationListItemClicked, LV_EVENT_CLICKED, nullptr);

            lv_obj_t* label = lv_label_create(item);
            lv_label_set_text(label, textbook.stages[i].name.c_str());
            lv_obj_align(label, LV_ALIGN_LEFT_MID, 18, 0);
            lv_obj_set_style_text_font(label, text_font, 0);
            lv_obj_set_style_text_color(label, lv_color_hex(kWordDictationTitleText), 0);

            y += 68;
        }
    }

    LoadScreen(word_dictation_screen_);
}

void UiLcdDisplay::ShowWordDictationWordScreen() {
    if (word_dictation_selected_textbook_index_ < 0 || word_dictation_selected_stage_index_ < 0) {
        ShowWordDictationStageScreen();
        return;
    }

    auto* lvgl_theme = static_cast<LvglTheme*>(GetTheme());
    auto* text_font = lvgl_theme->text_font()->font();
    const auto& stage =
        word_dictation_textbooks_[word_dictation_selected_textbook_index_].stages[word_dictation_selected_stage_index_];

    {
        DisplayLockGuard lock(this);
        word_dictation_view_ = WordDictationView::kWords;
        word_dictation_running_count_label_ = nullptr;
        word_dictation_running_hint_label_ = nullptr;
        word_dictation_list_item_data_.clear();
        word_dictation_action_data_.clear();
        lv_obj_clean(word_dictation_content_);
        lv_label_set_text(word_dictation_title_label_, stage.name.c_str());
        lv_obj_clear_flag(word_dictation_back_button_, LV_OBJ_FLAG_HIDDEN);
        UpdateWordDictationBackButton(false);

        word_dictation_action_data_.resize(3);

        lv_obj_t* select_all_btn = lv_btn_create(word_dictation_content_);
        lv_obj_set_size(select_all_btn, 78, 32);
        lv_obj_set_pos(select_all_btn, 0, 6);
        lv_obj_set_style_bg_opa(select_all_btn, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(select_all_btn, 0, 0);
        lv_obj_set_style_shadow_width(select_all_btn, 0, 0);
        word_dictation_action_data_[0] = WordDictationActionData{this, kWordDictationActionSelectAll};
        lv_obj_set_user_data(select_all_btn, &word_dictation_action_data_[0]);
        lv_obj_add_event_cb(select_all_btn, OnWordDictationActionClicked, LV_EVENT_CLICKED, nullptr);
        lv_obj_t* select_all_label = lv_label_create(select_all_btn);
        lv_label_set_text(select_all_label, "全选");
        lv_obj_center(select_all_label);
        lv_obj_set_style_text_color(select_all_label, lv_color_hex(kWordDictationBlue), 0);
        lv_obj_set_style_text_font(select_all_label, text_font, 0);

        lv_obj_t* clear_btn = lv_btn_create(word_dictation_content_);
        lv_obj_set_size(clear_btn, 112, 32);
        lv_obj_set_pos(clear_btn, LV_HOR_RES - 32 - 112, 6);
        lv_obj_set_style_bg_opa(clear_btn, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(clear_btn, 0, 0);
        lv_obj_set_style_shadow_width(clear_btn, 0, 0);
        word_dictation_action_data_[1] = WordDictationActionData{this, kWordDictationActionClearSelection};
        lv_obj_set_user_data(clear_btn, &word_dictation_action_data_[1]);
        lv_obj_add_event_cb(clear_btn, OnWordDictationActionClicked, LV_EVENT_CLICKED, nullptr);
        lv_obj_t* clear_label = lv_label_create(clear_btn);
        lv_label_set_text(clear_label, "取消选择");
        lv_obj_center(clear_label);
        lv_obj_set_style_text_color(clear_label, lv_color_hex(kWordDictationDangerText), 0);
        lv_obj_set_style_text_font(clear_label, text_font, 0);

        word_dictation_list_item_data_.resize(stage.words.size());
        int y = 46;
        for (size_t i = 0; i < stage.words.size(); ++i) {
            const auto& word = stage.words[i];
            lv_obj_t* item = CreatePageButton(word_dictation_content_, LV_HOR_RES - 32, 58);
            lv_obj_set_pos(item, 0, y);
            word_dictation_list_item_data_[i] = WordDictationListItemData{this, static_cast<int>(i)};
            lv_obj_set_user_data(item, &word_dictation_list_item_data_[i]);
            lv_obj_add_event_cb(item, OnWordDictationListItemClicked, LV_EVENT_CLICKED, nullptr);

            lv_obj_t* mark = lv_obj_create(item);
            lv_obj_set_size(mark, 24, 24);
            lv_obj_align(mark, LV_ALIGN_LEFT_MID, 14, 0);
            lv_obj_set_style_radius(mark, LV_RADIUS_CIRCLE, 0);
            lv_obj_set_style_bg_color(mark, word.selected ? lv_color_hex(kWordDictationBlue) : lv_color_hex(0xD9E2F2), 0);
            lv_obj_set_style_bg_opa(mark, LV_OPA_COVER, 0);
            lv_obj_set_style_border_width(mark, 0, 0);
            lv_obj_clear_flag(mark, LV_OBJ_FLAG_CLICKABLE);

            lv_obj_t* mark_label = lv_label_create(mark);
            lv_label_set_text(mark_label, word.selected ? "v" : "");
            lv_obj_center(mark_label);
            lv_obj_set_style_text_color(mark_label, lv_color_white(), 0);
            lv_obj_set_style_text_font(mark_label, text_font, 0);

            lv_obj_t* word_label = lv_label_create(item);
            lv_label_set_text(word_label, word.text.c_str());
            lv_obj_set_pos(word_label, 48, 10);
            lv_obj_set_style_text_font(word_label, text_font, 0);
            lv_obj_set_style_text_color(word_label, lv_color_hex(kWordDictationTitleText), 0);

            if (!word.note.empty()) {
                lv_obj_t* note_label = lv_label_create(item);
                lv_label_set_text(note_label, word.note.c_str());
                lv_obj_set_pos(note_label, 48, 32);
                lv_obj_set_style_text_font(note_label, text_font, 0);
                lv_obj_set_style_text_color(note_label, lv_color_hex(kWordDictationMutedText), 0);
            }

            y += 68;
        }

        const int selected_count = GetWordDictationSelectedWordCount();
        char confirm_text[48];
        snprintf(confirm_text, sizeof(confirm_text), "确认选择 (%d)", selected_count);
        lv_obj_t* confirm_btn = CreatePrimaryButton(word_dictation_content_, LV_HOR_RES - 44, 44, confirm_text, text_font);
        lv_obj_set_pos(confirm_btn, 6, y + 10);
        word_dictation_action_data_[2] = WordDictationActionData{this, kWordDictationActionConfirmSelection};
        lv_obj_set_user_data(confirm_btn, &word_dictation_action_data_[2]);
        lv_obj_add_event_cb(confirm_btn, OnWordDictationActionClicked, LV_EVENT_CLICKED, nullptr);
    }

    LoadScreen(word_dictation_screen_);
}

void UiLcdDisplay::ShowWordDictationSummaryScreen() {
    if (GetWordDictationSelectedWordCount() <= 0) {
        ShowNotification("请至少选择一个单词", 1200);
        ShowWordDictationWordScreen();
        return;
    }

    auto* lvgl_theme = static_cast<LvglTheme*>(GetTheme());
    auto* text_font = lvgl_theme->text_font()->font();

    {
        DisplayLockGuard lock(this);
        word_dictation_view_ = WordDictationView::kSummary;
        word_dictation_running_count_label_ = nullptr;
        word_dictation_running_hint_label_ = nullptr;
        word_dictation_action_data_.clear();
        word_dictation_list_item_data_.clear();
        lv_obj_clean(word_dictation_content_);
        lv_label_set_text(word_dictation_title_label_, GetWordDictationStageTitle().c_str());
        lv_obj_clear_flag(word_dictation_back_button_, LV_OBJ_FLAG_HIDDEN);
        UpdateWordDictationBackButton(false);

    lv_obj_t* card = lv_obj_create(word_dictation_content_);
    lv_obj_set_size(card, LV_HOR_RES - 44, 170);
    lv_obj_set_pos(card, 6, 24);
    lv_obj_set_style_radius(card, 22, 0);
    lv_obj_set_style_bg_color(card, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_border_color(card, lv_color_hex(kWordDictationCardBorder), 0);
    lv_obj_set_style_pad_all(card, 18, 0);
    lv_obj_set_style_shadow_width(card, 0, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* title = lv_label_create(card);
    lv_label_set_text(title, "当前听写设置");
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_text_font(title, text_font, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(kWordDictationTitleText), 0);

    lv_obj_t* summary = lv_label_create(card);
    lv_label_set_text(summary, GetWordDictationSummaryText().c_str());
    lv_obj_set_width(summary, LV_HOR_RES - 92);
    lv_obj_align(summary, LV_ALIGN_TOP_LEFT, 0, 30);
    lv_label_set_long_mode(summary, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(summary, text_font, 0);
    lv_obj_set_style_text_color(summary, lv_color_hex(kWordDictationMutedText), 0);
    lv_obj_set_style_text_line_space(summary, 4, 0);

    word_dictation_action_data_.resize(2);
    lv_obj_t* start_btn = CreatePrimaryButton(word_dictation_content_, LV_HOR_RES - 44, 44, "开始听写", text_font);
    lv_obj_set_pos(start_btn, 6, 220);
    word_dictation_action_data_[0] = WordDictationActionData{this, kWordDictationActionStart};
    lv_obj_set_user_data(start_btn, &word_dictation_action_data_[0]);
    lv_obj_add_event_cb(start_btn, OnWordDictationActionClicked, LV_EVENT_CLICKED, nullptr);

    lv_obj_t* settings_btn = CreateSecondaryButton(word_dictation_content_, LV_HOR_RES - 44, 44, "去设置", text_font);
    lv_obj_set_pos(settings_btn, 6, 276);
    word_dictation_action_data_[1] = WordDictationActionData{this, kWordDictationActionOpenSettings};
    lv_obj_set_user_data(settings_btn, &word_dictation_action_data_[1]);
    lv_obj_add_event_cb(settings_btn, OnWordDictationActionClicked, LV_EVENT_CLICKED, nullptr);

    }
    LoadScreen(word_dictation_screen_);
}

void UiLcdDisplay::ShowWordDictationSettingsScreen() {
    auto* lvgl_theme = static_cast<LvglTheme*>(GetTheme());
    auto* text_font = lvgl_theme->text_font()->font();

    {
        DisplayLockGuard lock(this);
        word_dictation_view_ = WordDictationView::kSettings;
        word_dictation_running_count_label_ = nullptr;
        word_dictation_running_hint_label_ = nullptr;
        word_dictation_action_data_.clear();
        word_dictation_list_item_data_.clear();
        lv_obj_clean(word_dictation_content_);
        lv_label_set_text(word_dictation_title_label_, "听写设置");
        lv_obj_clear_flag(word_dictation_back_button_, LV_OBJ_FLAG_HIDDEN);
        UpdateWordDictationBackButton(false);

    const std::array<std::pair<const char*, std::string>, 4> items = {{
        {"听写顺序", word_dictation_settings_.random_order ? "随机听写" : "顺序听写"},
        {"听写间隔", std::to_string(word_dictation_settings_.interval_seconds) + "s"},
        {"播报次数", std::to_string(word_dictation_settings_.repeat_count) + "次"},
        {"播报模式", word_dictation_settings_.bilingual_mode ? "中英" : "英文"},
    }};
    const std::array<int, 4> actions = {{
        kWordDictationActionToggleOrder,
        kWordDictationActionToggleInterval,
        kWordDictationActionToggleRepeat,
        kWordDictationActionToggleMode,
    }};

    word_dictation_action_data_.resize(5);
    int y = 8;
    for (size_t i = 0; i < items.size(); ++i) {
        lv_obj_t* item = CreatePageButton(word_dictation_content_, LV_HOR_RES - 32, 56);
        lv_obj_set_pos(item, 0, y);
        word_dictation_action_data_[i] = WordDictationActionData{this, actions[i]};
        lv_obj_set_user_data(item, &word_dictation_action_data_[i]);
        lv_obj_add_event_cb(item, OnWordDictationActionClicked, LV_EVENT_CLICKED, nullptr);

        lv_obj_t* left_label = lv_label_create(item);
        lv_label_set_text(left_label, items[i].first);
        lv_obj_align(left_label, LV_ALIGN_LEFT_MID, 16, 0);
        lv_obj_set_style_text_font(left_label, text_font, 0);
        lv_obj_set_style_text_color(left_label, lv_color_hex(kWordDictationTitleText), 0);

        lv_obj_t* right_label = lv_label_create(item);
        lv_label_set_text(right_label, items[i].second.c_str());
        lv_obj_align(right_label, LV_ALIGN_RIGHT_MID, -16, 0);
        lv_obj_set_style_text_font(right_label, text_font, 0);
        lv_obj_set_style_text_color(right_label, lv_color_hex(kWordDictationBlue), 0);

        y += 68;
    }

    lv_obj_t* save_btn = CreatePrimaryButton(word_dictation_content_, LV_HOR_RES - 44, 44, "保存并返回", text_font);
    lv_obj_set_pos(save_btn, 6, y + 18);
    word_dictation_action_data_[4] = WordDictationActionData{this, kWordDictationActionSaveSettings};
    lv_obj_set_user_data(save_btn, &word_dictation_action_data_[4]);
    lv_obj_add_event_cb(save_btn, OnWordDictationActionClicked, LV_EVENT_CLICKED, nullptr);

    }
    LoadScreen(word_dictation_screen_);
}

void UiLcdDisplay::ShowWordDictationCountdownScreen() {
    auto* lvgl_theme = static_cast<LvglTheme*>(GetTheme());
    auto* text_font = lvgl_theme->text_font()->font();

    {
        DisplayLockGuard lock(this);
        word_dictation_view_ = WordDictationView::kCountdown;
        word_dictation_action_data_.clear();
        word_dictation_list_item_data_.clear();
        word_dictation_running_count_label_ = nullptr;
        word_dictation_running_hint_label_ = nullptr;
        lv_obj_clean(word_dictation_content_);
        lv_label_set_text(word_dictation_title_label_, "");
        lv_obj_add_flag(word_dictation_back_button_, LV_OBJ_FLAG_HIDDEN);

    char countdown_text[16];
    snprintf(countdown_text, sizeof(countdown_text), "%d", word_dictation_countdown_value_);
    word_dictation_running_count_label_ = lv_label_create(word_dictation_content_);
    lv_label_set_text(word_dictation_running_count_label_, countdown_text);
    lv_obj_center(word_dictation_running_count_label_);
    lv_obj_set_style_text_font(word_dictation_running_count_label_, text_font, 0);
    lv_obj_set_style_text_color(word_dictation_running_count_label_, lv_color_hex(kWordDictationBlue), 0);
    lv_obj_set_style_text_letter_space(word_dictation_running_count_label_, 2, 0);
    lv_obj_set_style_text_font(word_dictation_running_count_label_, text_font, 0);
    lv_obj_set_style_text_align(word_dictation_running_count_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_transform_scale(word_dictation_running_count_label_, 320, 0);

    word_dictation_running_hint_label_ = lv_label_create(word_dictation_content_);
    lv_label_set_text(word_dictation_running_hint_label_, "请准备好纸和笔");
    lv_obj_align(word_dictation_running_hint_label_, LV_ALIGN_CENTER, 0, 44);
    lv_obj_set_style_text_font(word_dictation_running_hint_label_, text_font, 0);
    lv_obj_set_style_text_color(word_dictation_running_hint_label_, lv_color_hex(kWordDictationBlue), 0);

    }
    LoadScreen(word_dictation_screen_);
}

void UiLcdDisplay::ShowWordDictationRunningScreen() {
    auto* lvgl_theme = static_cast<LvglTheme*>(GetTheme());
    auto* text_font = lvgl_theme->text_font()->font();

    {
        DisplayLockGuard lock(this);
        word_dictation_view_ = WordDictationView::kRunning;
        word_dictation_action_data_.clear();
        word_dictation_list_item_data_.clear();
        lv_obj_clean(word_dictation_content_);
        lv_label_set_text(word_dictation_title_label_, "");
        lv_obj_clear_flag(word_dictation_back_button_, LV_OBJ_FLAG_HIDDEN);
        UpdateWordDictationBackButton(true);

    word_dictation_running_count_label_ = lv_label_create(word_dictation_content_);
    lv_label_set_text(word_dictation_running_count_label_, "0/0");
    lv_obj_center(word_dictation_running_count_label_);
    lv_obj_set_style_text_font(word_dictation_running_count_label_, text_font, 0);
    lv_obj_set_style_text_color(word_dictation_running_count_label_, lv_color_hex(kWordDictationTitleText), 0);
    lv_obj_set_style_transform_scale(word_dictation_running_count_label_, 260, 0);

    word_dictation_running_hint_label_ = lv_label_create(word_dictation_content_);
    lv_label_set_text(word_dictation_running_hint_label_, "听写进行中");
    lv_obj_align(word_dictation_running_hint_label_, LV_ALIGN_CENTER, 0, 42);
    lv_obj_set_style_text_font(word_dictation_running_hint_label_, text_font, 0);
    lv_obj_set_style_text_color(word_dictation_running_hint_label_, lv_color_hex(kWordDictationMutedText), 0);

    }
    LoadScreen(word_dictation_screen_);
}

void UiLcdDisplay::SelectWordDictationTextbook(int index) {
    if (index < 0 || index >= static_cast<int>(word_dictation_textbooks_.size())) {
        return;
    }

    word_dictation_selected_textbook_index_ = index;
    word_dictation_selected_stage_index_ = -1;
    ShowWordDictationStageScreen();
}

void UiLcdDisplay::SelectWordDictationStage(int index) {
    if (word_dictation_selected_textbook_index_ < 0) {
        return;
    }

    auto& textbook = word_dictation_textbooks_[word_dictation_selected_textbook_index_];
    if (index < 0 || index >= static_cast<int>(textbook.stages.size())) {
        return;
    }

    word_dictation_selected_stage_index_ = index;
    for (auto& word : textbook.stages[index].words) {
        word.selected = true;
    }
    ShowWordDictationWordScreen();
}

void UiLcdDisplay::ToggleWordDictationWord(int index) {
    if (word_dictation_selected_textbook_index_ < 0 || word_dictation_selected_stage_index_ < 0) {
        return;
    }

    auto& stage =
        word_dictation_textbooks_[word_dictation_selected_textbook_index_].stages[word_dictation_selected_stage_index_];
    if (index < 0 || index >= static_cast<int>(stage.words.size())) {
        return;
    }

    stage.words[index].selected = !stage.words[index].selected;
    ShowWordDictationWordScreen();
}

void UiLcdDisplay::SetAllWordDictationWordsSelected(bool selected) {
    if (word_dictation_selected_textbook_index_ < 0 || word_dictation_selected_stage_index_ < 0) {
        return;
    }

    auto& stage =
        word_dictation_textbooks_[word_dictation_selected_textbook_index_].stages[word_dictation_selected_stage_index_];
    for (auto& word : stage.words) {
        word.selected = selected;
    }
    ShowWordDictationWordScreen();
}

void UiLcdDisplay::CycleWordDictationSetting(int action) {
    switch (action) {
    case kWordDictationActionToggleOrder:
        word_dictation_settings_.random_order = !word_dictation_settings_.random_order;
        break;
    case kWordDictationActionToggleInterval:
        if (word_dictation_settings_.interval_seconds == 5) {
            word_dictation_settings_.interval_seconds = 10;
        } else if (word_dictation_settings_.interval_seconds == 10) {
            word_dictation_settings_.interval_seconds = 15;
        } else {
            word_dictation_settings_.interval_seconds = 5;
        }
        break;
    case kWordDictationActionToggleRepeat:
        word_dictation_settings_.repeat_count = (word_dictation_settings_.repeat_count == 1) ? 2 : 1;
        break;
    case kWordDictationActionToggleMode:
        word_dictation_settings_.bilingual_mode = !word_dictation_settings_.bilingual_mode;
        break;
    default:
        break;
    }

    ShowWordDictationSettingsScreen();
}

void UiLcdDisplay::StartWordDictationCountdown() {
    if (GetWordDictationSelectedWordCount() <= 0) {
        ShowNotification("请至少选择一个单词", 1200);
        return;
    }

    RebuildWordDictationPlayOrder();
    if (word_dictation_play_order_.empty()) {
        ShowNotification("没有可听写的单词", 1200);
        return;
    }

    StopWordDictationPlayback();
    word_dictation_countdown_value_ = 3;
    word_dictation_current_play_index_ = 0;
    ShowWordDictationCountdownScreen();

    if (const auto sound = GetDigitSound(word_dictation_countdown_value_); !sound.empty()) {
        Application::GetInstance().PlaySound(sound);
    }
    ESP_ERROR_CHECK(esp_timer_start_periodic(word_dictation_countdown_timer_, 1000 * 1000));
}

void UiLcdDisplay::AdvanceWordDictationCountdown() {
    if (word_dictation_view_ != WordDictationView::kCountdown || word_dictation_running_count_label_ == nullptr) {
        return;
    }

    if (word_dictation_countdown_value_ > 1) {
        --word_dictation_countdown_value_;
        char countdown_text[16];
        snprintf(countdown_text, sizeof(countdown_text), "%d", word_dictation_countdown_value_);
        DisplayLockGuard lock(this);
        lv_label_set_text(word_dictation_running_count_label_, countdown_text);
        if (const auto sound = GetDigitSound(word_dictation_countdown_value_); !sound.empty()) {
            Application::GetInstance().PlaySound(sound);
        }
        return;
    }

    if (word_dictation_countdown_value_ == 1) {
        word_dictation_countdown_value_ = 0;
        DisplayLockGuard lock(this);
        lv_label_set_text(word_dictation_running_count_label_, "开始听写");
        if (word_dictation_running_hint_label_ != nullptr) {
            lv_label_set_text(word_dictation_running_hint_label_, "");
        }
        Application::GetInstance().PlaySound(Lang::Sounds::OGG_POPUP);
        return;
    }

    esp_timer_stop(word_dictation_countdown_timer_);
    ShowWordDictationRunningScreen();
    AdvanceWordDictationPlayback();
    if (!word_dictation_play_order_.empty()) {
        ESP_ERROR_CHECK(
            esp_timer_start_periodic(word_dictation_progress_timer_,
                                     static_cast<uint64_t>(word_dictation_settings_.interval_seconds) * 1000 * 1000));
    }
}

void UiLcdDisplay::AdvanceWordDictationPlayback() {
    if (word_dictation_view_ != WordDictationView::kRunning || word_dictation_running_count_label_ == nullptr) {
        return;
    }

    const int total = GetWordDictationPlaybackStepCount();
    if (total <= 0) {
        StopWordDictationPlayback();
        ShowWordDictationSummaryScreen();
        return;
    }

    if (word_dictation_current_play_index_ >= word_dictation_play_order_.size()) {
        StopWordDictationPlayback();
        return;
    }

    ++word_dictation_current_play_index_;
    char progress_text[32];
    snprintf(progress_text, sizeof(progress_text), "%u/%d",
             static_cast<unsigned>(word_dictation_current_play_index_), total);

    {
        DisplayLockGuard lock(this);
        lv_label_set_text(word_dictation_running_count_label_, progress_text);
        if (word_dictation_running_hint_label_ != nullptr) {
            lv_label_set_text(word_dictation_running_hint_label_, "听写进行中");
        }
    }
    Application::GetInstance().PlaySound(Lang::Sounds::OGG_POPUP);

    if (word_dictation_current_play_index_ >= word_dictation_play_order_.size()) {
        StopWordDictationPlayback();
        {
            DisplayLockGuard lock(this);
            if (word_dictation_running_hint_label_ != nullptr) {
                lv_label_set_text(word_dictation_running_hint_label_, "听写完成");
            }
        }
        Application::GetInstance().PlaySound(Lang::Sounds::OGG_SUCCESS);
    }
}

void UiLcdDisplay::StopWordDictationPlayback() {
    if (word_dictation_countdown_timer_ != nullptr) {
        esp_timer_stop(word_dictation_countdown_timer_);
    }
    if (word_dictation_progress_timer_ != nullptr) {
        esp_timer_stop(word_dictation_progress_timer_);
    }
}

void UiLcdDisplay::HandleWordDictationBack() {
    switch (word_dictation_view_) {
    case WordDictationView::kTextbooks:
        StopWordDictationPlayback();
        word_dictation_view_ = WordDictationView::kHidden;
        ReturnToMainMenu();
        break;
    case WordDictationView::kStages:
        ShowWordDictationTextbookScreen();
        break;
    case WordDictationView::kWords:
        ShowWordDictationStageScreen();
        break;
    case WordDictationView::kSummary:
        ShowWordDictationWordScreen();
        break;
    case WordDictationView::kSettings:
        ShowWordDictationSummaryScreen();
        break;
    case WordDictationView::kRunning:
        StopWordDictationPlayback();
        ShowWordDictationSummaryScreen();
        break;
    case WordDictationView::kCountdown:
        break;
    case WordDictationView::kHidden:
    default:
        ReturnToMainMenu();
        break;
    }
}

void UiLcdDisplay::OnWordDictationListItemClicked(lv_event_t* e) {
    auto* target = static_cast<lv_obj_t*>(lv_event_get_current_target(e));
    auto* data = static_cast<WordDictationListItemData*>(lv_obj_get_user_data(target));
    if (data == nullptr || data->display == nullptr) {
        return;
    }

    switch (data->display->word_dictation_view_) {
    case WordDictationView::kTextbooks:
        data->display->SelectWordDictationTextbook(data->index);
        break;
    case WordDictationView::kStages:
        data->display->SelectWordDictationStage(data->index);
        break;
    case WordDictationView::kWords:
        data->display->ToggleWordDictationWord(data->index);
        break;
    default:
        break;
    }
}

void UiLcdDisplay::OnWordDictationActionClicked(lv_event_t* e) {
    auto* target = static_cast<lv_obj_t*>(lv_event_get_current_target(e));
    auto* data = static_cast<WordDictationActionData*>(lv_obj_get_user_data(target));
    if (data == nullptr || data->display == nullptr) {
        return;
    }

    switch (data->action) {
    case kWordDictationActionSelectAll:
        data->display->SetAllWordDictationWordsSelected(true);
        break;
    case kWordDictationActionClearSelection:
        data->display->SetAllWordDictationWordsSelected(false);
        break;
    case kWordDictationActionConfirmSelection:
        data->display->ShowWordDictationSummaryScreen();
        break;
    case kWordDictationActionOpenSettings:
        data->display->ShowWordDictationSettingsScreen();
        break;
    case kWordDictationActionStart:
        data->display->StartWordDictationCountdown();
        break;
    case kWordDictationActionSaveSettings:
        data->display->ShowWordDictationSummaryScreen();
        break;
    case kWordDictationActionToggleOrder:
    case kWordDictationActionToggleInterval:
    case kWordDictationActionToggleRepeat:
    case kWordDictationActionToggleMode:
        data->display->CycleWordDictationSetting(data->action);
        break;
    default:
        break;
    }
}
