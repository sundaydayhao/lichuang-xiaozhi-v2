#ifndef UI_DISPLAY_H
#define UI_DISPLAY_H

#include "lcd_display.h"
#include "device_state.h"

#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <atomic>
#include <esp_timer.h>
#include <vector>

class UiPrintPanel;
class UiLcdDisplay : public SpiLcdDisplay {
public:
    static constexpr size_t kMenuItemCount = 12;
    static constexpr size_t kSettingsItemCount = 4;
    static constexpr size_t kNetworkSwitchOptionCount = 2;
    static constexpr size_t kServerSwitchOptionCount = 2;
    static constexpr size_t kLanguageSwitchOptionCount = 3;
    static constexpr size_t kAboutInfoItemCount = 7;

    UiLcdDisplay(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_handle_t panel,
                 int width, int height, int offset_x, int offset_y,
                 bool mirror_x, bool mirror_y, bool swap_xy);
    ~UiLcdDisplay() override;

    void SetEmotion(const char* emotion) override;
    void SetChatMessage(const char* role, const char* content) override;
    void SetPreviewImage(std::unique_ptr<LvglImage> image) override;
    void SetCameraPreviewCallback(std::function<void()> callback);
    bool HandleFeatureSpeechRecognized(const std::string& text);
    bool IsAiPrintActive() const;

private:
    friend class UiPrintPanel;

    enum class CameraMode {
        kPhoto = 0,
        kVideo = 1,
    };

    struct MenuButtonData {
        UiLcdDisplay* display = nullptr;
        int option_id = 0;
        lv_point_t press_point = {0, 0};
        bool moved = false;
    };

    struct SettingsItemData {
        UiLcdDisplay* display = nullptr;
        int item_id = 0;
    };

    struct BackButtonData {
        UiLcdDisplay* display = nullptr;
        int target = 0;
    };

    struct NetworkSwitchOptionData {
        UiLcdDisplay* display = nullptr;
        int option_id = 0;
    };

    struct ServerSwitchOptionData {
        UiLcdDisplay* display = nullptr;
        int option_id = 0;
    };

    struct LanguageSwitchOptionData {
        UiLcdDisplay* display = nullptr;
        int option_id = 0;
    };

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

    struct SleepMusicOptionData {
        UiLcdDisplay* display = nullptr;
        int index = 0;
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
    std::array<lv_obj_t*, kMenuItemCount> menu_buttons_ = {};
    std::array<lv_obj_t*, kMenuItemCount> menu_button_icon_images_ = {};
    std::array<lv_obj_t*, kMenuItemCount> menu_button_labels_ = {};
    std::array<MenuButtonData, kMenuItemCount> menu_button_data_ = {};

    lv_obj_t* settings_screen_ = nullptr;
    lv_obj_t* settings_title_label_ = nullptr;
    std::array<lv_obj_t*, kSettingsItemCount> settings_item_name_labels_ = {};
    std::array<SettingsItemData, kSettingsItemCount> settings_item_data_ = {};
    BackButtonData settings_back_button_data_ = {};

    lv_obj_t* network_switch_screen_ = nullptr;
    lv_obj_t* network_switch_title_label_ = nullptr;
    lv_obj_t* network_switch_hint_label_ = nullptr;
    std::array<lv_obj_t*, kNetworkSwitchOptionCount> network_switch_option_buttons_ = {};
    std::array<lv_obj_t*, kNetworkSwitchOptionCount> network_switch_option_labels_ = {};
    std::array<lv_obj_t*, kNetworkSwitchOptionCount> network_switch_option_state_labels_ = {};
    std::array<NetworkSwitchOptionData, kNetworkSwitchOptionCount> network_switch_option_data_ = {};
    BackButtonData network_switch_back_button_data_ = {};

    lv_obj_t* server_switch_screen_ = nullptr;
    lv_obj_t* server_switch_title_label_ = nullptr;
    std::array<lv_obj_t*, kServerSwitchOptionCount> server_switch_option_buttons_ = {};
    std::array<lv_obj_t*, kServerSwitchOptionCount> server_switch_option_labels_ = {};
    std::array<lv_obj_t*, kServerSwitchOptionCount> server_switch_option_state_labels_ = {};
    std::array<ServerSwitchOptionData, kServerSwitchOptionCount> server_switch_option_data_ = {};
    BackButtonData server_switch_back_button_data_ = {};

