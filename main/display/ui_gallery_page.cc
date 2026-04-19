#include "ui_display.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <unistd.h>

#include <esp_heap_caps.h>
#include <esp_log.h>

namespace {

constexpr int kStatusBarHeight = 27;
const char* TAG = "CompactDisplay";

}

void UiLcdDisplay::AllocateGalleryPageResources() {
    DisplayLockGuard lock(this);

    if (gallery_screen_ != nullptr) {
        return;
    }

    gallery_screen_ = lv_obj_create(nullptr);
    lv_obj_set_style_bg_color(gallery_screen_, lv_color_white(), 0);
    lv_obj_set_style_border_width(gallery_screen_, 0, 0);
    lv_obj_set_style_pad_all(gallery_screen_, 0, 0);
    gallery_list_ = lv_list_create(gallery_screen_);
    lv_obj_set_size(gallery_list_, LV_HOR_RES, LV_VER_RES - kStatusBarHeight);
    lv_obj_align(gallery_list_, LV_ALIGN_TOP_MID, 0, kStatusBarHeight);
    lv_obj_set_style_bg_color(gallery_list_, lv_color_white(), 0);
    lv_obj_set_style_border_width(gallery_list_, 0, 0);
    lv_obj_set_style_pad_all(gallery_list_, 0, 0);
    gallery_back_button_data_ = BackButtonData{this, 2};
    CreateBackButton(gallery_screen_, &gallery_back_button_data_);
}

void UiLcdDisplay::FreeGalleryPageResources() {
    DisplayLockGuard lock(this);
    if (gallery_screen_ != nullptr) {
        lv_obj_del(gallery_screen_);
        gallery_screen_ = nullptr;
    }
    gallery_list_ = nullptr;
    photo_files_.clear();
}

void UiLcdDisplay::ShowGallery() {
    current_photo_image_.reset();
    current_photo_img_ = nullptr;
    StopCameraPreview();

    if (gallery_screen_ == nullptr) {
        AllocateGalleryPageResources();
    }

    RefreshGalleryList();
    LoadScreen(gallery_screen_);
}

void UiLcdDisplay::RefreshGalleryList() {
    if (gallery_list_ == nullptr) {
        return;
    }

    DisplayLockGuard lock(this);

    lv_obj_clean(gallery_list_);
    photo_files_.clear();

    DIR* dir = opendir("/storage");
    if (dir == nullptr) {
        ESP_LOGE(TAG, "Failed to open /storage directory");
        lv_list_add_text(gallery_list_, "暂无照片");
        return;
    }

    dirent* entry = nullptr;
    while ((entry = readdir(dir)) != nullptr) {
        if (entry->d_type != DT_REG) {
            continue;
        }

        const char* name = entry->d_name;
        size_t len = strlen(name);
        if (len > 4 && strcmp(name + len - 4, ".raw") == 0) {
            photo_files_.emplace_back(name);
        }
    }
    closedir(dir);

    if (photo_files_.empty()) {
        lv_list_add_text(gallery_list_, "暂无照片");
        return;
    }

    std::sort(photo_files_.begin(), photo_files_.end(), std::greater<std::string>());

    for (const auto& photo_file : photo_files_) {
        std::string display_name = photo_file;
        size_t dot_pos = display_name.find_last_of('.');
        if (dot_pos != std::string::npos) {
            display_name = display_name.substr(0, dot_pos);
        }

        lv_obj_t* btn = lv_list_add_button(gallery_list_, LV_SYMBOL_RIGHT, display_name.c_str());
        lv_obj_set_style_text_color(btn, lv_color_black(), 0);

        char* filename_copy = static_cast<char*>(malloc(photo_file.size() + 1));
        if (filename_copy == nullptr) {
            continue;
        }
        strcpy(filename_copy, photo_file.c_str());
        lv_obj_set_user_data(btn, filename_copy);
        lv_obj_add_event_cb(btn, FreeUserDataOnDelete, LV_EVENT_DELETE, nullptr);
        lv_obj_add_event_cb(btn, OnGalleryItemClicked, LV_EVENT_CLICKED, this);
        lv_obj_add_event_cb(btn, OnGalleryItemLongPressed, LV_EVENT_LONG_PRESSED, this);
    }
}

void UiLcdDisplay::ShowPhotoViewer(const char* filename) {
    {
        DisplayLockGuard lock(this);
        AllocatePhotoViewerPageResources();

        current_photo_image_.reset();
        current_photo_img_ = nullptr;

        lv_obj_clean(photo_viewer_container_);
        current_photo_img_ = lv_image_create(photo_viewer_container_);
        lv_obj_align(current_photo_img_, LV_ALIGN_CENTER, 0, 0);
    }

    char filepath[128];
    snprintf(filepath, sizeof(filepath), "/storage/%s", filename);
    LoadRawImage(current_photo_img_, filepath);

    LoadScreen(photo_viewer_screen_);
}

