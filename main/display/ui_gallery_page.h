#ifndef UI_GALLERY_PAGE_H
#define UI_GALLERY_PAGE_H

#include "ui_page_common.h"

#include <vector>
#include <string>

struct UiDisplayGalleryPage {
    lv_obj_t* gallery_screen_ = nullptr;
    lv_obj_t* gallery_list_ = nullptr;
    std::vector<std::string> photo_files_;
    BackButtonData gallery_back_button_data_ = {};
};

#endif // UI_GALLERY_PAGE_H
