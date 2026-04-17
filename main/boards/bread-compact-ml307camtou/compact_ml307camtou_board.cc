#include "dual_network_board.h"
#include "codecs/no_audio_codec.h"
#include "display/lcd_display.h"
#include "system_reset.h"
#include "application.h"
#include "button.h"
#include "config.h"
#include "mcp_server.h"
#include "lamp_controller.h"
#include "led/single_led.h"
#include "assets/lang_config.h"
#include "UI/camera_icon.c"
#include "UI/video_icon.c"
#include "UI/AIzi.c"
#include "UI/baike.c"
#include "UI/danci.c"
#include "UI/kouyu.c"
#include "UI/kuaisu.c"
#include "UI/paishi.c"
#include "UI/qinzi.c"
#include "UI/shengkong.c"
#include "UI/shuiqian.c"
#include "UI/tangshi.c"
#include "esp32_camera.h"
// 添加以下三行
#if defined(TOUCH_CONTROLLER_AXS5106)
#include <esp_lcd_touch_axs5106.h>
#elif defined(TOUCH_CONTROLLER_CST816D)
#include <esp_lcd_touch_cst816s.h>
#endif
#include <esp_lvgl_port.h>
#include <lvgl.h>
#include "widgets/list/lv_list.h"

#include <esp_log.h>
#include <esp_heap_caps.h>
#include <driver/i2c_master.h>
#include <esp_lcd_panel_ops.h>
#include <esp_lcd_panel_vendor.h>
#include <wifi_station.h>
#include <wifi_configuration_ap.h>

#include <nvs.h>
#include <nvs_flash.h>

#include "lamp_G.h"
#include "ble_hid.h"
#include "display/lvgl_display/lvgl_theme.h"
#include "s620.h"

#include "esp_lcd_gc9a01.h"
#include <esp_spiffs.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <algorithm>
#include <vector>
#include <string>
#include <memory>
#include "display/lvgl_display/lvgl_image.h"
static const gc9a01_lcd_init_cmd_t gc9107_lcd_init_cmds[] = {
    //  {cmd, { data }, data_size, delay_ms}
     // REG enable
    {0xfe, (uint8_t[]){0x00}, 0, 0},
    {0xef, (uint8_t[]){0x00}, 0, 0},
    
    // Direction RGB/BGR Select
    {0x36, (uint8_t[]){0x48}, 1, 0},
    
    // RGB565 color format
    {0x3a, (uint8_t[]){0x05}, 1, 0},
    
    // Register enable sequence
    {0x86, (uint8_t[]){0x98}, 1, 0},  // c9/c3/c7 en
    {0x89, (uint8_t[]){0x13}, 1, 0},  // e8/e9 en
    {0x8b, (uint8_t[]){0x80}, 1, 0},  // ff en
    {0x8d, (uint8_t[]){0x33}, 1, 0},  // 99/9d en
    {0x8e, (uint8_t[]){0x0f}, 1, 0},  // f0~f3 en
    
    // Inversion & frame rate
    {0xe8, (uint8_t[]){0x12, 0x00}, 2, 0},
    
    {0xec, (uint8_t[]){0x33, 0x06, 0x22}, 3, 0},
    
    {0xff, (uint8_t[]){0x62}, 1, 0},
    
    {0x99, (uint8_t[]){0x3e}, 1, 0},
    {0x9d, (uint8_t[]){0x4b}, 1, 0},
    {0x98, (uint8_t[]){0x3e}, 1, 0},  // V1 ADD bvdd 3x
    {0x9c, (uint8_t[]){0x4b}, 1, 0},  // V1 ADD 20211004
    
    // VBP/VBN settings
    {0xc3, (uint8_t[]){0x20}, 1, 0},  // vbp for vreg1a/1b
    {0xc4, (uint8_t[]){0x30}, 1, 0},  // vbn for 2a/2b
    {0xc9, (uint8_t[]){0x2A}, 1, 0},
    
    // Column address set
    {0x2a, (uint8_t[]){0x00, 0x22, 0x00, 0xcd}, 4, 0},
    
    // Page address set
    {0x2b, (uint8_t[]){0x00, 0x00, 0x01, 0x3f}, 4, 0},
    
    // Memory write (prepare)
    {0x2c, (uint8_t[]){0x00}, 0, 0},
    
    // Gamma settings
    {0xF0, (uint8_t[]){0x04, 0x07, 0x07, 0x07, 0x05, 0x26}, 6, 0},
    {0xF1, (uint8_t[]){0x3B, 0x71, 0x74, 0x26, 0x28, 0x69}, 6, 0},
    {0xF2, (uint8_t[]){0x04, 0x07, 0x07, 0x07, 0x05, 0x26}, 6, 0},
    {0xF3, (uint8_t[]){0x3B, 0x71, 0x74, 0x26, 0x28, 0x6C}, 6, 0},
    
    // Tearing effect line
    {0x35, (uint8_t[]){0x00}, 1, 0},
    {0x44, (uint8_t[]){0x00, 0x0a}, 2, 0},
    
    // Sleep Out
    {0x11, (uint8_t[]){0x00}, 0, 120},  // 120ms delay after sleep out
    
    // Display On
    {0x29, (uint8_t[]){0x00}, 0, 0},
    
    // Memory write (final prepare)
    {0x2c, (uint8_t[]){0x00}, 0, 0},
};

#ifdef TAG
#undef TAG
#endif
#define TAG "CompactMl307ctBoard"

// 菜单按钮数据结构
struct MenuButtonData {
    class CompactMl307ctBoard* board;
    int option_id;
};

class CompactMl307ctBoard : public DualNetworkBoard {
private:
    Button boot_button_;
    LcdDisplay* display_;
    i2c_master_bus_handle_t i2c_bus_;
    S620* s620_ = nullptr;  // S620电源管理芯片
    lv_obj_t* touch_overlay_;  //LVGL 对象用于捕获触摸
    Esp32Camera* camera_;
    lv_obj_t* volume_down_btn_ = nullptr;  // 音量-按键
    lv_obj_t* volume_up_btn_ = nullptr;    // 音量+按键

    // 主菜单相关成员变量
    lv_obj_t* menu_screen_ = nullptr;      // 主菜单屏幕
    lv_obj_t* menu_page_container_ = nullptr;  // 页面容器（用于滑动）
    lv_obj_t* menu_page1_ = nullptr;       // 第一页（选项1-4）
    lv_obj_t* menu_page2_ = nullptr;       // 第二页（选项5-8）
    lv_obj_t* menu_buttons_[10] = {nullptr}; // 8个选项按钮
    lv_obj_t* ai_screen_ = nullptr;         // AI界面屏幕（原来的界面）
    lv_obj_t* camera_screen_ = nullptr;     // 拍照界面屏幕
    int current_menu_page_ = 0;             // 当前页面（0或1）

    // 拍照界面相关成员变量
    enum CameraMode {
        CAMERA_MODE_PHOTO = 0,  // 拍照模式
        CAMERA_MODE_VIDEO = 1   // 录像模式
    };
    lv_obj_t* camera_mode_btn_left_ = nullptr;   // 左侧摄像机图标按钮
    lv_obj_t* camera_mode_btn_center_ = nullptr; // 中间拍照/摄影按键
    lv_obj_t* camera_mode_btn_right_ = nullptr;   // 右侧照相机图标按钮
    lv_obj_t* camera_zoom_slider_ = nullptr;      // 变焦滑块
    CameraMode current_camera_mode_ = CameraMode::CAMERA_MODE_PHOTO;  // 当前模式，默认拍照

    bool is_recording_ = false;  // 是否正在录像

    // 蓝牙状态标签
    lv_obj_t* camera_ble_status_label_ = nullptr;  // 拍照界面蓝牙状态标签
    
    // 相册相关成员变量
    lv_obj_t* gallery_screen_ = nullptr;  // 相册界面
    lv_obj_t* gallery_list_ = nullptr;   // 相册图片列表容器
    lv_obj_t* photo_viewer_screen_ = nullptr;  // 图片查看器界面
    lv_obj_t* current_photo_img_ = nullptr;  // 当前显示的图片对象
    std::unique_ptr<LvglImage> current_photo_image_;  // 当前图片对象（管理生命周期）
    std::vector<std::string> photo_files_; // 保存的文件名列表
    
    // 保存状态栏引用，避免每次查找
    lv_obj_t* status_bar_ref_ = nullptr;  // 状态栏对象引用

    // 摄像头预览定时器（用于"拍识万物"界面持续显示摄像头画面）
    esp_timer_handle_t camera_preview_timer_ = nullptr;

     // 添加ML307_EN_PIN控制方法
     void SetML307Enable(bool enable) {
        if (ML307_EN_PIN != GPIO_NUM_NC) {
            gpio_set_level(ML307_EN_PIN, enable ? 1 : 0);
            ESP_LOGI(TAG, "ML307 EN pin set to %s", enable ? "HIGH" : "LOW");
        }
    }

void InitializeI2c() {
        // Initialize I2C peripheral for camera and touch
        i2c_master_bus_config_t i2c_bus_cfg = {
            .i2c_port = I2C_NUM_0,
            .sda_io_num = (gpio_num_t)CAMERA_PIN_SIOD,  // GPIO_NUM_4
            .scl_io_num = (gpio_num_t)CAMERA_PIN_SIOC,  // GPIO_NUM_5
            .clk_source = I2C_CLK_SRC_DEFAULT,
            .glitch_ignore_cnt = 7,
            .intr_priority = 0,
            .trans_queue_depth = 0,
            .flags = {
                .enable_internal_pullup = 1,
            },
        };
        ESP_ERROR_CHECK(i2c_new_master_bus(&i2c_bus_cfg, &i2c_bus_));
        ESP_LOGI(TAG, "I2C bus initialized on SDA=%d, SCL=%d", CAMERA_PIN_SIOD, CAMERA_PIN_SIOC);
    }

    void InitializeS620() {
        if (i2c_bus_ == nullptr) {
            ESP_LOGE(TAG, "I2C bus not initialized, cannot initialize S620");
            return;
        }
        s620_ = new S620(i2c_bus_, 0x38);
        // 延迟初始化，在第一次使用时再调用Initialize()
        ESP_LOGI(TAG, "S620 instance created");
    }

void InitializeSpi() {
    spi_bus_config_t buscfg = {};
    buscfg.mosi_io_num = DISPLAY_MOSI_PIN;
    buscfg.miso_io_num = GPIO_NUM_NC;
    buscfg.sclk_io_num = DISPLAY_CLK_PIN;
    buscfg.quadwp_io_num = GPIO_NUM_NC;
    buscfg.quadhd_io_num = GPIO_NUM_NC;
    buscfg.max_transfer_sz = DISPLAY_WIDTH * DISPLAY_HEIGHT * sizeof(uint16_t);
    ESP_ERROR_CHECK(spi_bus_initialize(SPI3_HOST, &buscfg, SPI_DMA_CH_AUTO));
}

void InitializeLcdDisplay() {
        esp_lcd_panel_io_handle_t panel_io = nullptr;
        esp_lcd_panel_handle_t panel = nullptr;
        // 液晶屏控制IO初始化
        ESP_LOGD(TAG, "Install panel IO");
        esp_lcd_panel_io_spi_config_t io_config = {};
        io_config.cs_gpio_num = DISPLAY_CS_PIN;
        io_config.dc_gpio_num = DISPLAY_DC_PIN;
        io_config.spi_mode = DISPLAY_SPI_MODE;
        io_config.pclk_hz = 40 * 1000 * 1000;
        io_config.trans_queue_depth = 10;
        io_config.lcd_cmd_bits = 8;
        io_config.lcd_param_bits = 8;
        ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(SPI3_HOST, &io_config, &panel_io));

