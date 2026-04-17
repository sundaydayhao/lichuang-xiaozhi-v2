#ifndef UI_DISPLAY_H
#define UI_DISPLAY_H

#include "lcd_display.h"

#include <array>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <atomic>
#include <vector>

class AiPrintFeature;
class UiLcdDisplay : public SpiLcdDisplay {
public:
    static constexpr size_t kMenuItemCount = 12;
    static constexpr size_t kSettingsItemCount = 4;
    static constexpr size_t kAboutInfoItemCount = 7;

    UiLcdDisplay(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_handle_t panel,
                 int width, int height, int offset_x, int offset_y,
                 bool mirror_x, bool mirror_y, bool swap_xy);
    ~UiLcdDisplay() override;

    void SetCameraPreviewCallback(std::function<void()> callback);
    bool HandleFeatureSpeechRecognized(const std::string& text);
    bool IsAiPrintActive() const;

private:
    friend class AiPrintFeature;

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

    lv_obj_t* ai_screen_ = nullptr;
    lv_obj_t* feature_background_image_ = nullptr;
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

    lv_obj_t* about_screen_ = nullptr;
    lv_obj_t* about_title_label_ = nullptr;
    lv_obj_t* about_content_ = nullptr;
    std::array<lv_obj_t*, kAboutInfoItemCount> about_info_title_labels_ = {};
    std::array<lv_obj_t*, kAboutInfoItemCount> about_info_value_labels_ = {};
    std::array<lv_obj_t*, kAboutInfoItemCount> about_info_separator_labels_ = {};
    BackButtonData about_back_button_data_ = {};

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

    bool gallery_swipe_enabled_ = false;
    int current_feature_option_id_ = 0;
    esp_timer_handle_t camera_preview_timer_ = nullptr;
    std::function<void()> camera_preview_callback_;
    std::atomic<bool> ai_print_generation_running_ = false;

    void InitializeMainMenu();
    void InitializeCameraScreen();
    void InitializeGallery();
    void InitializeSettingsScreen();
    void InitializeAboutScreen();
    void MoveStatusBarToScreen(lv_obj_t* target_screen);

    void StartCameraPreview(int interval_ms);
    void StopCameraPreview();

    void OnMenuOptionClicked(int option_id);
    void ReturnToMainMenu();
    void ReturnToCameraScreen();
    void ShowSettingsScreen();
    void ShowAboutScreen();
    void SetCameraMode(CameraMode mode);
    void UpdateCameraModeDisplay();
    void ExecuteCameraAction();
    void UpdateCameraBleStatus(bool connected);
    void ShowAiPrintListeningState();
    void ShowAiPrintGeneratingState();
    void ShowAiPrintErrorState(const std::string& message);
    void ShowAiPrintResultImage(std::unique_ptr<LvglImage> image);
    void ApplyFeatureVisual(int option_id);
    void ClearFeatureVisual();
    void RefreshMenuIconImages();
    const char* GetSettingsTitle() const;
    const char* GetSettingsItemName(int index) const;
    const char* GetAboutTitle() const;
    const char* GetAboutInfoTitle(int index) const;
    void RefreshAboutScreenTexts();
    std::string GetFirmwareVersionForDisplay() const;
    std::string GetActivationCodeForDisplay() const;
    std::string GetServerAddressForDisplay() const;
    std::string GetWifiMacAddressForDisplay() const;
    std::string Get4gImeiForDisplay() const;
    std::string GetIpAddressForDisplay() const;
    std::string GetUuidForDisplay() const;
    const lv_image_dsc_t* LoadAssetImage(const char* asset_name);
    const char* GetMenuText(int option_id) const;
    const char* GetMenuIconAssetName(int option_id) const;
    const char* GetFeatureBackgroundAssetName(int option_id) const;

    void ShowGallery();
    void RefreshGalleryList();
    void ShowPhotoViewer(const char* filename);
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
    static void OnBackButtonClicked(lv_event_t* e);
    static void OnGalleryItemClicked(lv_event_t* e);
    static void OnGalleryItemLongPressed(lv_event_t* e);
    static void OnDeleteDialogCancel(lv_event_t* e);
    static void OnDeleteDialogConfirm(lv_event_t* e);
    static void FreeUserDataOnDelete(lv_event_t* e);
};

#endif // UI_DISPLAY_H