void UiLcdDisplay::AllocatePhotoViewerPageResources() {
    if (photo_viewer_screen_ != nullptr) {
        return;
    }

    DisplayLockGuard lock(this);
    photo_viewer_screen_ = lv_obj_create(nullptr);
    lv_obj_set_style_bg_color(photo_viewer_screen_, lv_color_black(), 0);
    lv_obj_set_style_border_width(photo_viewer_screen_, 0, 0);
    lv_obj_set_style_pad_all(photo_viewer_screen_, 0, 0);
    photo_viewer_container_ = lv_obj_create(photo_viewer_screen_);
    lv_obj_set_size(photo_viewer_container_, LV_HOR_RES, LV_VER_RES);
    lv_obj_set_style_bg_color(photo_viewer_container_, lv_color_black(), 0);
    lv_obj_set_style_border_width(photo_viewer_container_, 0, 0);
    lv_obj_set_style_pad_all(photo_viewer_container_, 0, 0);
    photo_viewer_back_button_data_ = BackButtonData{this, 3};
    CreateBackButton(photo_viewer_screen_, &photo_viewer_back_button_data_, true);
}

void UiLcdDisplay::FreePhotoViewerPageResources() {
    current_photo_image_.reset();
    current_photo_img_ = nullptr;

    DisplayLockGuard lock(this);
    if (photo_viewer_screen_ != nullptr) {
        lv_obj_del(photo_viewer_screen_);
        photo_viewer_screen_ = nullptr;
    }
    photo_viewer_container_ = nullptr;
}

void UiLcdDisplay::ShowDeleteConfirmDialog(const char* filename) {
    DisplayLockGuard lock(this);

    lv_obj_t* mbox = lv_msgbox_create(nullptr);
    lv_msgbox_add_title(mbox, "Delete Photo");

    std::string display_name = filename;
    size_t dot_pos = display_name.find_last_of('.');
    if (dot_pos != std::string::npos) {
        display_name = display_name.substr(0, dot_pos);
    }
    std::string msg_text = "确定要删除照片\n\"" + display_name + "\" 吗？";
    lv_msgbox_add_text(mbox, msg_text.c_str());

    lv_obj_t* cancel_btn = lv_msgbox_add_footer_button(mbox, "Cancel");
    lv_obj_t* delete_btn = lv_msgbox_add_footer_button(mbox, "Delete");
    lv_obj_set_style_text_color(delete_btn, lv_color_make(255, 0, 0), 0);

    char* filename_copy = static_cast<char*>(malloc(strlen(filename) + 1));
    if (filename_copy != nullptr) {
        strcpy(filename_copy, filename);
        lv_obj_set_user_data(mbox, filename_copy);
        lv_obj_add_event_cb(mbox, FreeUserDataOnDelete, LV_EVENT_DELETE, nullptr);
    }

    lv_obj_add_event_cb(cancel_btn, OnDeleteDialogCancel, LV_EVENT_CLICKED, nullptr);
    lv_obj_add_event_cb(delete_btn, OnDeleteDialogConfirm, LV_EVENT_CLICKED, this);
}

void UiLcdDisplay::DeletePhoto(const char* filename) {
    char filepath[128];
    snprintf(filepath, sizeof(filepath), "/storage/%s", filename);
    if (unlink(filepath) == 0) {
        ESP_LOGI(TAG, "Photo deleted: %s", filepath);
        RefreshGalleryList();
    } else {
        ESP_LOGE(TAG, "Failed to delete photo: %s", filepath);
    }
}

