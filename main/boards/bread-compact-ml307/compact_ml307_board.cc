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
#include "led/single_led.h"
#include "esp32_camera.h"
// 添加以下三行
#include <esp_lcd_touch_axs5106.h> 
#include <esp_lvgl_port.h>
#include <lvgl.h>

#include <esp_log.h>
#include <driver/i2c_master.h>
#include <esp_lcd_panel_ops.h>
#include <esp_lcd_panel_vendor.h>
#include <wifi_station.h>
#include <wifi_configuration_ap.h>

#include <nvs.h>
#include <nvs_flash.h>

#include "esp_lcd_gc9a01.h"
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

#define TAG "CompactMl307Board"

class CompactMl307Board : public DualNetworkBoard {
private:
    Button boot_button_;
    LcdDisplay* display_;
    i2c_master_bus_handle_t i2c_bus_;
    lv_obj_t* touch_overlay_;  //LVGL 对象用于捕获触摸
    Esp32Camera* camera_;

     static bool was_touched_;
    static int64_t touch_start_time_;

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
                    auto& board = (CompactMl307Board&)Board::GetInstance();
                    
                    if (app.GetDeviceState() == kDeviceStateStarting ) {
                //        board.ResetWifiConfiguration();
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
            if (GetNetworkType() == NetworkType::WIFI) {
                if (app.GetDeviceState() == kDeviceStateStarting && !WifiStation::GetInstance().IsConnected()) {
                    // cast to WifiBoard
                    auto& wifi_board = static_cast<WifiBoard&>(GetCurrentBoard());
                    wifi_board.ResetWifiConfiguration();
                }
            }
            app.ToggleChatState();
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
    void InitializeTools() {
        static LampController lamp(LAMP_GPIO);
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
    CompactMl307Board() : DualNetworkBoard(ML307_TX_PIN, ML307_RX_PIN, GPIO_NUM_NC,0),
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
        InitializeTouch();  // 初始化触摸
        SetupTouchListener();  //创建触摸覆盖层
        InitializeButtons();
        InitializeTools();
        InitializeCamera();

        if (DISPLAY_BACKLIGHT_PIN != GPIO_NUM_NC) {
            gpio_config_t bk_gpio_config = {
                .pin_bit_mask = 1ULL << DISPLAY_BACKLIGHT_PIN,
                .mode = GPIO_MODE_OUTPUT,
                .pull_up_en = GPIO_PULLUP_DISABLE,
                .pull_down_en = GPIO_PULLDOWN_DISABLE,
                .intr_type = GPIO_INTR_DISABLE,
            };
            ESP_ERROR_CHECK(gpio_config(&bk_gpio_config));
            ESP_ERROR_CHECK(gpio_set_level(DISPLAY_BACKLIGHT_PIN, 1));
        }

        ESP_LOGI(TAG, "CompactMl307Board initialized, network type: %d", (int)GetNetworkType());
        
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

        if (GetNetworkType() == NetworkType::WIFI) {
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
};

// 静态变量定义（放在类定义外）
bool CompactMl307Board::was_touched_ = false;
int64_t CompactMl307Board::touch_start_time_ = 0;

DECLARE_BOARD(CompactMl307Board);