    lv_obj_t* language_switch_screen_ = nullptr;
    lv_obj_t* language_switch_title_label_ = nullptr;
    std::array<lv_obj_t*, kLanguageSwitchOptionCount> language_switch_option_buttons_ = {};
    std::array<lv_obj_t*, kLanguageSwitchOptionCount> language_switch_option_labels_ = {};
    std::array<lv_obj_t*, kLanguageSwitchOptionCount> language_switch_option_state_labels_ = {};
    std::array<LanguageSwitchOptionData, kLanguageSwitchOptionCount> language_switch_option_data_ = {};
    BackButtonData language_switch_back_button_data_ = {};

    lv_obj_t* about_screen_ = nullptr;
    lv_obj_t* about_title_label_ = nullptr;
    lv_obj_t* about_content_ = nullptr;
    std::array<lv_obj_t*, kAboutInfoItemCount> about_info_title_labels_ = {};
    std::array<lv_obj_t*, kAboutInfoItemCount> about_info_value_labels_ = {};
    std::array<lv_obj_t*, kAboutInfoItemCount> about_info_separator_labels_ = {};
    BackButtonData about_back_button_data_ = {};
    BackButtonData ai_back_button_data_ = {};
    BackButtonData camera_back_button_data_ = {};
    BackButtonData gallery_back_button_data_ = {};
    BackButtonData photo_viewer_back_button_data_ = {};
    BackButtonData word_dictation_back_button_data_ = {};
    BackButtonData sleep_music_back_button_data_ = {};

    lv_obj_t* camera_screen_ = nullptr;
    lv_obj_t* camera_mode_btn_left_ = nullptr;
    lv_obj_t* camera_mode_btn_center_ = nullptr;
    lv_obj_t* camera_mode_btn_right_ = nullptr;
    lv_obj_t* camera_ble_status_label_ = nullptr;
    CameraMode current_camera_mode_ = CameraMode::kPhoto;
    bool is_recording_ = false;

    lv_obj_t* gallery_screen_ = nullptr;
    lv_obj_t* gallery_list_ = nullptr;
    lv_obj_t* photo_viewer_screen_ = nullptr;
    lv_obj_t* photo_viewer_container_ = nullptr;
    lv_obj_t* current_photo_img_ = nullptr;
    std::unique_ptr<LvglImage> current_photo_image_;
    std::vector<std::string> photo_files_;
    std::map<std::string, std::shared_ptr<LvglImage>> asset_image_cache_;
    esp_timer_handle_t alarm_ui_timer_ = nullptr;
    lv_obj_t* alarm_panel_ = nullptr;
    lv_obj_t* alarm_subtitle_label_ = nullptr;
    lv_obj_t* alarm_now_time_label_ = nullptr;
    lv_obj_t* alarm_date_label_ = nullptr;
    lv_obj_t* alarm_hint_label_ = nullptr;
    lv_obj_t* alarm_next_icon_label_ = nullptr;
    lv_obj_t* alarm_next_time_label_ = nullptr;
    lv_obj_t* alarm_ring_frame_ = nullptr;
    lv_obj_t* alarm_ring_inner_ = nullptr;
    lv_obj_t* alarm_clock_time_label_ = nullptr;
    lv_obj_t* alarm_countdown_label_ = nullptr;
    lv_obj_t* alarm_hourglass_label_ = nullptr;
    std::array<lv_obj_t*, 24> alarm_ring_segments_ = {};
    bool alarm_countdown_active_ = false;
    int alarm_countdown_initial_seconds_ = -1;
    int alarm_countdown_last_remaining_seconds_ = -1;

    lv_obj_t* word_dictation_screen_ = nullptr;
    lv_obj_t* word_dictation_title_label_ = nullptr;
    lv_obj_t* word_dictation_content_ = nullptr;
    lv_obj_t* word_dictation_back_button_ = nullptr;
    lv_obj_t* word_dictation_running_count_label_ = nullptr;
    lv_obj_t* word_dictation_running_hint_label_ = nullptr;

    bool gallery_swipe_enabled_ = false;
    int current_feature_option_id_ = 0;
    esp_timer_handle_t camera_preview_timer_ = nullptr;
    std::function<void()> camera_preview_callback_;
    std::atomic<bool> ai_print_generation_running_ = false;
    bool remote_selfie_audio_suspended_ = false;
    int current_dialogue_scene_option_id_ = 0;
    bool magic_mirror_chat_started_ = false;
    int64_t dialogue_scene_deadline_us_ = 0;
    bool dialogue_scene_prompt_captured_ = false;
    esp_timer_handle_t dialogue_scene_timer_ = nullptr;
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
    esp_timer_handle_t word_dictation_countdown_timer_ = nullptr;
    esp_timer_handle_t word_dictation_progress_timer_ = nullptr;
    esp_timer_handle_t sleep_music_timer_ = nullptr;
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