void UiLcdDisplay::LoadRawImage(lv_obj_t* img_obj, const char* filepath) {
    FILE* f = fopen(filepath, "rb");
    if (f == nullptr) {
        ESP_LOGE(TAG, "Failed to open RAW file: %s", filepath);
        lv_obj_t* error_label = lv_label_create(lv_obj_get_parent(img_obj));
        lv_label_set_text(error_label, "无法加载图片");
        lv_obj_set_style_text_color(error_label, lv_color_white(), 0);
        lv_obj_center(error_label);
        return;
    }

    uint16_t width = 0;
    uint16_t height = 0;
    if (fread(&width, 2, 1, f) != 1 || fread(&height, 2, 1, f) != 1) {
        fclose(f);
        lv_obj_t* error_label = lv_label_create(lv_obj_get_parent(img_obj));
        lv_label_set_text(error_label, "读取文件头失败");
        lv_obj_set_style_text_color(error_label, lv_color_white(), 0);
        lv_obj_center(error_label);
        return;
    }

    uint32_t pixel_count = width * height;
    uint32_t data_size = pixel_count * 2;
    uint8_t* data = static_cast<uint8_t*>(heap_caps_malloc(data_size, MALLOC_CAP_SPIRAM));
    if (data == nullptr) {
        fclose(f);
        lv_obj_t* error_label = lv_label_create(lv_obj_get_parent(img_obj));
        lv_label_set_text(error_label, "内存不足");
        lv_obj_set_style_text_color(error_label, lv_color_white(), 0);
        lv_obj_center(error_label);
        return;
    }

    if (fread(data, 1, data_size, f) != data_size) {
        free(data);
        fclose(f);
        lv_obj_t* error_label = lv_label_create(lv_obj_get_parent(img_obj));
        lv_label_set_text(error_label, "读取像素数据失败");
        lv_obj_set_style_text_color(error_label, lv_color_white(), 0);
        lv_obj_center(error_label);
        return;
    }
    fclose(f);

    uint16_t* rgb565 = reinterpret_cast<uint16_t*>(data);
    for (uint32_t i = 0; i < pixel_count; ++i) {
        rgb565[i] = __builtin_bswap16(rgb565[i]);
    }

    current_photo_image_ = std::make_unique<LvglAllocatedImage>(data, data_size, width, height, width * 2, LV_COLOR_FORMAT_RGB565);
    lv_image_set_src(img_obj, current_photo_image_->image_dsc());

    lv_coord_t max_w = LV_HOR_RES;
    lv_coord_t max_h = LV_VER_RES - 50;
    lv_coord_t scale_w = (max_w * 256) / width;
    lv_coord_t scale_h = (max_h * 256) / height;
    lv_coord_t scale = (scale_w < scale_h) ? scale_w : scale_h;
    if (scale > 256) {
        scale = 256;
    }

    lv_image_set_scale(img_obj, scale);
    lv_obj_set_size(img_obj, (width * scale) / 256, (height * scale) / 256);
    lv_obj_center(img_obj);
}

void UiLcdDisplay::OnGalleryItemClicked(lv_event_t* e) {
    auto* display = static_cast<UiLcdDisplay*>(lv_event_get_user_data(e));
    auto* target = static_cast<lv_obj_t*>(lv_event_get_current_target(e));
    const char* filename = static_cast<const char*>(lv_obj_get_user_data(target));
    if (display != nullptr && filename != nullptr) {
        display->ShowPhotoViewer(filename);
    }
}

void UiLcdDisplay::OnGalleryItemLongPressed(lv_event_t* e) {
    auto* display = static_cast<UiLcdDisplay*>(lv_event_get_user_data(e));
    auto* target = static_cast<lv_obj_t*>(lv_event_get_current_target(e));
    const char* filename = static_cast<const char*>(lv_obj_get_user_data(target));
    if (display != nullptr && filename != nullptr) {
        display->ShowDeleteConfirmDialog(filename);
    }
}

void UiLcdDisplay::OnDeleteDialogCancel(lv_event_t* e) {
    auto* btn = static_cast<lv_obj_t*>(lv_event_get_current_target(e));
    lv_obj_t* footer = lv_obj_get_parent(btn);
    lv_obj_t* mbox = lv_obj_get_parent(footer);
    lv_msgbox_close(mbox);
}

void UiLcdDisplay::OnDeleteDialogConfirm(lv_event_t* e) {
    auto* display = static_cast<UiLcdDisplay*>(lv_event_get_user_data(e));
    auto* btn = static_cast<lv_obj_t*>(lv_event_get_current_target(e));
    lv_obj_t* footer = lv_obj_get_parent(btn);
    lv_obj_t* mbox = lv_obj_get_parent(footer);
    const char* filename = static_cast<const char*>(lv_obj_get_user_data(mbox));
    if (display != nullptr && filename != nullptr) {
        display->DeletePhoto(filename);
    }
    lv_msgbox_close(mbox);
}

void UiLcdDisplay::FreeUserDataOnDelete(lv_event_t* e) {
    auto* target = static_cast<lv_obj_t*>(lv_event_get_current_target(e));
    void* user_data = lv_obj_get_user_data(target);
    if (user_data != nullptr) {
        free(user_data);
        lv_obj_set_user_data(target, nullptr);
    }
}
