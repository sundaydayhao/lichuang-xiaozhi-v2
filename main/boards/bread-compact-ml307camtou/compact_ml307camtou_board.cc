#include "dual_network_board.h"
#include "codecs/no_audio_codec.h"
#include "display/ui_display.h"
#include "application.h"
#include "assets/lang_config.h"
#include "button.h"
#include "config.h"
#include "lamp_controller.h"
#include "led/single_led.h"
#include "esp32_camera.h"
#include "settings.h"

#if defined(TOUCH_CONTROLLER_AXS5106)
#include <esp_lcd_touch_axs5106.h>
#elif defined(TOUCH_CONTROLLER_CST816D)
#include <esp_lcd_touch_cst816s.h>
#endif

#include <driver/i2c_master.h>
#include <esp_log.h>
#include <esp_lvgl_port.h>
#include <esp_lcd_panel_ops.h>
#include <esp_partition.h>
#include <esp_lcd_panel_vendor.h>
#include <nvs.h>
#include <nvs_flash.h>
#include <wifi_configuration_ap.h>
#include <wifi_station.h>
#include <mutex>

#include "lamp_G.h"
#include "s620.h"

#include "esp_lcd_gc9a01.h"
#include <esp_spiffs.h>

static const gc9a01_lcd_init_cmd_t gc9107_lcd_init_cmds[] = {
    {0xfe, (uint8_t[]){0x00}, 0, 0},
    {0xef, (uint8_t[]){0x00}, 0, 0},
    {0x36, (uint8_t[]){0x48}, 1, 0},
    {0x3a, (uint8_t[]){0x05}, 1, 0},
    {0x86, (uint8_t[]){0x98}, 1, 0},
    {0x89, (uint8_t[]){0x13}, 1, 0},
    {0x8b, (uint8_t[]){0x80}, 1, 0},
    {0x8d, (uint8_t[]){0x33}, 1, 0},
    {0x8e, (uint8_t[]){0x0f}, 1, 0},
    {0xe8, (uint8_t[]){0x12, 0x00}, 2, 0},
    {0xec, (uint8_t[]){0x33, 0x06, 0x22}, 3, 0},
    {0xff, (uint8_t[]){0x62}, 1, 0},
    {0x99, (uint8_t[]){0x3e}, 1, 0},
    {0x9d, (uint8_t[]){0x4b}, 1, 0},
    {0x98, (uint8_t[]){0x3e}, 1, 0},
    {0x9c, (uint8_t[]){0x4b}, 1, 0},
    {0xc3, (uint8_t[]){0x20}, 1, 0},
    {0xc4, (uint8_t[]){0x30}, 1, 0},
    {0xc9, (uint8_t[]){0x2A}, 1, 0},
    {0x2a, (uint8_t[]){0x00, 0x22, 0x00, 0xcd}, 4, 0},
    {0x2b, (uint8_t[]){0x00, 0x00, 0x01, 0x3f}, 4, 0},
    {0x2c, (uint8_t[]){0x00}, 0, 0},
    {0xF0, (uint8_t[]){0x04, 0x07, 0x07, 0x07, 0x05, 0x26}, 6, 0},
    {0xF1, (uint8_t[]){0x3B, 0x71, 0x74, 0x26, 0x28, 0x69}, 6, 0},
    {0xF2, (uint8_t[]){0x04, 0x07, 0x07, 0x07, 0x05, 0x26}, 6, 0},
    {0xF3, (uint8_t[]){0x3B, 0x71, 0x74, 0x26, 0x28, 0x6C}, 6, 0},
    {0x35, (uint8_t[]){0x00}, 1, 0},
    {0x44, (uint8_t[]){0x00, 0x0a}, 2, 0},
    {0x11, (uint8_t[]){0x00}, 0, 120},
    {0x29, (uint8_t[]){0x00}, 0, 0},
    {0x2c, (uint8_t[]){0x00}, 0, 0},
};

#ifdef TAG
#undef TAG
#endif
#define TAG "CompactMl307ctBoard"

class CompactMl307ctBoard : public DualNetworkBoard {
private:
    Button boot_button_;
    UiLcdDisplay* display_ = nullptr;
    i2c_master_bus_handle_t i2c_bus_ = nullptr;
    S620* s620_ = nullptr;
    Esp32Camera* camera_ = nullptr;
    std::mutex camera_mutex_;
    std::string pending_camera_explain_url_;
    std::string pending_camera_explain_token_;

    void SetML307Enable(bool enable) {
        if (ML307_EN_PIN != GPIO_NUM_NC) {
            gpio_set_level(ML307_EN_PIN, enable ? 1 : 0);
            ESP_LOGI(TAG, "ML307 EN pin set to %s", enable ? "HIGH" : "LOW");
        }
    }