        // 初始化液晶屏驱动芯片
        ESP_LOGD(TAG, "Install LCD driver");
        esp_lcd_panel_dev_config_t panel_config = {};
        panel_config.reset_gpio_num = DISPLAY_RST_PIN;
        panel_config.rgb_ele_order = DISPLAY_RGB_ORDER;
        panel_config.bits_per_pixel = 16;
#if defined(LCD_TYPE_ILI9341_SERIAL)
        ESP_ERROR_CHECK(esp_lcd_new_panel_ili9341(panel_io, &panel_config, &panel));
#elif defined(LCD_TYPE_GC9A01_SERIAL)
        ESP_ERROR_CHECK(esp_lcd_new_panel_gc9a01(panel_io, &panel_config, &panel));
        gc9a01_vendor_config_t gc9107_vendor_config = {
            .init_cmds = gc9107_lcd_init_cmds,
            .init_cmds_size = sizeof(gc9107_lcd_init_cmds) / sizeof(gc9a01_lcd_init_cmd_t),
        };        
#else
        ESP_ERROR_CHECK(bsp_lcd_new_panel_st7789(panel_io, &panel_config, &panel));
#endif
        
        esp_lcd_panel_reset(panel);

        esp_lcd_panel_init(panel);
        esp_lcd_panel_invert_color(panel, DISPLAY_INVERT_COLOR);
        esp_lcd_panel_swap_xy(panel, DISPLAY_SWAP_XY);
        esp_lcd_panel_mirror(panel, DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y);
#ifdef  LCD_TYPE_GC9A01_SERIAL
        panel_config.vendor_config = &gc9107_vendor_config;
#endif
        display_ = new SpiLcdDisplay(panel_io, panel,
                                    DISPLAY_WIDTH, DISPLAY_HEIGHT, DISPLAY_OFFSET_X, DISPLAY_OFFSET_Y, DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y, DISPLAY_SWAP_XY);
    }

 void InitializeTouch() {
        esp_lcd_touch_handle_t tp;

        #if defined(TOUCH_CONTROLLER_AXS5106)
    i2c_master_dev_handle_t touch_dev_handle;
    i2c_device_config_t touch_dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = TOUCH_I2C_ADDRESS,
        .scl_speed_hz = TOUCH_I2C_SPEED_HZ,
    };
    ESP_ERROR_CHECK(i2c_master_bus_add_device(i2c_bus_, &touch_dev_cfg, &touch_dev_handle));
    #elif defined(TOUCH_CONTROLLER_CST816D)
    esp_lcd_panel_io_handle_t tp_io_handle = nullptr;
    esp_lcd_panel_io_i2c_config_t tp_io_config = {
        .dev_addr = ESP_LCD_TOUCH_IO_I2C_CST816S_ADDRESS,
        .on_color_trans_done = nullptr,
        .user_ctx = nullptr,
        .control_phase_bytes = 1,
        .dc_bit_offset = 0,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 0,
        .flags = {
            .dc_low_on_data = 0,
            .disable_control_phase = 1,
        },
        .scl_speed_hz = TOUCH_I2C_SPEED_HZ,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_i2c(i2c_bus_, &tp_io_config, &tp_io_handle));
    #endif

        esp_lcd_touch_config_t tp_cfg = {
            .x_max = DISPLAY_SWAP_XY ? DISPLAY_HEIGHT : DISPLAY_WIDTH,
            .y_max = DISPLAY_SWAP_XY ? DISPLAY_WIDTH : DISPLAY_HEIGHT,
            .rst_gpio_num = GPIO_NUM_NC,
            .int_gpio_num = GPIO_NUM_NC,
            .levels = {
                .reset = 0,
                .interrupt = 0,
            },
            .flags = {
                .swap_xy = TOUCH_SWAP_XY ? 1u : 0u,
                .mirror_x = TOUCH_MIRROR_X ? 1u : 0u,
                .mirror_y = TOUCH_MIRROR_Y ? 1u : 0u,
            },
        };
        
        //esp_lcd_panel_io_handle_t tp_io_handle = NULL;
        //esp_lcd_panel_io_i2c_config_t tp_io_config = ESP_LCD_TOUCH_IO_I2C_AXS5106_CONFIG();
        //tp_io_config.scl_speed_hz = 400000;
        #if defined(TOUCH_CONTROLLER_AXS5106)
        ESP_ERROR_CHECK(esp_lcd_touch_new_i2c_axs5106(touch_dev_handle, &tp_cfg, &tp));
        #elif defined(TOUCH_CONTROLLER_CST816D)
        ESP_ERROR_CHECK(esp_lcd_touch_new_i2c_cst816s(tp_io_handle, &tp_cfg, &tp));
        #endif
        
        //ESP_ERROR_CHECK(esp_lcd_new_panel_io_i2c(i2c_bus_, &tp_io_config, &tp_io_handle));
        //ESP_ERROR_CHECK(esp_lcd_touch_new_i2c_ft5x06(tp_io_handle, &tp_cfg, &tp));
        
        /* Add touch input to LVGL */
        const lvgl_port_touch_cfg_t touch_cfg = {
            .disp = lv_display_get_default(),
            .handle = tp,
        };
        lvgl_port_add_touch(&touch_cfg);
        
        ESP_LOGI(TAG, "Touch screen initialized (FT5x06)");
    }

    // 将原有状态栏移到最顶层，使其显示在所有页面上
    void MoveStatusBarToTop() {
        DisplayLockGuard lock(display_);
        
        // 查找原有的状态栏（通过遍历 ai_screen_ 的子对象）
        // 状态栏应该在 container_ 的第一个子对象
        lv_obj_t* container = lv_obj_get_child(ai_screen_, 0);
        if (container != nullptr) {
            lv_obj_t* status_bar = lv_obj_get_child(container, 0);
            if (status_bar != nullptr) {
                // 保存状态栏引用
                status_bar_ref_ = status_bar;
                
                // 将状态栏移到 ai_screen_ 的直接子对象（而不是 container_ 的子对象）
                // 这样状态栏就会显示在所有内容之上
                lv_obj_set_parent(status_bar, ai_screen_);
                lv_obj_align(status_bar, LV_ALIGN_TOP_MID, 0, 0);
                lv_obj_move_foreground(status_bar);
                
                // 确保状态栏在所有屏幕上都能显示
                // 当切换屏幕时，需要将状态栏也移到新屏幕上
                ESP_LOGI(TAG, "Status bar moved to top level and reference saved");
            }
        }
    }
    
    // 查找状态栏（从所有可能的屏幕中查找）
    lv_obj_t* FindStatusBar() {
        // 首先检查保存的引用是否有效
        if (status_bar_ref_ != nullptr && lv_obj_is_valid(status_bar_ref_)) {
            ESP_LOGI(TAG, "Using saved status bar reference");
            return status_bar_ref_;
        }
        
        // 如果引用无效，重新查找
        // 按优先级顺序：当前活动屏幕 -> ai_screen_ -> menu_screen_ -> camera_screen_
        lv_obj_t* current_screen = lv_screen_active();
        lv_obj_t* screens[] = {current_screen, ai_screen_, menu_screen_, camera_screen_};
        
        for (int s = 0; s < 4; s++) {
            if (screens[s] == nullptr) continue;
            
            // 先检查是否是屏幕的直接子对象
            uint32_t child_cnt = lv_obj_get_child_cnt(screens[s]);
            for (uint32_t i = 0; i < child_cnt; i++) {
                lv_obj_t* child = lv_obj_get_child(screens[s], i);
                if (child == nullptr) continue;
                
                // 检查是否是状态栏：状态栏通常有多个子对象（network_label, status_label等）
                uint32_t grandchild_cnt = lv_obj_get_child_cnt(child);
                // 状态栏至少有3-10个子对象（network_label, status_label, mute_label, battery_label等）
                if (grandchild_cnt >= 3 && grandchild_cnt <= 10) {
                    // 保存找到的状态栏引用
                    status_bar_ref_ = child;
                    ESP_LOGI(TAG, "Found status bar on screen %p (child count: %d)", screens[s], grandchild_cnt);
                    return child;
                }
            }
            
            // 如果没找到，检查 container 中的状态栏（ai_screen_ 特有）
            if (screens[s] == ai_screen_) {
                lv_obj_t* container = lv_obj_get_child(ai_screen_, 0);
                if (container != nullptr) {
                    lv_obj_t* status_bar = lv_obj_get_child(container, 0);
                    if (status_bar != nullptr) {
                        uint32_t grandchild_cnt = lv_obj_get_child_cnt(status_bar);
                        if (grandchild_cnt >= 3 && grandchild_cnt <= 10) {
                            // 保存找到的状态栏引用
                            status_bar_ref_ = status_bar;
                            ESP_LOGI(TAG, "Found status bar in container (child count: %d)", grandchild_cnt);
                            return status_bar;
                        }
                    }
                }
            }
        }
        
        ESP_LOGW(TAG, "Status bar not found in any screen");
        status_bar_ref_ = nullptr;  // 清除无效引用
        return nullptr;
    }
    
    // 在切换屏幕时，将状态栏移到新屏幕上
    void MoveStatusBarToScreen(lv_obj_t* target_screen) {
        if (target_screen == nullptr) {
            ESP_LOGW(TAG, "MoveStatusBarToScreen: target_screen is nullptr");
            return;
        }
        
        DisplayLockGuard lock(display_);
        
        // 从所有可能的屏幕中查找状态栏
        lv_obj_t* status_bar = FindStatusBar();
        
        if (status_bar != nullptr && lv_obj_is_valid(status_bar)) {
            // 检查状态栏是否已经在目标屏幕上
            lv_obj_t* current_parent = lv_obj_get_parent(status_bar);
            if (current_parent == target_screen) {
                // 已经在目标屏幕上，只需要确保它在最前面且可见
                lv_obj_clear_flag(status_bar, LV_OBJ_FLAG_HIDDEN);
                lv_obj_move_foreground(status_bar);
                ESP_LOGI(TAG, "Status bar already on target screen, moved to foreground");
            } else {
                // 将状态栏移到目标屏幕
                lv_obj_set_parent(status_bar, target_screen);
                lv_obj_clear_flag(status_bar, LV_OBJ_FLAG_HIDDEN);  // 确保可见
                lv_obj_align(status_bar, LV_ALIGN_TOP_MID, 0, 0);
                lv_obj_move_foreground(status_bar);
                ESP_LOGI(TAG, "Status bar moved to target screen %p", target_screen);
            }
        } else {
            ESP_LOGW(TAG, "Status bar not found or invalid, trying to re-find");
            // 清除无效引用，尝试重新查找
            status_bar_ref_ = nullptr;
            status_bar = FindStatusBar();
            if (status_bar != nullptr && lv_obj_is_valid(status_bar) && target_screen != nullptr) {
                lv_obj_set_parent(status_bar, target_screen);
                lv_obj_clear_flag(status_bar, LV_OBJ_FLAG_HIDDEN);  // 确保可见
                lv_obj_align(status_bar, LV_ALIGN_TOP_MID, 0, 0);
                lv_obj_move_foreground(status_bar);
                ESP_LOGI(TAG, "Status bar re-found and moved to target screen");
            } else {
                ESP_LOGE(TAG, "Failed to find status bar, it may not be visible");
            }
        }
    }
    
    // 初始化主菜单
    void InitializeMainMenu() {
        DisplayLockGuard lock(display_);
        
        auto lvgl_theme = static_cast<LvglTheme*>(display_->GetTheme());
        auto screen = lv_screen_active();
        
        // 保存原来的屏幕作为AI界面
        ai_screen_ = screen;
        
        // 创建主菜单屏幕
        menu_screen_ = lv_obj_create(nullptr);
        lv_obj_set_style_bg_color(menu_screen_, lvgl_theme->background_color(), 0);
        lv_obj_set_style_border_width(menu_screen_, 0, 0);
        lv_obj_set_style_pad_all(menu_screen_, 0, 0);
        
        // 创建页面容器（用于滑动）
        menu_page_container_ = lv_obj_create(menu_screen_);
        lv_obj_set_size(menu_page_container_, LV_HOR_RES, LV_VER_RES);
        lv_obj_set_style_bg_color(menu_page_container_, lvgl_theme->background_color(), 0);
        lv_obj_set_style_border_width(menu_page_container_, 0, 0);
        lv_obj_set_style_pad_all(menu_page_container_, 0, 0);
        lv_obj_set_flex_flow(menu_page_container_, LV_FLEX_FLOW_ROW);
        lv_obj_set_scroll_snap_x(menu_page_container_, LV_SCROLL_SNAP_CENTER);
        lv_obj_set_scroll_dir(menu_page_container_, LV_DIR_HOR);
        lv_obj_set_scrollbar_mode(menu_page_container_, LV_SCROLLBAR_MODE_OFF);
        
        // 创建第一页（选项1-4）
        menu_page1_ = lv_obj_create(menu_page_container_);
        lv_obj_set_size(menu_page1_, LV_HOR_RES, LV_VER_RES);
        lv_obj_set_style_bg_color(menu_page1_, lvgl_theme->background_color(), 0);
        lv_obj_set_style_border_width(menu_page1_, 0, 0);
        lv_obj_set_style_pad_all(menu_page1_, 0, 0);
        lv_obj_set_scrollbar_mode(menu_page1_, LV_SCROLLBAR_MODE_OFF);  // 添加这行，禁用滚动条
        
        // 创建第二页（选项5-8）
        menu_page2_ = lv_obj_create(menu_page_container_);
        lv_obj_set_size(menu_page2_, LV_HOR_RES, LV_VER_RES);
        lv_obj_set_style_bg_color(menu_page2_, lvgl_theme->background_color(), 0);
        lv_obj_set_style_border_width(menu_page2_, 0, 0);
        lv_obj_set_style_pad_all(menu_page2_, 0, 0);
        lv_obj_set_scrollbar_mode(menu_page2_, LV_SCROLLBAR_MODE_OFF);  // 添加这行，禁用滚动条
        
        // 使用屏幕宽度自动计算
        int btn_size_w = 34;      // 按钮大小保持不变
        int btn_size_h = 34;
        int btn_spacing_x = 42;   // 改：55 → 42（减小间距）
        int btn_spacing_y = 60;
        int start_x = 44;         // 改：54 → 24（左移，居中）
        int start_y = 50;
        
        // 创建8个选项按钮
        const char* btn_texts[] = {
            "口语陪练", "睡前故事", "拍识万物", "亲子通话", "唐诗宋词", "百科问答", 
            "单词听写", "AI自拍", "快速录音", "声控按摩"
        };
        
        
        // 紫色背景色 (RGB: 156, 39, 176, 对应 #9C27B0)
        lv_color_t purple_color = lv_color_make(156, 39, 176);
        
        for (int i = 0; i < 10; i++) {
            lv_obj_t* parent_page = (i < 6) ? menu_page1_ : menu_page2_;
            
            menu_buttons_[i] = lv_btn_create(parent_page);
            lv_obj_set_size(menu_buttons_[i], btn_size_w, btn_size_h);
            
            // 计算位置（四个象限）
            int page_index = (i < 6) ? i : (i - 6);
            int x = start_x + (page_index % 3) * (btn_size_w + btn_spacing_x);
            int y = start_y + (page_index / 3) * (btn_size_h + btn_spacing_y); // 顶部留20像素
            
            lv_obj_set_pos(menu_buttons_[i], x, y);
            //lv_obj_set_style_radius(menu_buttons_[i], 10, 0);
            // 取消按钮的所有padding（内边距）
            lv_obj_set_style_pad_all(menu_buttons_[i], 0, 0);
            lv_obj_set_style_pad_hor(menu_buttons_[i], 0, 0);  // 水平padding为0
            lv_obj_set_style_pad_ver(menu_buttons_[i], 0, 0);  // 垂直padding为0
            lv_obj_set_style_pad_left(menu_buttons_[i], 0, 0);
            lv_obj_set_style_pad_right(menu_buttons_[i], 0, 0);
            lv_obj_set_style_pad_top(menu_buttons_[i], 0, 0);
            lv_obj_set_style_pad_bottom(menu_buttons_[i], 0, 0);

            // 取消按钮的边框（如果有）
            lv_obj_set_style_border_width(menu_buttons_[i], 0, 0);
            
            // 设置按钮背景色为紫色
            //lv_obj_set_style_bg_color(menu_buttons_[i], purple_color, 0);
            lv_obj_set_style_bg_opa(menu_buttons_[i], LV_OPA_0, 0);
            
            // 创建图片对象
            lv_obj_t* img = lv_image_create(menu_buttons_[i]);
            // 根据索引选择对应的图标
            const lv_image_dsc_t* icon_ptr = nullptr;
            switch(i) {
                case 0: icon_ptr = &kouyu; break;
                case 1: icon_ptr = &shuiqian; break;
                case 2: icon_ptr = &paishi; break;
                case 3: icon_ptr = &qinzi; break;
                case 4: icon_ptr = &tangshi; break;
                case 5: icon_ptr = &baike; break;
                case 6: icon_ptr = &danci; break;  // Q&A图标
                case 7: icon_ptr = &AIzi; break;
                case 8: icon_ptr = &kuaisu; break;
                case 9: icon_ptr = &shengkong; break;
            } 
            if (icon_ptr) {
                lv_image_set_src(img, icon_ptr);
            }
            // 设置图片位置：顶部居中，大小40x40
           // lv_obj_set_pos(img, (btn_size_w - 40) / 2, 8);  // 图片居中，距离顶部8像素
            lv_obj_set_size(img, 34, 34);
            lv_obj_align(img, LV_ALIGN_CENTER, 0, 0);
            // 创建按钮标签（放在图片下方） 
            lv_obj_t* label = lv_label_create(parent_page);  // 创建在页面上，不在按钮上
            lv_obj_set_pos(label, x-10, y + btn_size_h + 6);    // 按钮y坐标 + 按钮高度 + 6像素
            lv_label_set_text(label, btn_texts[i]); 
            lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
            lv_obj_set_style_text_font(label, lvgl_theme->text_font()->font(), 0);
            lv_obj_set_style_text_color(label, lv_color_black(), 0);
            
            // 设置按钮点击事件（使用user_data传递选项ID和board指针）
            MenuButtonData* btn_data = new MenuButtonData{this, i + 1};
            lv_obj_set_user_data(menu_buttons_[i], btn_data);
            
            lv_obj_add_event_cb(menu_buttons_[i], [](lv_event_t* e) {
                if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
                    lv_obj_t* obj = (lv_obj_t*)lv_event_get_current_target(e);
                    if (obj != nullptr) {
                        void* user_data = lv_obj_get_user_data(obj);
                        if (user_data != nullptr) {
                            MenuButtonData* data = reinterpret_cast<MenuButtonData*>(user_data);
                            if (data->board != nullptr) {
                                data->board->OnMenuOptionClicked(data->option_id);
                            }
                        }
                    }
                }
            }, LV_EVENT_CLICKED, nullptr);
        }
        
        // 创建拍照界面屏幕
        camera_screen_ = lv_obj_create(nullptr);
        lv_obj_set_style_bg_color(camera_screen_, lv_color_white(), 0);  // 改为白色背景
        lv_obj_set_style_border_width(camera_screen_, 0, 0);
        lv_obj_set_style_pad_all(camera_screen_, 0, 0);
        
        // 创建蓝牙状态标签（在状态栏下方）
        {
            DisplayLockGuard lock(display_);
            auto lvgl_theme = static_cast<LvglTheme*>(display_->GetTheme());
            camera_ble_status_label_ = lv_label_create(camera_screen_);
            lv_obj_set_size(camera_ble_status_label_, LV_HOR_RES, 30);
            lv_obj_align(camera_ble_status_label_, LV_ALIGN_BOTTOM_MID, 0, -105);   // 在状态栏下方
            lv_obj_set_style_text_align(camera_ble_status_label_, LV_TEXT_ALIGN_CENTER, 0);
            lv_obj_set_style_text_color(camera_ble_status_label_, lv_color_black(), 0);  // 黑色文字（白色背景）
            lv_obj_set_style_text_font(camera_ble_status_label_, lvgl_theme->text_font()->font(), 0);
            lv_label_set_text(camera_ble_status_label_, "请连接xiaozhi-hid");  // 默认显示未连接
        }
        
        // 在拍照界面创建拍照按钮（两个按钮）
        InitializeCameraScreen();
        
        // 将原有状态栏移到最顶层，使其显示在所有页面上
        MoveStatusBarToTop();

        // 为 AI 界面添加右滑返回主菜单手势
          lv_obj_add_event_cb(ai_screen_, [](lv_event_t* e) {
              if (lv_event_get_code(e) == LV_EVENT_GESTURE) {
                  lv_indev_t* indev = lv_indev_active();
                  if (indev) {
                      lv_dir_t dir = lv_indev_get_gesture_dir(indev);
                      if (dir == LV_DIR_RIGHT) {  // 向右滑动
                          auto* board = static_cast<CompactMl307ctBoard*>(lv_event_get_user_data(e));
                          if (board) {
                              board->ReturnToMainMenu();
                          }
                      }
                  }
              }
          }, LV_EVENT_GESTURE, this);
          lv_obj_clear_flag(ai_screen_, LV_OBJ_FLAG_GESTURE_BUBBLE);
        
        // 默认显示主菜单
        lv_screen_load(menu_screen_);
        
        // 加载主菜单后，确保状态栏在主菜单上
        MoveStatusBarToScreen(menu_screen_);
        
        ESP_LOGI(TAG, "Main menu initialized");
    }
    
    // 初始化拍照界面
    void InitializeCameraScreen() {
        DisplayLockGuard lock(display_);
        
        auto lvgl_theme = static_cast<LvglTheme*>(display_->GetTheme());
        
        // 先创建变焦滑块（在按钮栏之前创建，确保滑块在最上层且可见）
        //camera_zoom_slider_ = lv_slider_create(camera_screen_);
        //lv_obj_set_size(camera_zoom_slider_, LV_HOR_RES - 40, 4);  // 宽度略小于屏幕，高度很细
        //lv_obj_align(camera_zoom_slider_, LV_ALIGN_CENTER, 0, -3);  // 在按钮栏上方约100像素
        //lv_slider_set_range(camera_zoom_slider_, 0, 100);  // 设置范围0-100
        //lv_slider_set_value(camera_zoom_slider_, 50, LV_ANIM_OFF);  // 默认值50（中间位置-）
        //
        //// 确保滑块可见且在最前面
        //lv_obj_clear_flag(camera_zoom_slider_, LV_OBJ_FLAG_HIDDEN);
        //lv_obj_move_foreground(camera_zoom_slider_);  // 移到前景层
        //
        //// 设置滑块主轨道样式（统一灰色，左右一致）
        //lv_color_t slider_color = lv_color_make(150, 150, 150);  // 统一的灰色
        //lv_obj_set_style_bg_color(camera_zoom_slider_, slider_color, LV_PART_MAIN);
        //lv_obj_set_style_bg_opa(camera_zoom_slider_, LV_OPA_COVER, LV_PART_MAIN);
        //lv_obj_set_style_radius(camera_zoom_slider_, 2, LV_PART_MAIN);  // 圆角
        //lv_obj_set_style_pad_all(camera_zoom_slider_, 0, LV_PART_MAIN);
        //
        //// 设置指示器样式（已滑过的部分，与主轨道相同的颜色，左右一致）
        //lv_obj_set_style_bg_color(camera_zoom_slider_, slider_color, LV_PART_INDICATOR);
        //lv_obj_set_style_bg_opa(camera_zoom_slider_, LV_OPA_COVER, LV_PART_INDICATOR);
        //lv_obj_set_style_radius(camera_zoom_slider_, 2, LV_PART_INDICATOR);
        //
        //// 设置滑块圆点（KNOB）样式（深灰色圆形，在白色背景上可见）
        //lv_obj_set_style_bg_color(camera_zoom_slider_, lv_color_make(80, 80, 80), LV_PART_KNOB);
        //lv_obj_set_style_bg_opa(camera_zoom_slider_, LV_OPA_COVER, LV_PART_KNOB);
        //lv_obj_set_style_radius(camera_zoom_slider_, LV_RADIUS_CIRCLE, LV_PART_KNOB);
        //lv_obj_set_style_pad_all(camera_zoom_slider_, 6, LV_PART_KNOB);  // 设置圆点大小（通过padding）
        //lv_obj_set_style_border_width(camera_zoom_slider_, 0, LV_PART_KNOB);  // 无边框
        
        // 创建底部按钮栏
        lv_obj_t* camera_bar = lv_obj_create(camera_screen_);
        lv_obj_set_size(camera_bar, LV_HOR_RES, 80);  // 保持高度80
        lv_obj_align(camera_bar, LV_ALIGN_BOTTOM_MID, 0, 0);
        lv_obj_set_style_bg_color(camera_bar, lvgl_theme->background_color(), 0);
        lv_obj_set_style_border_width(camera_bar, 1, 0);
        lv_obj_set_style_border_color(camera_bar, lvgl_theme->border_color(), 0);
        lv_obj_set_style_radius(camera_bar, 0, 0);
        // 移除所有 Flex 布局设置，使用绝对定位
        lv_obj_set_scrollbar_mode(camera_bar, LV_SCROLLBAR_MODE_OFF);
        lv_obj_clear_flag(camera_bar, LV_OBJ_FLAG_SCROLLABLE);  // 禁用滚动
        
        // 创建左侧摄像机图标按钮（切换到录像模式）
        camera_mode_btn_left_ = lv_btn_create(camera_bar);
        lv_obj_set_size(camera_mode_btn_left_, 44, 44);  // 宽度44，高度60（三个按钮一样大）
        lv_obj_set_style_radius(camera_mode_btn_left_, 22, 0);  // 圆形按钮（半径22，对应宽度44）
        // 设置按钮为透明背景
        lv_obj_set_style_bg_opa(camera_mode_btn_left_, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(camera_mode_btn_left_, 0, 0);
        // 使用自定义图标
        lv_obj_t* video_icon_img = lv_image_create(camera_mode_btn_left_);
        lv_image_set_src(video_icon_img, &video_icon);
        lv_obj_center(video_icon_img);
        // 设置绝对位置：距离左边10像素，垂直居中（距离顶部10像素）
        lv_obj_align(camera_mode_btn_left_, LV_ALIGN_LEFT_MID, 40, 0);
        lv_obj_clear_flag(camera_mode_btn_left_, LV_OBJ_FLAG_SCROLLABLE);  // 禁用滚动
        
        // 创建中间拍照/摄影按键
        camera_mode_btn_center_ = lv_btn_create(camera_bar);
        lv_obj_set_size(camera_mode_btn_center_, 44, 60);  // 宽度44，高度60（三个按钮一样大）
        lv_obj_set_style_radius(camera_mode_btn_center_, 22, 0);  // 圆形按钮（半径22）
        // 设置绝对位置：居中 (172-44)/2 = 64，垂直居中（距离顶部10像素）
        //lv_obj_set_pos(camera_mode_btn_center_, 128, 10);
        lv_obj_align(camera_mode_btn_center_, LV_ALIGN_CENTER, 0, 0);
        lv_obj_clear_flag(camera_mode_btn_center_, LV_OBJ_FLAG_SCROLLABLE);  // 禁用滚动
        
        // 创建右侧照相机图标按钮（切换到拍照模式）
        camera_mode_btn_right_ = lv_btn_create(camera_bar);
        lv_obj_set_size(camera_mode_btn_right_, 44, 44);  // 宽度44，高度60（三个按钮一样大）
        lv_obj_set_style_radius(camera_mode_btn_right_, 22, 0);  // 圆形按钮（半径22）
        // 设置按钮为透明背景
        lv_obj_set_style_bg_opa(camera_mode_btn_right_, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(camera_mode_btn_right_, 0, 0);
        // 使用自定义图标
        lv_obj_t* photo_icon_img = lv_image_create(camera_mode_btn_right_);
        lv_image_set_src(photo_icon_img, &camera_icon);
        lv_obj_center(photo_icon_img);
        // 设置绝对位置：x = 10 + 44 + 10 + 44 + 10 = 118，垂直居中（距离顶部10像素）
        //lv_obj_set_pos(camera_mode_btn_right_, 182, 10);
        lv_obj_align(camera_mode_btn_right_, LV_ALIGN_RIGHT_MID, -40, 0);
        lv_obj_clear_flag(camera_mode_btn_right_, LV_OBJ_FLAG_SCROLLABLE);  // 禁用滚动
        
        // 左侧按钮点击事件：切换到录像模式
        lv_obj_add_event_cb(camera_mode_btn_left_, [](lv_event_t* e) {
            if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
                lv_obj_t* obj = (lv_obj_t*)lv_event_get_current_target(e);
                auto* board = static_cast<CompactMl307ctBoard*>(lv_obj_get_user_data(obj));
                if (board) {
                    board->SetCameraMode(CameraMode::CAMERA_MODE_VIDEO);
                }
            }
        }, LV_EVENT_CLICKED, this);
        lv_obj_set_user_data(camera_mode_btn_left_, this);
        
        // 中间按钮点击事件：执行拍照或录像
        lv_obj_add_event_cb(camera_mode_btn_center_, [](lv_event_t* e) {
            if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
                lv_obj_t* obj = (lv_obj_t*)lv_event_get_current_target(e);
                auto* board = static_cast<CompactMl307ctBoard*>(lv_obj_get_user_data(obj));
                if (board) {
                    board->ExecuteCameraAction();
                }
            }
        }, LV_EVENT_CLICKED, this);
        lv_obj_set_user_data(camera_mode_btn_center_, this);
        
        // 右侧按钮点击事件：切换到拍照模式
        lv_obj_add_event_cb(camera_mode_btn_right_, [](lv_event_t* e) {
            if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
                lv_obj_t* obj = (lv_obj_t*)lv_event_get_current_target(e);
                auto* board = static_cast<CompactMl307ctBoard*>(lv_obj_get_user_data(obj));
                if (board) {
                    board->SetCameraMode(CameraMode::CAMERA_MODE_PHOTO);
                }
            }
        }, LV_EVENT_CLICKED, this);
        lv_obj_set_user_data(camera_mode_btn_right_, this);
        
        // 初始化模式为拍照模式
        current_camera_mode_ = CameraMode::CAMERA_MODE_PHOTO;
        UpdateCameraModeDisplay();

         // 为拍照界面添加右滑返回主菜单手势
          lv_obj_add_event_cb(camera_screen_, [](lv_event_t* e) {
              if (lv_event_get_code(e) == LV_EVENT_GESTURE) {
                  lv_indev_t* indev = lv_indev_active();
                  if (indev) {
                      lv_dir_t dir = lv_indev_get_gesture_dir(indev);
                      if (dir == LV_DIR_RIGHT) {  // 向右滑动
                          auto* board = static_cast<CompactMl307ctBoard*>(lv_event_get_user_data(e));
                          if (board) {
                              board->ReturnToMainMenu();
                          }
                      }
                  }
              }
          }, LV_EVENT_GESTURE, this);
          lv_obj_clear_flag(camera_screen_, LV_OBJ_FLAG_GESTURE_BUBBLE);
      
    }
    
    // 设置拍照/录像模式
    void SetCameraMode(CameraMode mode) {
        if (current_camera_mode_ == mode) {
            return;  // 已经是该模式，不需要切换
        }
        current_camera_mode_ = mode;
        UpdateCameraModeDisplay();
        ESP_LOGI(TAG, "Camera mode switched to: %s", mode == CameraMode::CAMERA_MODE_PHOTO ? "PHOTO" : "VIDEO");
    }
    
    // 更新模式显示（更新中间按钮的样式）
    void UpdateCameraModeDisplay() {
        if (camera_mode_btn_center_ == nullptr) {
            return;
        }
        
        DisplayLockGuard lock(display_);
        
        // 清除所有样式，包括主题样式
        lv_obj_remove_style_all(camera_mode_btn_center_);
        
        // 重新设置按钮基本属性（确保大小是44x60）
        lv_obj_set_size(camera_mode_btn_center_, 44, 44);  // 宽度44，高度60
        lv_obj_align(camera_mode_btn_center_, LV_ALIGN_CENTER, 0, 0);
        lv_obj_set_style_radius(camera_mode_btn_center_, 22, 0);  // 圆形按钮（半径22）
        // 设置绝对位置：居中 (172-44)/2 = 64，垂直居中（距离顶部10像素）
        //lv_obj_set_pos(camera_mode_btn_center_, 128, 10);
        lv_obj_clear_flag(camera_mode_btn_center_, LV_OBJ_FLAG_SCROLLABLE);  // 禁用滚动
        
        if (current_camera_mode_ == CameraMode::CAMERA_MODE_PHOTO) {
            // 照相机模式：灰色实心圆圈（灰色背景，不透明）
            lv_obj_set_style_bg_color(camera_mode_btn_center_, lv_color_make(150, 150, 150), 0);  // 灰色背景
            lv_obj_set_style_bg_opa(camera_mode_btn_center_, LV_OPA_COVER, 0);  // 不透明背景
            lv_obj_set_style_border_width(camera_mode_btn_center_, 0, 0);  // 无边框
            lv_obj_set_style_border_opa(camera_mode_btn_center_, LV_OPA_TRANSP, 0);
            
            // 按下状态也保持灰色实心圆圈
            lv_obj_set_style_bg_color(camera_mode_btn_center_, lv_color_make(150, 150, 150), LV_STATE_PRESSED);
            lv_obj_set_style_bg_opa(camera_mode_btn_center_, LV_OPA_COVER, LV_STATE_PRESSED);
            lv_obj_set_style_border_width(camera_mode_btn_center_, 0, LV_STATE_PRESSED);
            lv_obj_set_style_border_opa(camera_mode_btn_center_, LV_OPA_TRANSP, LV_STATE_PRESSED);
        } else {
           if (is_recording_) {
        // 正在录像：红色空心圆圈（红色边框，透明背景）
        lv_obj_set_style_bg_opa(camera_mode_btn_center_, LV_OPA_TRANSP, 0);  // 透明背景
        lv_obj_set_style_border_color(camera_mode_btn_center_, lv_color_make( 0, 0,255), 0);  // 红色边框
        lv_obj_set_style_border_width(camera_mode_btn_center_, 4, 0);  // 4像素边框
        lv_obj_set_style_border_opa(camera_mode_btn_center_, LV_OPA_COVER, 0);
        
        // 按下状态也保持红色空心圆圈
        lv_obj_set_style_bg_opa(camera_mode_btn_center_, LV_OPA_TRANSP, LV_STATE_PRESSED);
        lv_obj_set_style_border_color(camera_mode_btn_center_, lv_color_make( 0, 0,255), LV_STATE_PRESSED);
        lv_obj_set_style_border_width(camera_mode_btn_center_, 4, LV_STATE_PRESSED);
        lv_obj_set_style_border_opa(camera_mode_btn_center_, LV_OPA_COVER, LV_STATE_PRESSED);
            } else {
        // 未录像：红色实心圆圈（红色背景，不透明）
        lv_obj_set_style_bg_color(camera_mode_btn_center_, lv_color_make( 0, 0,255), 0);  // 红色背景
        lv_obj_set_style_bg_opa(camera_mode_btn_center_, LV_OPA_COVER, 0);  // 不透明背景
        lv_obj_set_style_border_width(camera_mode_btn_center_, 0, 0);  // 无边框
        lv_obj_set_style_border_opa(camera_mode_btn_center_, LV_OPA_TRANSP, 0);
        
        // 按下状态也保持红色实心圆圈
        lv_obj_set_style_bg_color(camera_mode_btn_center_, lv_color_make( 0, 0,255), LV_STATE_PRESSED);
        lv_obj_set_style_bg_opa(camera_mode_btn_center_, LV_OPA_COVER, LV_STATE_PRESSED);
        lv_obj_set_style_border_width(camera_mode_btn_center_, 0, LV_STATE_PRESSED);
        lv_obj_set_style_border_opa(camera_mode_btn_center_, LV_OPA_TRANSP, LV_STATE_PRESSED);
        }
    }
}
    // 2. 修改 ExecuteCameraAction 函数（第974-994行）
