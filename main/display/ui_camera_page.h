#ifndef UI_CAMERA_PAGE_H
#define UI_CAMERA_PAGE_H

#include "ui_page_common.h"

struct UiDisplayCameraPage {
    lv_obj_t* camera_screen_ = nullptr;
    lv_obj_t* camera_mode_btn_left_ = nullptr;
    lv_obj_t* camera_mode_btn_center_ = nullptr;
    lv_obj_t* camera_mode_btn_right_ = nullptr;
    lv_obj_t* camera_ble_status_label_ = nullptr;
    BackButtonData camera_back_button_data_ = {};
};

#endif // UI_CAMERA_PAGE_H