    void InitializeI2c() {
        i2c_master_bus_config_t i2c_bus_cfg = {
            .i2c_port = I2C_NUM_0,
            .sda_io_num = (gpio_num_t)CAMERA_PIN_SIOD,
            .scl_io_num = (gpio_num_t)CAMERA_PIN_SIOC,
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

        esp_lcd_panel_io_spi_config_t io_config = {};
        io_config.cs_gpio_num = DISPLAY_CS_PIN;
        io_config.dc_gpio_num = DISPLAY_DC_PIN;
        io_config.spi_mode = DISPLAY_SPI_MODE;
        io_config.pclk_hz = 40 * 1000 * 1000;
        io_config.trans_queue_depth = 10;
        io_config.lcd_cmd_bits = 8;
        io_config.lcd_param_bits = 8;
        ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(SPI3_HOST, &io_config, &panel_io));

        esp_lcd_panel_dev_config_t panel_config = {};
        panel_config.reset_gpio_num = DISPLAY_RST_PIN;
        panel_config.rgb_ele_order = DISPLAY_RGB_ORDER;
        panel_config.bits_per_pixel = 16;

#if defined(LCD_TYPE_ILI9341_SERIAL)
        ESP_ERROR_CHECK(esp_lcd_new_panel_ili9341(panel_io, &panel_config, &panel));
#elif defined(LCD_TYPE_GC9A01_SERIAL)
        gc9a01_vendor_config_t gc9107_vendor_config = {
            .init_cmds = gc9107_lcd_init_cmds,
            .init_cmds_size = sizeof(gc9107_lcd_init_cmds) / sizeof(gc9a01_lcd_init_cmd_t),
        };
        panel_config.vendor_config = &gc9107_vendor_config;
        ESP_ERROR_CHECK(esp_lcd_new_panel_gc9a01(panel_io, &panel_config, &panel));
#else
        ESP_ERROR_CHECK(bsp_lcd_new_panel_st7789(panel_io, &panel_config, &panel));
#endif

        esp_lcd_panel_reset(panel);
        esp_lcd_panel_init(panel);
        esp_lcd_panel_invert_color(panel, DISPLAY_INVERT_COLOR);
        esp_lcd_panel_swap_xy(panel, DISPLAY_SWAP_XY);
        esp_lcd_panel_mirror(panel, DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y);

        display_ = new UiLcdDisplay(panel_io, panel, DISPLAY_WIDTH, DISPLAY_HEIGHT,
                                    DISPLAY_OFFSET_X, DISPLAY_OFFSET_Y,
                                    DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y, DISPLAY_SWAP_XY);
    }

    void InitializeTouch() {
        esp_lcd_touch_handle_t tp = nullptr;

#if defined(TOUCH_CONTROLLER_AXS5106)
        i2c_master_dev_handle_t touch_dev_handle = nullptr;
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

#if defined(TOUCH_CONTROLLER_AXS5106)
        ESP_ERROR_CHECK(esp_lcd_touch_new_i2c_axs5106(touch_dev_handle, &tp_cfg, &tp));
#elif defined(TOUCH_CONTROLLER_CST816D)
        ESP_ERROR_CHECK(esp_lcd_touch_new_i2c_cst816s(tp_io_handle, &tp_cfg, &tp));
#endif

        const lvgl_port_touch_cfg_t touch_cfg = {
            .disp = lv_display_get_default(),
            .handle = tp,
        };
        lvgl_port_add_touch(&touch_cfg);
        ESP_LOGI(TAG, "Touch screen initialized");
    }

