#include "wifi_board.h"
#include "codecs/no_audio_codec.h"
#include "display/lcd_display.h"
#include "system_reset.h"
#include "application.h"
#include "button.h"
#include "config.h"
#include "mcp_server.h"
#include "lamp_controller.h"
#include "led/single_led.h"
#include "esp32_camera.h"

#include <wifi_station.h>
#include <esp_log.h>
#include <driver/i2c_master.h>
#include <esp_lcd_panel_vendor.h>
#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_ops.h>
#include <driver/spi_common.h>
#include <esp_lcd_touch_axs5106.h>  
#include <esp_lvgl_port.h>          
#include <lvgl.h>    
#include <esp_timer.h>     

#include <esp_lcd_touch_cst816s.h>
#include <esp_lvgl_port.h>
#include <lvgl.h>

#if defined(LCD_TYPE_ILI9341_SERIAL)
#include "esp_lcd_ili9341.h"
#endif

#if defined(LCD_TYPE_GC9A01_SERIAL)
#include "esp_lcd_gc9a01.h"
static const gc9a01_lcd_init_cmd_t gc9107_lcd_init_cmds[] = {
    //  {cmd, { data }, data_size, delay_ms}
    {0xfe, (uint8_t[]){0x00}, 0, 0},
    {0xef, (uint8_t[]){0x00}, 0, 0},
    {0xb0, (uint8_t[]){0xc0}, 1, 0},
    {0xb1, (uint8_t[]){0x80}, 1, 0},
    {0xb2, (uint8_t[]){0x27}, 1, 0},
    {0xb3, (uint8_t[]){0x13}, 1, 0},
    {0xb6, (uint8_t[]){0x19}, 1, 0},
    {0xb7, (uint8_t[]){0x05}, 1, 0},
    {0xac, (uint8_t[]){0xc8}, 1, 0},
    {0xab, (uint8_t[]){0x0f}, 1, 0},
    {0x3a, (uint8_t[]){0x05}, 1, 0},
    {0xb4, (uint8_t[]){0x04}, 1, 0},
    {0xa8, (uint8_t[]){0x08}, 1, 0},
    {0xb8, (uint8_t[]){0x08}, 1, 0},
    {0xea, (uint8_t[]){0x02}, 1, 0},
    {0xe8, (uint8_t[]){0x2A}, 1, 0},
    {0xe9, (uint8_t[]){0x47}, 1, 0},
    {0xe7, (uint8_t[]){0x5f}, 1, 0},
    {0xc6, (uint8_t[]){0x21}, 1, 0},
    {0xc7, (uint8_t[]){0x15}, 1, 0},
    {0xf0,
    (uint8_t[]){0x1D, 0x38, 0x09, 0x4D, 0x92, 0x2F, 0x35, 0x52, 0x1E, 0x0C,
                0x04, 0x12, 0x14, 0x1f},
    14, 0},
    {0xf1,
    (uint8_t[]){0x16, 0x40, 0x1C, 0x54, 0xA9, 0x2D, 0x2E, 0x56, 0x10, 0x0D,
                0x0C, 0x1A, 0x14, 0x1E},
    14, 0},
    {0xf4, (uint8_t[]){0x00, 0x00, 0xFF}, 3, 0},
    {0xba, (uint8_t[]){0xFF, 0xFF}, 2, 0},
};
#endif
 
#define TAG "CompactWifiBoardS3Cam"

class CompactWifiBoardS3Cam : public WifiBoard {
private:
    i2c_master_bus_handle_t i2c_bus_;
    lv_obj_t* touch_overlay_;  //LVGL 对象用于捕获触摸
    Button boot_button_;
    LcdDisplay* display_;
     Esp32Camera* camera_;

    //触摸状态（静态变量，用于回调访问）
    static bool was_touched_;
    static int64_t touch_start_time_;

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
        ESP_ERROR_CHECK(esp_lcd_new_panel_st7789(panel_io, &panel_config, &panel));
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

        // 第一步：在 I2C 总线上添加触摸设备（地址 0x63）
        i2c_master_dev_handle_t touch_dev_handle;
        i2c_device_config_t touch_dev_cfg = {
            .dev_addr_length = I2C_ADDR_BIT_LEN_7,
            .device_address = 0x63,  // AXS5106 的 I2C 地址
            .scl_speed_hz = 400000,
        };
        ESP_ERROR_CHECK(i2c_master_bus_add_device(i2c_bus_, &touch_dev_cfg, &touch_dev_handle));

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
        ESP_ERROR_CHECK(esp_lcd_touch_new_i2c_axs5106(touch_dev_handle, &tp_cfg, &tp));
        
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