    void InitializeMainMenu();
    void LoadScreen(lv_obj_t* target_screen);
    void ReleaseScreenResources(lv_obj_t* previous_screen, lv_obj_t* target_screen);
    void InitializeCameraScreen();
    void DestroyCameraScreen();
    void InitializeGallery();
    void DestroyGalleryScreen();
    void InitializeSettingsScreen();
    void DestroySettingsScreen();
    void InitializeNetworkSwitchScreen();
    void DestroyNetworkSwitchScreen();
    void InitializeServerSwitchScreen();
    void DestroyServerSwitchScreen();
    void InitializeLanguageSwitchScreen();
    void DestroyLanguageSwitchScreen();
    void InitializeAboutScreen();
    void DestroyAboutScreen();
    void InitializeWordDictationSupport();
    void DeinitializeWordDictationSupport();
    void DestroyWordDictationScreen();
    void InitializeAlarmSupport();
    void DeinitializeAlarmSupport();
    void InitializeSleepMusicSupport();
    void DeinitializeSleepMusicSupport();
    void InitializeAlarmPanel();
    void InitializeSleepMusicScreen();
    void DestroySleepMusicScreen();
    void UpdateStatusBarAppearance(lv_obj_t* target_screen);
    bool ShouldUseTransparentStatusBar(lv_obj_t* target_screen);
    void MoveStatusBarToScreen(lv_obj_t* target_screen);
    lv_obj_t* CreateBackButton(lv_obj_t* parent, BackButtonData* data, bool dark_surface = false);

    void StartCameraPreview(int interval_ms);
    void StopCameraPreview();
    void StartAlarmUiRefresh();
    void StopAlarmUiRefresh();
    void ResetAlarmPanelObjects();
    void RefreshAlarmUi();
    void ShowAlarmPanel();
    void HideAlarmPanel();
    bool HasValidLocalTime() const;