// 执行拍照或录像动作
void ExecuteCameraAction() {
    if (current_camera_mode_ == CameraMode::CAMERA_MODE_PHOTO) {
        // 拍照模式：发送音量-指令（或执行拍照）
        ESP_LOGI(TAG, "Taking photo (camera mode)");
        auto& ble_hid = BleHid::GetInstance();
        if (ble_hid.IsConnected()) {
            ble_hid.SendVolumeDown();
        } else {
            ESP_LOGW(TAG, "BLE HID not connected, cannot send volume down");
        }
    } else {
        // 录像模式：切换录像状态
        is_recording_ = !is_recording_;  // 切换录像状态
        if (is_recording_) {
            ESP_LOGI(TAG, "Start recording video");
            auto& ble_hid = BleHid::GetInstance();
            if (ble_hid.IsConnected()) {
                ble_hid.SendVolumeUp();  // 开始录像
            } else {
                ESP_LOGW(TAG, "BLE HID not connected, cannot start recording");
            }
        } else {
            ESP_LOGI(TAG, "Stop recording video");
            auto& ble_hid = BleHid::GetInstance();
            if (ble_hid.IsConnected()) {
                ble_hid.SendVolumeUp();  // 停止录像（可能需要发送另一个指令）
            } else {
                ESP_LOGW(TAG, "BLE HID not connected, cannot stop recording");
            }
        }
        // 更新显示
        UpdateCameraModeDisplay();
    }
}
    
    // 更新拍照界面的蓝牙连接状态显示
    void UpdateCameraBleStatus(bool connected) {
        if (camera_ble_status_label_ == nullptr) {
            return;
        }
        
        DisplayLockGuard lock(display_);
        
        if (connected) {
            lv_label_set_text(camera_ble_status_label_, "已连接");
        } else {
            lv_label_set_text(camera_ble_status_label_, "请连接xiaozhi-hid");
        }
    }
    
    // 菜单选项点击处理
    void OnMenuOptionClicked(int option_id) {
        ESP_LOGI(TAG, "Menu option %d clicked", option_id);
        
        DisplayLockGuard lock(display_);
        
        if (option_id >= 1 && option_id <= 7) {
            // 选项1-7：切换到AI界面
            // 如果之前在拍照界面，先关闭蓝牙
            lv_obj_t* current_screen = lv_screen_active();
            if (current_screen == camera_screen_) {
                ESP_LOGI(TAG, "Leaving camera screen, deinitializing BLE");
                auto& ble_hid = BleHid::GetInstance();
                ble_hid.Deinitialize();
            }
            lv_screen_load(ai_screen_);
            // 将状态栏移到AI界面
            MoveStatusBarToScreen(ai_screen_);
            
            // 如果是"拍识万物"（选项3），添加左滑手势进入相册
            if (option_id == 3) {
                AddGallerySwipeGesture();
                // 启动摄像头预览定时器，每100ms更新一次画面（约10fps）
                if (camera_preview_timer_ != nullptr && camera_ != nullptr) {
                    esp_timer_stop(camera_preview_timer_);  // 先停止（如果正在运行）
                    // 立即显示第一帧
                    camera_->Capture();
                    // 启动定时器，每100ms更新一次
                    ESP_ERROR_CHECK(esp_timer_start_periodic(camera_preview_timer_, 100 * 1000));  // 100ms = 100000微秒
                    ESP_LOGI(TAG, "Camera preview started for '拍识万物' mode");
                }
            } else {
                // 其他选项，移除左滑手势
                RemoveGallerySwipeGesture();
                if (camera_preview_timer_ != nullptr) {
                    esp_timer_stop(camera_preview_timer_);
                    ESP_LOGI(TAG, "Camera preview stopped");
                }
            }
        } else if (option_id == 8) {
            // 选项8：切换到拍照界面，初始化蓝牙
            ESP_LOGI(TAG, "Entering camera screen, initializing BLE");
            auto& ble_hid = BleHid::GetInstance();
            ble_hid.Initialize();  // Initialize() 内部会检查是否已初始化
            
            // 注册 BLE HID 连接状态变化回调
            ble_hid.OnConnectionStateChanged([this](bool connected) {
                ESP_LOGI(TAG, "BLE HID connection state changed: %s", connected ? "connected" : "disconnected");
                // 更新拍照界面的蓝牙状态显示
                UpdateCameraBleStatus(connected);
            });
            
            // 初始化时更新蓝牙状态显示
            UpdateCameraBleStatus(ble_hid.IsConnected());
            
            lv_screen_load(camera_screen_);
            // 将状态栏移到拍照界面
            MoveStatusBarToScreen(camera_screen_);
            
            // 切换到拍照界面时，移除左滑手势
            RemoveGallerySwipeGesture();
            if (camera_preview_timer_ != nullptr) {
                esp_timer_stop(camera_preview_timer_);
                ESP_LOGI(TAG, "Camera preview stopped when entering camera screen");
            }
        }
    }
    
    // 在AI界面添加左滑手势进入相册
    void AddGallerySwipeGesture() {
        if (ai_screen_ == nullptr) {
            return;
        }
        
        // 先移除之前的手势（如果存在）
        RemoveGallerySwipeGesture();
        
        // 添加左滑手势事件
        lv_obj_add_event_cb(ai_screen_, [](lv_event_t* e) {
            if (lv_event_get_code(e) == LV_EVENT_GESTURE) {
                lv_indev_t* indev = lv_indev_active();
                if (indev) {
                    lv_dir_t dir = lv_indev_get_gesture_dir(indev);
                    if (dir == LV_DIR_LEFT) {  // 向左滑动
                        auto* board = static_cast<CompactMl307ctBoard*>(lv_event_get_user_data(e));
                        if (board) {
                            board->ShowGallery();
                        }
                    }
                }
            }
        }, LV_EVENT_GESTURE, this);
        lv_obj_clear_flag(ai_screen_, LV_OBJ_FLAG_GESTURE_BUBBLE);  // 不向上传递手势事件
    }
    
    // 从AI界面移除左滑手势
    void RemoveGallerySwipeGesture() {
        if (ai_screen_ == nullptr) {
            return;
        }
        
        // 移除手势事件回调（通过查找并移除特定的事件回调）
        // 注意：这里使用 nullptr 会移除所有事件回调，如果有其他事件回调需要保留，
        // 需要更精确的管理方式
        lv_obj_remove_event_cb(ai_screen_, nullptr);
    }
    
    // 返回主菜单的方法（可以在AI界面和拍照界面调用）
    void ReturnToMainMenu() {
        DisplayLockGuard lock(display_);
        
        // 如果当前在拍照界面，关闭蓝牙
        lv_obj_t* current_screen = lv_screen_active();
        if (current_screen == camera_screen_) {
            ESP_LOGI(TAG, "Returning from camera screen, deinitializing BLE");
            auto& ble_hid = BleHid::GetInstance();
            ble_hid.Deinitialize();
        }

        // 停止摄像头预览定时器
        if (camera_preview_timer_ != nullptr) {
            esp_timer_stop(camera_preview_timer_);
            ESP_LOGI(TAG, "Camera preview stopped when returning to main menu");
        }
        
        // 先加载主菜单屏幕
        lv_screen_load(menu_screen_);
        
        // 确保状态栏移到主菜单并显示
        MoveStatusBarToScreen(menu_screen_);
        
        ESP_LOGI(TAG, "Returned to main menu, status bar should be visible");
    }

    // 相册相关函数
    void InitializeGallery() {
        DisplayLockGuard lock(display_);
        
        gallery_screen_ = lv_obj_create(nullptr);
        lv_obj_set_style_bg_color(gallery_screen_, lv_color_white(), 0);
        lv_obj_set_style_border_width(gallery_screen_, 0, 0);
        lv_obj_set_style_pad_all(gallery_screen_, 0, 0);
        
        // 使用 LVGL 原生列表控件（可滚动，避开状态栏）
        gallery_list_ = lv_list_create(gallery_screen_);
        lv_obj_set_size(gallery_list_, LV_HOR_RES, LV_VER_RES - 27);  // 减去状态栏(27)
        lv_obj_align(gallery_list_, LV_ALIGN_TOP_MID, 0, 27);  // 从状态栏下方开始
        lv_obj_set_style_bg_color(gallery_list_, lv_color_white(), 0);
        lv_obj_set_style_border_width(gallery_list_, 0, 0);
        lv_obj_set_style_pad_all(gallery_list_, 0, 0);  // 列表控件自己管理padding
        
        // 添加右滑返回手势（在相册列表上）
        lv_obj_add_event_cb(gallery_screen_, [](lv_event_t* e) {
            if (lv_event_get_code(e) == LV_EVENT_GESTURE) {
                lv_indev_t* indev = lv_indev_active();
                if (indev) {
                    lv_dir_t dir = lv_indev_get_gesture_dir(indev);
                    if (dir == LV_DIR_RIGHT) {  // 向右滑动
                        auto* board = static_cast<CompactMl307ctBoard*>(lv_event_get_user_data(e));
                        if (board) {
                            board->ReturnToCameraScreen();
                        }
                    }
                }
            }
        }, LV_EVENT_GESTURE, this);
        lv_obj_clear_flag(gallery_screen_, LV_OBJ_FLAG_GESTURE_BUBBLE);  // 不向上传递手势事件
    }
    
    void ShowGallery() {
        DisplayLockGuard lock(display_);
        
        // 释放当前图片对象（如果从查看器返回）
        current_photo_image_.reset();  // 释放图片对象（会自动释放内存）
        current_photo_img_ = nullptr;

         // 停止摄像头预览定时器
      if (camera_preview_timer_ != nullptr) {
          esp_timer_stop(camera_preview_timer_);
          ESP_LOGI(TAG, "Camera preview stopped when entering gallery");
      }
        
        // 如果相册界面未初始化，先初始化
        if (gallery_screen_ == nullptr) {
            InitializeGallery();
        }
        
        // 刷新图片列表
        RefreshGalleryList();
        
        // 切换到相册界面
        lv_screen_load(gallery_screen_);
        MoveStatusBarToScreen(gallery_screen_);
    }
    
    void ReturnToCameraScreen() {
        DisplayLockGuard lock(display_);
        lv_screen_load(ai_screen_);  // 返回AI界面（拍识万物）
        MoveStatusBarToScreen(ai_screen_);
        
        // 重新添加左滑手势（因为返回的是拍识万物界面）
        AddGallerySwipeGesture();

        // 重新启动摄像头预览定时器
      if (camera_preview_timer_ != nullptr && camera_ != nullptr) {
          esp_timer_stop(camera_preview_timer_);  // 先停止（防止重复启动）
          // 立即显示第一帧
          camera_->Capture();
          // 启动定时器，每200ms更新一次
          ESP_ERROR_CHECK(esp_timer_start_periodic(camera_preview_timer_, 200 * 1000));
          ESP_LOGI(TAG, "Camera preview restarted when returning to camera screen");
      }
    }
    
    // 显示删除确认对话框
    void ShowDeleteConfirmDialog(const char* filename) {
        DisplayLockGuard lock(display_);
        
        // 创建模态对话框
        lv_obj_t* mbox = lv_msgbox_create(nullptr);
        
        // 添加标题
        lv_msgbox_add_title(mbox, "Delete Photo");
        
        // 添加文本内容
        std::string display_name = filename;
        size_t dot_pos = display_name.find_last_of('.');
        if (dot_pos != std::string::npos) {
            display_name = display_name.substr(0, dot_pos);
        }
        std::string msg_text = "确定要删除照片\n\"" + display_name + "\" 吗？";
        lv_msgbox_add_text(mbox, msg_text.c_str());
        
        // 添加按钮
        lv_obj_t* cancel_btn = lv_msgbox_add_footer_button(mbox, "Cancel");
        lv_obj_t* delete_btn = lv_msgbox_add_footer_button(mbox, "Delete");
        
        // 设置删除按钮为红色
        lv_obj_set_style_text_color(delete_btn, lv_color_make(255, 0, 0), 0);
        
        // 保存文件名到对话框的 user_data
        char* filename_copy = (char*)malloc(strlen(filename) + 1);
        strcpy(filename_copy, filename);
        lv_obj_set_user_data(mbox, filename_copy);
        
        // 取消按钮事件：关闭对话框
        lv_obj_add_event_cb(cancel_btn, [](lv_event_t* e) {
            if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
                lv_obj_t* btn = (lv_obj_t*)lv_event_get_current_target(e);
                // 通过向上查找找到 msgbox（footer -> msgbox）
                lv_obj_t* footer = lv_obj_get_parent(btn);
                lv_obj_t* mbox = lv_obj_get_parent(footer);
                
                // 释放文件名内存
                char* filename = (char*)lv_obj_get_user_data(mbox);
                if (filename) {
                    free(filename);
                }
                lv_msgbox_close(mbox);
            }
        }, LV_EVENT_CLICKED, nullptr);
        
        // 删除按钮事件：删除文件并刷新列表
        lv_obj_add_event_cb(delete_btn, [](lv_event_t* e) {
            if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
                lv_obj_t* btn = (lv_obj_t*)lv_event_get_current_target(e);
                // 通过向上查找找到 msgbox（footer -> msgbox）
                lv_obj_t* footer = lv_obj_get_parent(btn);
                lv_obj_t* mbox = lv_obj_get_parent(footer);
                const char* filename = (const char*)lv_obj_get_user_data(mbox);
                
                if (filename) {
                    auto* board = static_cast<CompactMl307ctBoard*>(lv_event_get_user_data(e));
                    if (board) {
                        board->DeletePhoto(filename);
                    }
                    
                    // 释放文件名内存
                    free((void*)filename);
                }
                
                // 关闭对话框
                lv_msgbox_close(mbox);
            }
        }, LV_EVENT_CLICKED, this);
    }
    
    // 删除照片文件
    void DeletePhoto(const char* filename) {
        char filepath[128];
        snprintf(filepath, sizeof(filepath), "/storage/%s", filename);
        
        // 删除文件
        if (unlink(filepath) == 0) {
            ESP_LOGI(TAG, "Photo deleted: %s", filepath);
            
            // 刷新相册列表
            RefreshGalleryList();
        } else {
            ESP_LOGE(TAG, "Failed to delete photo: %s", filepath);
        }
    }
    
    void RefreshGalleryList() {
        if (gallery_list_ == nullptr) {
            return;
        }
        
        // 清空现有列表
        lv_obj_clean(gallery_list_);
        photo_files_.clear();
        
        // 读取 /storage 目录下的文件
        DIR* dir = opendir("/storage");
        if (dir == nullptr) {
            ESP_LOGE(TAG, "Failed to open /storage directory");
            lv_list_add_text(gallery_list_, "暂无照片");
            return;
        }
        
        struct dirent* entry;
        while ((entry = readdir(dir)) != nullptr) {
            // 只处理 .raw 文件
            if (entry->d_type == DT_REG) {
                const char* name = entry->d_name;
                size_t len = strlen(name);
                if (len > 4 && strcmp(name + len - 4, ".raw") == 0) {
                    photo_files_.push_back(std::string(name));
                }
            }
        }
        closedir(dir);
        
        if (photo_files_.empty()) {
        lv_list_add_text(gallery_list_, "暂无照片");
        return;
    }
        
        // 按文件名排序（最新的在前，文件名包含时间戳）
        std::sort(photo_files_.begin(), photo_files_.end(), std::greater<std::string>());
        
         // 使用 lv_list_add_button 添加列表项
    for (size_t i = 0; i < photo_files_.size(); i++) {
        // 去掉 .raw 后缀显示
        std::string display_name = photo_files_[i];
        size_t dot_pos = display_name.find_last_of('.');
        if (dot_pos != std::string::npos) {
            display_name = display_name.substr(0, dot_pos);
        }
        
        // 添加列表按钮（带右箭头图标）
        lv_obj_t* btn = lv_list_add_button(gallery_list_, LV_SYMBOL_RIGHT, display_name.c_str());
        
        // 设置按钮样式
        lv_obj_set_style_text_color(btn, lv_color_black(), 0);
        
        // 保存文件名到 user_data（用于点击事件）
        char* filename_copy = (char*)malloc(photo_files_[i].length() + 1);
        strcpy(filename_copy, photo_files_[i].c_str());
        lv_obj_set_user_data(btn, filename_copy);
            
            
        // 点击事件：查看大图
        lv_obj_add_event_cb(btn, [](lv_event_t* e) {
            if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
                lv_obj_t* obj = (lv_obj_t*)lv_event_get_current_target(e);
                const char* filename = (const char*)lv_obj_get_user_data(obj);
                if (filename) {
                    auto* board = static_cast<CompactMl307ctBoard*>(lv_event_get_user_data(e));
                    if (board) {
                        board->ShowPhotoViewer(filename);
                    }
                }
            }
        }, LV_EVENT_CLICKED, this);
        
        // 长按事件：显示删除确认对话框
        lv_obj_add_event_cb(btn, [](lv_event_t* e) {
            if (lv_event_get_code(e) == LV_EVENT_LONG_PRESSED) {
                lv_obj_t* obj = (lv_obj_t*)lv_event_get_current_target(e);
                const char* filename = (const char*)lv_obj_get_user_data(obj);
                if (filename) {
                    auto* board = static_cast<CompactMl307ctBoard*>(lv_event_get_user_data(e));
                    if (board) {
                        board->ShowDeleteConfirmDialog(filename);
                    }
                }
            }
        }, LV_EVENT_LONG_PRESSED, this);
        }
    }
    
    void ShowPhotoViewer(const char* filename) {
        DisplayLockGuard lock(display_);
        
        // 创建图片查看器界面
        if (photo_viewer_screen_ == nullptr) {
            photo_viewer_screen_ = lv_obj_create(nullptr);
            lv_obj_set_style_bg_color(photo_viewer_screen_, lv_color_black(), 0);
            lv_obj_set_style_border_width(photo_viewer_screen_, 0, 0);
            lv_obj_set_style_pad_all(photo_viewer_screen_, 0, 0);
        }     
        
        // 添加右滑返回手势（在图片查看器上）
        lv_obj_add_event_cb(photo_viewer_screen_, [](lv_event_t* e) {
            if (lv_event_get_code(e) == LV_EVENT_GESTURE) {
                lv_indev_t* indev = lv_indev_active();
                if (indev) {
                    lv_dir_t dir = lv_indev_get_gesture_dir(indev);
                    if (dir == LV_DIR_RIGHT) {  // 向右滑动
                        auto* board = static_cast<CompactMl307ctBoard*>(lv_event_get_user_data(e));
                        if (board) {
                            board->ShowGallery();
                        }
                    }
                }
            }
        }, LV_EVENT_GESTURE, this);
        lv_obj_clear_flag(photo_viewer_screen_, LV_OBJ_FLAG_GESTURE_BUBBLE);  // 不向上传递手势事件
        
        // 释放之前图片的对象
        current_photo_image_.reset();  // 释放之前的图片对象（会自动释放内存）
        current_photo_img_ = nullptr;
        
        // 清空之前的图片
        lv_obj_clean(photo_viewer_screen_);
        // 创建图片对象
        current_photo_img_ = lv_image_create(photo_viewer_screen_);
        //lv_obj_set_size(current_photo_img_, 160, 80);
        lv_obj_align(current_photo_img_, LV_ALIGN_CENTER, 0, 0);
        
        // 构建文件路径并加载RAW图片
        char filepath[128];
        snprintf(filepath, sizeof(filepath), "/storage/%s", filename);
        LoadRawImage(current_photo_img_, filepath);
        
        // 切换到图片查看器
        lv_screen_load(photo_viewer_screen_);
        MoveStatusBarToScreen(photo_viewer_screen_);
    }
    
    // 加载RAW格式图片（RGB565）
    void LoadRawImage(lv_obj_t* img_obj, const char* filepath) {
        FILE* f = fopen(filepath, "rb");
        if (f == nullptr) {
            ESP_LOGE(TAG, "Failed to open RAW file: %s", filepath);
            lv_obj_t* error_label = lv_label_create(lv_obj_get_parent(img_obj));
            lv_label_set_text(error_label, "无法加载图片");
            lv_obj_set_style_text_color(error_label, lv_color_white(), 0);
            lv_obj_center(error_label);
            return;
        }
        
        // 读取文件头：宽度和高度（各2字节）
        uint16_t width, height;
        if (fread(&width, 2, 1, f) != 1 || fread(&height, 2, 1, f) != 1) {
            ESP_LOGE(TAG, "Failed to read RAW file header");
            fclose(f);
            lv_obj_t* error_label = lv_label_create(lv_obj_get_parent(img_obj));
            lv_label_set_text(error_label, "读取文件头失败");
            lv_obj_set_style_text_color(error_label, lv_color_white(), 0);
            lv_obj_center(error_label);
            return;
        }
        
        uint32_t pixel_count = width * height;
        uint32_t data_size = pixel_count * 2;  // RGB565 = 2 bytes per pixel
        
        ESP_LOGI(TAG, "RAW info: %dx%d, size: %u bytes", width, height, data_size);
        
        // 读取像素数据到PSRAM
        uint8_t* data = (uint8_t*)heap_caps_malloc(data_size, MALLOC_CAP_SPIRAM);
        if (data == nullptr) {
            ESP_LOGE(TAG, "Failed to allocate memory for RAW image");
            fclose(f);
            lv_obj_t* error_label = lv_label_create(lv_obj_get_parent(img_obj));
            lv_label_set_text(error_label, "内存不足");
            lv_obj_set_style_text_color(error_label, lv_color_white(), 0);
            lv_obj_center(error_label);
            return;
        }
        
        if (fread(data, 1, data_size, f) != data_size) {
            ESP_LOGE(TAG, "Failed to read RAW pixel data");
            free(data);
            fclose(f);
            lv_obj_t* error_label = lv_label_create(lv_obj_get_parent(img_obj));
            lv_label_set_text(error_label, "读取像素数据失败");
            lv_obj_set_style_text_color(error_label, lv_color_white(), 0);
            lv_obj_center(error_label);
            return;
        }
        fclose(f);
        
        // 做字节交换（与预览图片一致）
        uint16_t* rgb565 = (uint16_t*)data;
        for (uint32_t i = 0; i < pixel_count; i++) {
            rgb565[i] = __builtin_bswap16(rgb565[i]);
        }
        
        // 使用 LvglAllocatedImage 创建图片对象（与预览图片一致）
        current_photo_image_ = std::make_unique<LvglAllocatedImage>(data, data_size, width, height, width * 2, LV_COLOR_FORMAT_RGB565);
        
        // 获取图片描述符
        auto img_dsc = current_photo_image_->image_dsc();
        
        // 设置图片源
        lv_image_set_src(img_obj, img_dsc);
        
        // 计算缩放（与预览图片一致）
        lv_coord_t max_w = LV_HOR_RES;
        lv_coord_t max_h = LV_VER_RES - 50;  // 留出顶部空间
        lv_coord_t scale_w = (max_w * 256) / width;
        lv_coord_t scale_h = (max_h * 256) / height;
        lv_coord_t scale = (scale_w < scale_h) ? scale_w : scale_h;
        if (scale > 256) scale = 256;  // 不超过100%
        
        lv_image_set_scale(img_obj, scale);
        
        // 设置图片对象大小（参考预览图片的初始化）
        lv_obj_set_size(img_obj, (width * scale) / 256, (height * scale) / 256);
        
        // 居中显示
        lv_obj_center(img_obj);
    }
    

    void InitializeCamera() {
        camera_config_t config = {};
        config.pin_d0 = CAMERA_PIN_D0;
        config.pin_d1 = CAMERA_PIN_D1;
        config.pin_d2 = CAMERA_PIN_D2;
        config.pin_d3 = CAMERA_PIN_D3;
        config.pin_d4 = CAMERA_PIN_D4;
        config.pin_d5 = CAMERA_PIN_D5;
        config.pin_d6 = CAMERA_PIN_D6;
        config.pin_d7 = CAMERA_PIN_D7;
        config.pin_xclk = CAMERA_PIN_XCLK;
        config.pin_pclk = CAMERA_PIN_PCLK;
        config.pin_vsync = CAMERA_PIN_VSYNC;
        config.pin_href = CAMERA_PIN_HREF;
        config.pin_sccb_sda = -1;  
        config.pin_sccb_scl = CAMERA_PIN_SIOC;
        config.sccb_i2c_port = 0;
        config.pin_pwdn = CAMERA_PIN_PWDN;
        config.pin_reset = CAMERA_PIN_RESET;
        config.xclk_freq_hz = XCLK_FREQ_HZ;
        config.pixel_format = PIXFORMAT_RGB565;
        config.frame_size = FRAMESIZE_QVGA;
        config.jpeg_quality = 12;
        config.fb_count = 1;
        config.fb_location = CAMERA_FB_IN_PSRAM;
        config.grab_mode = CAMERA_GRAB_WHEN_EMPTY;
        camera_ = new Esp32Camera(config);
        camera_->SetHMirror(false);
    }

    void InitializeButtons() {
        boot_button_.OnClick([this]() {
            auto& app = Application::GetInstance();
            if (GetNetworkType() == NetworkType::WIFI) {
                if (app.GetDeviceState() == kDeviceStateStarting && !WifiStation::GetInstance().IsConnected()) {
                    auto& wifi_board = static_cast<WifiBoard&>(GetCurrentBoard());
                    wifi_board.ResetWifiConfiguration();
                }
            }
            app.ToggleChatState();
            // 添加：如果 BLE HID 已连接，发送音量控制
            //auto& ble_hid = BleHid::GetInstance();
            //if (ble_hid.IsConnected()) {
            //    ble_hid.SendVolumeUp();  // 或 SendVolumeDown()
            //}
        });
        boot_button_.OnDoubleClick([this]() {
            auto& app = Application::GetInstance();
            if (app.GetDeviceState() == kDeviceStateStarting || app.GetDeviceState() == kDeviceStateWifiConfiguring) {
                // 根据当前网络类型决定ML307_EN_PIN状态
            if (GetNetworkType() == NetworkType::WIFI) {
                // 当前是WiFi，即将切换到4G，启用ML307
                SetML307Enable(true);
                ESP_LOGI(TAG, "Switching to 4G, enabling ML307");
            } else {
                // 当前是4G，即将切换到WiFi，保持ML307启用状态
                // WiFi连接成功时会在StartNetwork中自动禁用
                ESP_LOGI(TAG, "Switching to WiFi, ML307 will be disabled when WiFi connects");
            }
                SwitchNetworkType();
            }
        });
    }

    // 物联网初始化，添加对 AI 可见设备
    void MountStorage() {
        // 挂载 storage 分区用于保存图片
        esp_vfs_spiffs_conf_t conf = {
            .base_path = "/storage",
            .partition_label = "storage",
            .max_files = 10,  // 可以同时打开的文件数
            .format_if_mount_failed = true,  // 如果挂载失败，自动格式化
        };
        
        esp_err_t ret = esp_vfs_spiffs_register(&conf);
        if (ret != ESP_OK) {
            if (ret == ESP_FAIL) {
                ESP_LOGE(TAG, "Failed to mount or format filesystem");
            } else if (ret == ESP_ERR_NOT_FOUND) {
                ESP_LOGE(TAG, "Failed to find SPIFFS partition");
            } else {
                ESP_LOGE(TAG, "Failed to initialize SPIFFS (%s)", esp_err_to_name(ret));
            }
            return;
        }
        
        size_t total = 0, used = 0;
        ret = esp_spiffs_info("storage", &total, &used);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to get SPIFFS partition information (%s)", esp_err_to_name(ret));
        } else {
            ESP_LOGI(TAG, "Storage partition mounted: total: %d KB, used: %d KB", total / 1024, used / 1024);
        }
    }

    void InitializeTools() {
        static LampController lamp(LAMP_GPIO);
        static Green_Lamp lamp_G(BOT_PWM);
    }

    // 尝试WiFi连接，30秒超时
    bool TryWifiConnection() {
        auto& wifi_station = WifiStation::GetInstance();
        wifi_station.OnScanBegin([this]() {
            auto display = GetDisplay();
            display->ShowNotification(Lang::Strings::SCANNING_WIFI, 30000);
        });
        wifi_station.OnConnect([this](const std::string& ssid) {
            auto display = GetDisplay();
            std::string notification = Lang::Strings::CONNECT_TO;
            notification += ssid;
            notification += "...";
            display->ShowNotification(notification.c_str(), 30000);
        });
        wifi_station.OnConnected([this](const std::string& ssid) {
            auto display = GetDisplay();
            std::string notification = Lang::Strings::CONNECTED_TO;
            notification += ssid;
            display->ShowNotification(notification.c_str(), 30000);
        });
        wifi_station.Start();

        // 等待WiFi连接，超时时间设置为30秒
        return wifi_station.WaitForConnected(30 * 1000);
    }

     // 尝试WiFi配网，30秒超时后切换到4G
     void TryWifiConfigAndSwitchTo4G() {
        auto display = GetDisplay();
        display->ShowNotification("WiFi连接失败，进入配网模式");
        vTaskDelay(pdMS_TO_TICKS(2000));
        
        // 进入配网模式
        auto& application = Application::GetInstance();
        application.SetDeviceState(kDeviceStateWifiConfiguring);

        auto& wifi_ap = WifiConfigurationAp::GetInstance();
        wifi_ap.SetLanguage(Lang::CODE);
        wifi_ap.SetSsidPrefix("Xiaozhi");
        wifi_ap.Start();

        // 等待 1.5 秒显示开发板信息
        vTaskDelay(pdMS_TO_TICKS(1500));

        // 显示 WiFi 配置 AP 的 SSID 和 Web 服务器 URL
        std::string hint = Lang::Strings::CONNECT_TO_HOTSPOT;
        hint += wifi_ap.GetSsid();
        hint += Lang::Strings::ACCESS_VIA_BROWSER;
        hint += wifi_ap.GetWebServerUrl();
        hint += "\n\n";
        
        // 播报配置 WiFi 的提示
        application.Alert(Lang::Strings::WIFI_CONFIG_MODE, hint.c_str(), "gear", Lang::Sounds::OGG_WIFICONFIG);

        // 等待30秒配网
        ESP_LOGI(TAG, "Waiting 30 seconds for WiFi configuration...");
        vTaskDelay(pdMS_TO_TICKS(30 * 1000));
        
        // 检查是否配网成功
        if (!WifiStation::GetInstance().IsConnected()) {
            ESP_LOGI(TAG, "WiFi configuration timeout, switching to 4G");
            wifi_ap.Stop();
            
            // 切换到4G网络
            display->ShowNotification("配网超时，切换到4G网络");
            vTaskDelay(pdMS_TO_TICKS(2000));
            SwitchNetworkType(); // 使用现有的公共方法
        }
    }

