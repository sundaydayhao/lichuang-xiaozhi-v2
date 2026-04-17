#ifndef UI_ASSETS_H
#define UI_ASSETS_H

#ifdef __has_include
    #if __has_include("lvgl.h")
        #ifndef LV_LVGL_H_INCLUDE_SIMPLE
            #define LV_LVGL_H_INCLUDE_SIMPLE
        #endif
    #endif
#endif

#if defined(LV_LVGL_H_INCLUDE_SIMPLE)
    #include "lvgl.h"
#else
    #include "lvgl/lvgl.h"
#endif

extern const lv_image_dsc_t AIzi;
extern const lv_image_dsc_t baike;
extern const lv_image_dsc_t camera_icon;
extern const lv_image_dsc_t danci;
extern const lv_image_dsc_t kouyu;
extern const lv_image_dsc_t kuaisu;
extern const lv_image_dsc_t paishi;
extern const lv_image_dsc_t qinzi;
extern const lv_image_dsc_t shengkong;
extern const lv_image_dsc_t shuiqian;
extern const lv_image_dsc_t tangshi;
extern const lv_image_dsc_t video_icon;

#endif // UI_ASSETS_H