    void OnMenuOptionClicked(int option_id);
    void ReturnToMainMenu();
    void ReturnToCameraScreen();
    void ShowSettingsScreen();
    void ShowNetworkSwitchScreen();
    void ShowServerSwitchScreen();
    void ShowLanguageSwitchScreen();
    void ShowAboutScreen();
    void SetCameraMode(CameraMode mode);
    void UpdateCameraModeDisplay();
    void ExecuteCameraAction();
    void SuspendAudioForRemoteSelfie();
    void ResumeAudioAfterRemoteSelfie();
    void CleanupRemoteSelfie();
    void UpdateCameraBleStatus(bool connected);
    void ShowAiPrintListeningState();
    void ShowAiPrintGeneratingState();
    void ShowAiPrintErrorState(const std::string& message);
    void ShowAiPrintResultImage(std::unique_ptr<LvglImage> image);
    void HideAiPrintMessageLabel();
    void InitializeMagicMirrorIntro();
    void ShowMagicMirrorIntro();
    void HideMagicMirrorIntro();
    void SwitchMagicMirrorToChatMode();
    void ShowWordDictationTextbookScreen();
    void ShowWordDictationStageScreen();
    void ShowWordDictationWordScreen();
    void ShowWordDictationSummaryScreen();
    void ShowWordDictationSettingsScreen();
    void StartWordDictationCountdown();
    void ShowWordDictationCountdownScreen();
    void ShowWordDictationRunningScreen();
    void ShowSleepMusicScreen();
    void HandleWordDictationBack();
    void SelectWordDictationTextbook(int index);
    void SelectWordDictationStage(int index);
    void ToggleWordDictationWord(int index);
    void SetAllWordDictationWordsSelected(bool selected);
    void CycleWordDictationSetting(int action);
    void AdvanceWordDictationCountdown();
    void AdvanceWordDictationPlayback();
    void StopWordDictationPlayback();
    void RefreshSleepMusicScreen();
    void UpdateSleepMusicSelectionStyles();
    void SelectSleepMusicMode(int index);
    void SelectSleepMusicDuration(int index);
    void ToggleSleepMusicPlayback();
    void StartSleepMusicPlayback();
    void StopSleepMusicPlayback(bool notify);
    void StartSleepMusicTimer();
    void StopSleepMusicTimer();
    void HandleSleepMusicTick();
    void UpdateWordDictationBackButton(bool close_mode);
    int GetWordDictationSelectedWordCount() const;
    int GetWordDictationPlaybackStepCount() const;
    std::string GetWordDictationStageTitle() const;
    std::string GetWordDictationSummaryText() const;
    void RebuildWordDictationPlayOrder();
    void ApplyFeatureVisual(int option_id);
    void ClearFeatureVisual();
    void EnterDialogueScene(int option_id);
    void ExitDialogueScene(bool exit_chat_session);
    void UpdateDialogueScenePrompt();
    void SetDialogueSceneOverlayText(const char* text);
    void ClearAiSceneMessages();
    void MarkDialogueScenePromptCaptured();
    void UpdateVisionAiBadgeVisibility();
    void RefreshMenuIconImages();
    const char* GetSettingsTitle() const;
    const char* GetSettingsItemName(int index) const;
    const char* GetNetworkSwitchOptionName(int index) const;
    const char* GetServerSwitchOptionName(int index) const;
    const char* GetLanguageSwitchOptionName(int index) const;
    const char* GetAboutTitle() const;
    const char* GetAboutInfoTitle(int index) const;
    void RefreshNetworkSwitchScreen();
    int GetCurrentNetworkSwitchOptionIndex() const;
    void SelectNetworkSwitchOption(int index);
    void RefreshServerSwitchScreen();
    int GetCurrentServerSwitchOptionIndex() const;
    void SelectServerSwitchOption(int index);
    void RefreshLanguageSwitchScreen();
    int GetCurrentLanguageSwitchOptionIndex() const;
    void SelectLanguageSwitchOption(int index);
    std::string GetSelectedLanguageCode() const;
    void RefreshAboutScreenTexts();
    std::string GetFirmwareVersionForDisplay() const;
    std::string GetActivationCodeForDisplay() const;
    std::string GetServerAddressForDisplay() const;
    std::string GetWifiMacAddressForDisplay() const;
    std::string Get4gImeiForDisplay() const;
    std::string GetIpAddressForDisplay() const;
    std::string GetUuidForDisplay() const;
    std::string BuildDialogueScenePayload(int option_id) const;
    const lv_image_dsc_t* LoadAssetImage(const char* asset_name);
    const char* GetMenuText(int option_id) const;
    const char* GetMenuIconAssetName(int option_id) const;
    const char* GetFeatureBackgroundAssetName(int option_id) const;
    bool IsDialogueSceneOption(int option_id) const;

    void ShowGallery();
    void RefreshGalleryList();
    void ShowPhotoViewer(const char* filename);
    void DestroyPhotoViewerScreen();
    void ShowDeleteConfirmDialog(const char* filename);
    void DeletePhoto(const char* filename);
    void LoadRawImage(lv_obj_t* img_obj, const char* filepath);

    static void OnMenuButtonClicked(lv_event_t* e);
    static void OnAiScreenGesture(lv_event_t* e);
    static void OnCameraScreenGesture(lv_event_t* e);
    static void OnGalleryScreenGesture(lv_event_t* e);
    static void OnPhotoViewerGesture(lv_event_t* e);
    static void OnSettingsScreenGesture(lv_event_t* e);
    static void OnAboutScreenGesture(lv_event_t* e);
    static void OnCameraModeButtonClicked(lv_event_t* e);
    static void OnCameraActionButtonClicked(lv_event_t* e);
    static void OnSettingsItemClicked(lv_event_t* e);
    static void OnNetworkSwitchOptionClicked(lv_event_t* e);
    static void OnServerSwitchOptionClicked(lv_event_t* e);
    static void OnLanguageSwitchOptionClicked(lv_event_t* e);
    static void OnBackButtonClicked(lv_event_t* e);
    static void OnWordDictationListItemClicked(lv_event_t* e);
    static void OnWordDictationActionClicked(lv_event_t* e);
    static void OnGalleryItemClicked(lv_event_t* e);
    static void OnGalleryItemLongPressed(lv_event_t* e);
    static void OnDeleteDialogCancel(lv_event_t* e);
    static void OnDeleteDialogConfirm(lv_event_t* e);
    static void FreeUserDataOnDelete(lv_event_t* e);
};

#endif // UI_DISPLAY_H