    // ✅ LVGL 触摸事件回调（静态方法）
    static void TouchEventCallback(lv_event_t* e) {
        lv_event_code_t code = lv_event_get_code(e);
        const int64_t TOUCH_THRESHOLD_MS = 500;
        
        if (code == LV_EVENT_PRESSED) {
            // 触摸按下
            was_touched_ = true;
            touch_start_time_ = esp_timer_get_time() / 1000;
            ESP_LOGI(TAG, "Touch pressed");
        } 
        else if (code == LV_EVENT_RELEASED) {
            // 触摸释放
            if (was_touched_) {
                was_touched_ = false;
                int64_t touch_duration = (esp_timer_get_time() / 1000) - touch_start_time_;
                
                // 只有短按才触发
                if (touch_duration < TOUCH_THRESHOLD_MS) {
                    auto& app = Application::GetInstance();
                    auto& board = (CompactWifiBoardS3Cam&)Board::GetInstance();
                    
                    if (app.GetDeviceState() == kDeviceStateStarting && 
                        !WifiStation::GetInstance().IsConnected()) {
                        board.ResetWifiConfiguration();
                    } else {
                        app.ToggleChatState();
                        ESP_LOGI(TAG, "Touch released - toggle chat (duration: %lldms)", touch_duration);
                    }
                }
            }
        }
    }

    // 创建全屏触摸拦截层
    void SetupTouchListener() {
        // 在 Display 的 Lock 保护下操作 LVGL
        DisplayLockGuard lock(display_);
        
        // 创建一个全屏透明对象覆盖在最顶层
        lv_obj_t* touch_overlay = lv_obj_create(lv_screen_active());
        lv_obj_set_size(touch_overlay, LV_PCT(100), LV_PCT(100));
        lv_obj_set_pos(touch_overlay, 0, 0);
        
        // 设置为透明，不显示任何内容
        lv_obj_set_style_bg_opa(touch_overlay, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(touch_overlay, 0, 0);
        
        // ✅ 关键：允许点击穿透，但捕获事件
        lv_obj_add_flag(touch_overlay, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_remove_flag(touch_overlay, LV_OBJ_FLAG_SCROLLABLE);
        
        // 移到最顶层
        lv_obj_move_foreground(touch_overlay);
        
        // 添加触摸事件监听
        lv_obj_add_event_cb(touch_overlay, TouchEventCallback, LV_EVENT_PRESSED, nullptr);
        lv_obj_add_event_cb(touch_overlay, TouchEventCallback, LV_EVENT_RELEASED, nullptr);
        
        ESP_LOGI(TAG, "Touch overlay created and event listener registered");
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
            if (app.GetDeviceState() == kDeviceStateStarting && !WifiStation::GetInstance().IsConnected()) {
                ResetWifiConfiguration();
            }
            app.ToggleChatState();
        });
    }

public:
    CompactWifiBoardS3Cam() :
        boot_button_(BOOT_BUTTON_GPIO) {
        InitializeI2c();
        InitializeSpi();
        InitializeLcdDisplay();
        //InitializeTouch();  // 初始化触摸
        //SetupTouchListener();  //创建触摸覆盖层
        InitializeButtons();
        InitializeCamera();
        if (DISPLAY_BACKLIGHT_PIN != GPIO_NUM_NC) {
            GetBacklight()->RestoreBrightness();
        }
        
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

    virtual Backlight* GetBacklight() override {
        if (DISPLAY_BACKLIGHT_PIN != GPIO_NUM_NC) {
            static PwmBacklight backlight(DISPLAY_BACKLIGHT_PIN, DISPLAY_BACKLIGHT_OUTPUT_INVERT);
            return &backlight;
        }
        return nullptr;
    }

    virtual Camera* GetCamera() override {
        return camera_;
    }
};

// 静态变量定义（放在类定义外）
bool CompactWifiBoardS3Cam::was_touched_ = false;
int64_t CompactWifiBoardS3Cam::touch_start_time_ = 0;

DECLARE_BOARD(CompactWifiBoardS3Cam);