public:
    // 更新音量按键状态（根据 BLE HID 连接状态）- 需要在 public 以便延迟任务访问
    void UpdateVolumeButtonsState() {
        if (!volume_down_btn_ || !volume_up_btn_) {
            return;
        }
        
        DisplayLockGuard lock(display_);
        auto& ble_hid = BleHid::GetInstance();
        bool connected = ble_hid.IsConnected();
        
        // 设置按键颜色和可点击状态
        lv_color_t btn_color = connected ? lv_color_hex(0x0066FF) : lv_color_hex(0x404040);  // 蓝色或灰色
        lv_color_t text_color = lv_color_white();
        
        // 更新音量-按键
        lv_obj_set_style_bg_color(volume_down_btn_, btn_color, 0);
        lv_obj_set_style_text_color(lv_obj_get_child(volume_down_btn_, 0), text_color, 0);
        if (connected) {
            lv_obj_add_flag(volume_down_btn_, LV_OBJ_FLAG_CLICKABLE);  // 连接时：可点击
        } else {
            lv_obj_clear_flag(volume_down_btn_, LV_OBJ_FLAG_CLICKABLE);  // 未连接时：不可点击
        }
        
        // 更新音量+按键
        lv_obj_set_style_bg_color(volume_up_btn_, btn_color, 0);
        lv_obj_set_style_text_color(lv_obj_get_child(volume_up_btn_, 0), text_color, 0);
        if (connected) {
            lv_obj_add_flag(volume_up_btn_, LV_OBJ_FLAG_CLICKABLE);  // 连接时：可点击
        } else {
            lv_obj_clear_flag(volume_up_btn_, LV_OBJ_FLAG_CLICKABLE);  // 未连接时：不可点击
        }
        
        ESP_LOGI(TAG, "Volume buttons state updated: %s", connected ? "connected (blue)" : "disconnected (gray)");
    }

    CompactMl307ctBoard() : DualNetworkBoard(ML307_TX_PIN, ML307_RX_PIN, GPIO_NUM_NC,0),
        boot_button_(BOOT_BUTTON_GPIO){

            // 初始化ML307_EN_PIN为高电平（启用ML307）
        if (ML307_EN_PIN != GPIO_NUM_NC) {
            gpio_config_t ml307_en_config = {
                .pin_bit_mask = 1ULL << ML307_EN_PIN,
                .mode = GPIO_MODE_OUTPUT,
                .pull_up_en = GPIO_PULLUP_DISABLE,
                .pull_down_en = GPIO_PULLDOWN_DISABLE,
                .intr_type = GPIO_INTR_DISABLE,
            };
            ESP_ERROR_CHECK(gpio_config(&ml307_en_config));
            SetML307Enable(true); // 上电时启用ML307
        }

        InitializeI2c(); 
        InitializeSpi();
        InitializeLcdDisplay();
        InitializeS620();  // 初始化S620电源管理芯片
        InitializeTouch();  // 初始化触摸
        //InitializeVolumeButtons();  // 创建音量按键（在触摸拦截层之上）
        InitializeMainMenu();  // 初始化主菜单界面（在音量按键之后，确保层级正确）
        InitializeButtons();
        InitializeTools();
        InitializeCamera();
        MountStorage();  // 挂载 storage 分区用于保存图片
        GetBacklight()->RestoreBrightness();

         // 初始化摄像头预览定时器
        esp_timer_create_args_t camera_preview_timer_args = {
            .callback = [](void* arg) {
                CompactMl307ctBoard* board = static_cast<CompactMl307ctBoard*>(arg);
                if (board && board->camera_) {
                    board->camera_->Capture();  // 更新摄像头画面
                }
            },
            .arg = this,
            .dispatch_method = ESP_TIMER_TASK,
            .name = "camera_preview_timer",
            .skip_unhandled_events = true,
        };
        ESP_ERROR_CHECK(esp_timer_create(&camera_preview_timer_args, &camera_preview_timer_));

        // BLE HID 将在进入拍照界面（选项8）时初始化，离开时关闭


        ESP_LOGI(TAG, "CompactMl307ctBoard initialized, network type: %d", (int)GetNetworkType());
        
        // 删除网络设置文件，强制使用默认值
        nvs_handle_t nvs;
        esp_err_t err = nvs_open("network", NVS_READWRITE, &nvs);
        if (err == ESP_OK) {
            nvs_erase_key(nvs, "type");
            nvs_commit(nvs);
            nvs_close(nvs);
            ESP_LOGI(TAG, "Cleared network settings");
        }
        
    }

     // 重写StartNetwork方法
     virtual void StartNetwork() override {
         ESP_LOGI(TAG, "StartNetwork called, current network type: %d", (int)GetNetworkType());
        //if (wifi_config_mode_) {  // 这个变量继承自 WifiBoard
        //  EnterWifiConfigMode();
        //  return;
        //}

        if (GetNetworkType() == NetworkType::WIFI) {
            auto& current_board = GetCurrentBoard();
            auto* wifi_board = dynamic_cast<WifiBoard*>(&current_board);
            if (wifi_board != nullptr && wifi_board->ShouldEnterWifiConfigMode()) {
                ESP_LOGI(TAG, "force_ap flag detected, entering WiFi config mode");
                // 调用 WifiBoard 的 StartNetwork 进入配网模式
                current_board.StartNetwork();
                return;
            }
            auto display = GetDisplay();
            display->SetStatus(Lang::Strings::CONNECTING);
             ESP_LOGI(TAG, "Starting WiFi connection attempt");
            
            // 尝试WiFi连接（30秒）
            if (!TryWifiConnection()) {
                ESP_LOGI(TAG, "WiFi connection failed, trying configuration mode");
                WifiStation::GetInstance().Stop();
                
                // 尝试配网模式（30秒）
                TryWifiConfigAndSwitchTo4G();
                return;
            }
       ESP_LOGI(TAG, "WiFi connection successful");
       SetML307Enable(false);
       return;
        } else {
            ESP_LOGI(TAG, "Network type is not WiFi, using 4G");
            SetML307Enable(true);
        }
        
        // WiFi连接成功或已经是4G网络，使用父类的StartNetwork方法
        DualNetworkBoard::StartNetwork();
    }

    virtual Led* GetLed() override {
        static SingleLed led(BUILTIN_LED_GPIO);
        return &led;
    }

    virtual AudioCodec* GetAudioCodec() override {
#ifdef AUDIO_I2S_METHOD_SIMPLEX
        static NoAudioCodecSimplex audio_codec(AUDIO_INPUT_SAMPLE_RATE, AUDIO_OUTPUT_SAMPLE_RATE,
            AUDIO_I2S_SPK_GPIO_BCLK, AUDIO_I2S_SPK_GPIO_LRCK, AUDIO_I2S_SPK_GPIO_DOUT, AUDIO_I2S_MIC_GPIO_SCK, AUDIO_I2S_MIC_GPIO_WS, AUDIO_I2S_MIC_GPIO_DIN);
#else
        static NoAudioCodecDuplex audio_codec(AUDIO_INPUT_SAMPLE_RATE, AUDIO_OUTPUT_SAMPLE_RATE,
            AUDIO_I2S_GPIO_BCLK, AUDIO_I2S_GPIO_WS, AUDIO_I2S_GPIO_DOUT, AUDIO_I2S_GPIO_DIN);
#endif
        return &audio_codec;
    }

    virtual Display* GetDisplay() override {
        return display_;
    }

    virtual Camera* GetCamera() override {
        return camera_;
    }

    virtual Backlight *GetBacklight() override {
        static PwmBacklight backlight(DISPLAY_BACKLIGHT_PIN, DISPLAY_BACKLIGHT_OUTPUT_INVERT);
        return &backlight;
    }

    virtual bool GetBatteryLevel(int &level, bool &charging, bool &discharging) override {
        if (s620_ == nullptr) {
             level = 100;  // 默认显示100%电量
            charging = false;
            discharging = false;
            return true;
        }
        
        // 确保S620已初始化
        if (!s620_->Initialize()) {
            level = 100;  // 默认显示100%电量
            charging = false;
            discharging = false;
            ESP_LOGW(TAG, "S620初始化失败");
            return true;
        }
        
        level = s620_->GetBatteryLevel();
        charging = s620_->IsCharging();
        discharging = s620_->IsDischarging();
        
        ESP_LOGD(TAG, "Battery: %d%%, Charging: %s, Discharging: %s", 
                 level, charging ? "Yes" : "No", discharging ? "Yes" : "No");
        
        return true;
    }

     ~CompactMl307ctBoard();  // 析构函数声明
};

// 析构函数实现
CompactMl307ctBoard::~CompactMl307ctBoard() {
    if (camera_preview_timer_ != nullptr) {
        esp_timer_stop(camera_preview_timer_);
        esp_timer_delete(camera_preview_timer_);
        camera_preview_timer_ = nullptr;
    }
}

DECLARE_BOARD(CompactMl307ctBoard);