    void InitializeCamera() {
        if (camera_ != nullptr) {
            return;
        }
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
        if (camera_ != nullptr) {
            camera_->SetHMirror(CAMERA_MIRROR_X);
            camera_->SetVFlip(CAMERA_FLIP_Y);
            if (!pending_camera_explain_url_.empty()) {
                camera_->SetExplainUrl(pending_camera_explain_url_, pending_camera_explain_token_);
            }
        }
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
        });

        boot_button_.OnDoubleClick([this]() {
            auto& app = Application::GetInstance();
            if (app.GetDeviceState() == kDeviceStateStarting || app.GetDeviceState() == kDeviceStateWifiConfiguring) {
                if (GetNetworkType() == NetworkType::WIFI) {
                    SetML307Enable(true);
                    ESP_LOGI(TAG, "Switching to 4G, enabling ML307");
                } else {
                    ESP_LOGI(TAG, "Switching to WiFi, ML307 will be disabled when WiFi connects");
                }
                SwitchNetworkType();
            }
        });
    }

    void MountStorage() {
        const char* partition_label = "storage";
        if (esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_SPIFFS,
                                     partition_label) == nullptr) {
            partition_label = "assets";
        }

        esp_vfs_spiffs_conf_t conf = {
            .base_path = "/storage",
            .partition_label = partition_label,
            .max_files = 10,
            .format_if_mount_failed = true,
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

        size_t total = 0;
        size_t used = 0;
        ret = esp_spiffs_info(partition_label, &total, &used);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to get SPIFFS partition information (%s)", esp_err_to_name(ret));
        } else {
            ESP_LOGI(TAG, "Storage partition mounted from '%s': total: %d KB, used: %d KB",
                     partition_label, total / 1024, used / 1024);
        }
    }

    void InitializeTools() {
        static LampController lamp(LAMP_GPIO);
        static Green_Lamp lamp_G(BOT_PWM);
    }

    bool TryWifiConnection() {
        auto& wifi_station = WifiStation::GetInstance();
        wifi_station.OnScanBegin([this]() {
            GetDisplay()->ShowNotification(Lang::Strings::SCANNING_WIFI, 30000);
        });
        wifi_station.OnConnect([this](const std::string& ssid) {
            std::string notification = Lang::Strings::CONNECT_TO;
            notification += ssid;
            notification += "...";
            GetDisplay()->ShowNotification(notification.c_str(), 30000);
        });
        wifi_station.OnConnected([this](const std::string& ssid) {
            std::string notification = Lang::Strings::CONNECTED_TO;
            notification += ssid;
            GetDisplay()->ShowNotification(notification.c_str(), 30000);
        });
        wifi_station.Start();
        return wifi_station.WaitForConnected(30 * 1000);
    }

    void TryWifiConfigAndSwitchTo4G() {
        auto display = GetDisplay();
        display->ShowNotification("WiFi连接失败，进入配网模式");
        vTaskDelay(pdMS_TO_TICKS(2000));

        auto& application = Application::GetInstance();
        application.SetDeviceState(kDeviceStateWifiConfiguring);

        auto& wifi_ap = WifiConfigurationAp::GetInstance();
        Settings ui_settings("ui", false);
        std::string language = ui_settings.GetString("language");
        if (language != "zh-CN" && language != "zh-TW" && language != "en-US") {
            language = Lang::CODE;
        }
        wifi_ap.SetLanguage(language.c_str());
        wifi_ap.SetSsidPrefix("Xiaozhi");
        wifi_ap.Start();

        vTaskDelay(pdMS_TO_TICKS(1500));

        std::string hint = Lang::Strings::CONNECT_TO_HOTSPOT;
        hint += wifi_ap.GetSsid();
        hint += Lang::Strings::ACCESS_VIA_BROWSER;
        hint += wifi_ap.GetWebServerUrl();
        hint += "\n\n";

        application.Alert(Lang::Strings::WIFI_CONFIG_MODE, hint.c_str(), "gear", Lang::Sounds::OGG_WIFICONFIG);

        ESP_LOGI(TAG, "Waiting 30 seconds for WiFi configuration...");
        vTaskDelay(pdMS_TO_TICKS(30 * 1000));

        if (!WifiStation::GetInstance().IsConnected()) {
            ESP_LOGI(TAG, "WiFi configuration timeout, switching to 4G");
            wifi_ap.Stop();
            display->ShowNotification("配网超时，切换到4G网络");
            vTaskDelay(pdMS_TO_TICKS(2000));
            SwitchNetworkType();
        }
    }

