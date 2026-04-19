#ifndef UI_PHOTO_VIEWER_PAGE_H
#define UI_PHOTO_VIEWER_PAGE_H

#include "ui_page_common.h"

#include <memory>

struct UiDisplayPhotoViewerPage {
    lv_obj_t* photo_viewer_screen_ = nullptr;
    lv_obj_t* photo_viewer_container_ = nullptr;
    lv_obj_t* current_photo_img_ = nullptr;
    std::unique_ptr<LvglImage> current_photo_image_;
    BackButtonData photo_viewer_back_button_data_ = {};
};

#endif // UI_PHOTO_VIEWER_PAGE_H