public:
    CompactMl307ctBoard() : DualNetworkBoard(ML307_TX_PIN, ML307_RX_PIN, GPIO_NUM_NC, 0),
                            boot_button_(BOOT_BUTTON_GPIO) {
        if (ML307_EN_PIN != GPIO_NUM_NC) {
            gpio_config_t ml307_en_config = {
                .pin_bit_mask = 1ULL << ML307_EN_PIN,
                .mode = GPIO_MODE_OUTPUT,
                .pull_up_en = GPIO_PULLUP_DISABLE,
                .pull_down_en = GPIO_PULLDOWN_DISABLE,
                .intr_type = GPIO_INTR_DISABLE,
            };
            ESP_ERROR_CHECK(gpio_config(&ml307_en_config));
            SetML307Enable(true);
        }

        InitializeI2c();
        InitializeSpi();
        InitializeLcdDisplay();
        InitializeS620();
        InitializeTouch();
        InitializeButtons();
        InitializeTools();
        display_->SetCameraPreviewCallback([this]() {
            auto* camera = GetCamera();
            if (camera != nullptr) {
                camera->Capture();
            }
        });
        MountStorage();
        GetBacklight()->RestoreBrightness();

        ESP_LOGI(TAG, "CompactMl307ctBoard initialized, network type: %d", (int)GetNetworkType());

        nvs_handle_t nvs;
        esp_err_t err = nvs_open("network", NVS_READWRITE, &nvs);
        if (err == ESP_OK) {
            nvs_erase_key(nvs, "type");
            nvs_commit(nvs);
            nvs_close(nvs);
            ESP_LOGI(TAG, "Cleared network settings");
        }
    }

    ~CompactMl307ctBoard() override {
        delete camera_;
        delete s620_;
        delete display_;
    }

    void StartNetwork() override {
        ESP_LOGI(TAG, "StartNetwork called, current network type: %d", (int)GetNetworkType());

        if (GetNetworkType() == NetworkType::WIFI) {
            auto& current_board = GetCurrentBoard();
            auto* wifi_board = dynamic_cast<WifiBoard*>(&current_board);
            if (wifi_board != nullptr && wifi_board->ShouldEnterWifiConfigMode()) {
                ESP_LOGI(TAG, "force_ap flag detected, entering WiFi config mode");
                current_board.StartNetwork();
                return;
            }

            auto display = GetDisplay();
            display->SetStatus(Lang::Strings::CONNECTING);
            ESP_LOGI(TAG, "Starting WiFi connection attempt");

            if (!TryWifiConnection()) {
                ESP_LOGI(TAG, "WiFi connection failed, trying configuration mode");
                WifiStation::GetInstance().Stop();
                TryWifiConfigAndSwitchTo4G();
                return;
            }

            ESP_LOGI(TAG, "WiFi connection successful");
            SetML307Enable(false);
            return;
        }

        ESP_LOGI(TAG, "Network type is not WiFi, using 4G");
        SetML307Enable(true);
        DualNetworkBoard::StartNetwork();
    }

    Led* GetLed() override {
        static SingleLed led(BUILTIN_LED_GPIO);
        return &led;
    }

    AudioCodec* GetAudioCodec() override {
#ifdef AUDIO_I2S_METHOD_SIMPLEX
        static NoAudioCodecSimplex audio_codec(AUDIO_INPUT_SAMPLE_RATE, AUDIO_OUTPUT_SAMPLE_RATE,
            AUDIO_I2S_SPK_GPIO_BCLK, AUDIO_I2S_SPK_GPIO_LRCK, AUDIO_I2S_SPK_GPIO_DOUT,
            AUDIO_I2S_MIC_GPIO_SCK, AUDIO_I2S_MIC_GPIO_WS, AUDIO_I2S_MIC_GPIO_DIN);
#else
        static NoAudioCodecDuplex audio_codec(AUDIO_INPUT_SAMPLE_RATE, AUDIO_OUTPUT_SAMPLE_RATE,
            AUDIO_I2S_GPIO_BCLK, AUDIO_I2S_GPIO_WS, AUDIO_I2S_GPIO_DOUT, AUDIO_I2S_GPIO_DIN);
#endif
        return &audio_codec;
    }

    Display* GetDisplay() override {
        return display_;
    }

    Camera* GetCamera() override {
        std::lock_guard<std::mutex> lock(camera_mutex_);
        if (camera_ == nullptr) {
            InitializeCamera();
        }
        return camera_;
    }

    void ConfigureCameraExplain(const std::string& url, const std::string& token) override {
        std::lock_guard<std::mutex> lock(camera_mutex_);
        pending_camera_explain_url_ = url;
        pending_camera_explain_token_ = token;
        if (camera_ != nullptr) {
            camera_->SetExplainUrl(url, token);
        }
    }

    Backlight* GetBacklight() override {
        static PwmBacklight backlight(DISPLAY_BACKLIGHT_PIN, DISPLAY_BACKLIGHT_OUTPUT_INVERT);
        return &backlight;
    }

    bool GetBatteryLevel(int& level, bool& charging, bool& discharging) override {
        if (s620_ == nullptr || !s620_->Initialize()) {
            level = 100;
            charging = false;
            discharging = false;
            if (s620_ != nullptr) {
                ESP_LOGW(TAG, "S620初始化失败");
            }
            return true;
        }

        level = s620_->GetBatteryLevel();
        charging = s620_->IsCharging();
        discharging = s620_->IsDischarging();
        ESP_LOGD(TAG, "Battery: %d%%, Charging: %s, Discharging: %s",
                 level, charging ? "Yes" : "No", discharging ? "Yes" : "No");
        return true;
    }
};

DECLARE_BOARD(CompactMl307ctBoard);
