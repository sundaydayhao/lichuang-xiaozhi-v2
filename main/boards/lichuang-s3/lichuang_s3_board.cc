#include "wifi_board.h"
#include "codecs/lichuang_audio_codec.h"
#include "display/ui_display.h"
#include "application.h"
#include "assets/lang_config.h"
#include "button.h"
#include "config.h"
#include "led/single_led.h"
#include "esp32_camera.h"

#include <esp_lcd_touch_ft5x06.h>

#include <driver/i2c_master.h>
#include <driver/ledc.h>
#include <esp_err.h>
#include <esp_heap_caps.h>
#include <esp_log.h>
#include <esp_lvgl_port.h>
#include <esp_lcd_panel_ops.h>
#include <esp_partition.h>
#include <esp_lcd_panel_vendor.h>
#include <wifi_station.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <mutex>

#include <esp_spiffs.h>

#ifdef TAG
#undef TAG
#endif
#define TAG "LichuangS3Board"

class LichuangS3Board : public WifiBoard {
private:
    static constexpr uint8_t kPca9557Address = 0x19;
    static constexpr uint8_t kPca9557OutputPort = 0x01;
    static constexpr uint8_t kPca9557ConfigurationPort = 0x03;
    static constexpr uint8_t kPca9557LcdCsBit = 1 << 0;
    static constexpr uint8_t kPca9557PaEnBit = 1 << 1;
    static constexpr uint8_t kPca9557DvpPwdnBit = 1 << 2;
    static constexpr uint8_t kPca9557DefaultOutput = kPca9557LcdCsBit | kPca9557DvpPwdnBit;
    static constexpr uint8_t kPca9557OutputConfig = 0xF8;

    Button boot_button_;
    UiLcdDisplay* display_ = nullptr;
    i2c_master_bus_handle_t i2c_bus_ = nullptr;
    i2c_master_dev_handle_t pca9557_ = nullptr;
    Esp32Camera* camera_ = nullptr;
    bool camera_probe_attempted_ = false;
    uint8_t pca9557_output_state_ = kPca9557DefaultOutput;
    std::mutex camera_mutex_;
    std::string pending_camera_explain_url_;
    std::string pending_camera_explain_token_;

    void InitializeI2c() {
        i2c_master_bus_config_t i2c_bus_cfg = {
            .i2c_port = BSP_I2C_NUM,
            .sda_io_num = BSP_I2C_SDA,
            .scl_io_num = BSP_I2C_SCL,
            .clk_source = I2C_CLK_SRC_DEFAULT,
            .glitch_ignore_cnt = 7,
            .intr_priority = 0,
            .trans_queue_depth = 0,
            .flags = {
                .enable_internal_pullup = 1,
            },
        };
        ESP_ERROR_CHECK(i2c_new_master_bus(&i2c_bus_cfg, &i2c_bus_));
        ESP_LOGI(TAG, "I2C bus initialized on SDA=%d, SCL=%d", BSP_I2C_SDA, BSP_I2C_SCL);
    }

    esp_err_t WritePca9557Reg(uint8_t reg, uint8_t value) {
        if (pca9557_ == nullptr) {
            return ESP_ERR_INVALID_STATE;
        }

        uint8_t buffer[2] = {reg, value};
        return i2c_master_transmit(pca9557_, buffer, sizeof(buffer), 100);
    }

    void SetPca9557Output(uint8_t bit, bool level) {
        if (level) {
            pca9557_output_state_ |= bit;
        } else {
            pca9557_output_state_ &= ~bit;
        }

        ESP_ERROR_CHECK(WritePca9557Reg(kPca9557OutputPort, pca9557_output_state_));
    }

    void InitializePca9557() {
        if (i2c_bus_ == nullptr) {
            ESP_LOGE(TAG, "I2C bus not initialized, cannot initialize PCA9557");
            return;
        }

        i2c_device_config_t pca9557_config = {
            .dev_addr_length = I2C_ADDR_BIT_LEN_7,
            .device_address = kPca9557Address,
            .scl_speed_hz = BSP_I2C_FREQ_HZ,
            .scl_wait_us = 0,
            .flags = {
                .disable_ack_check = 0,
            },
        };
        ESP_ERROR_CHECK(i2c_master_bus_add_device(i2c_bus_, &pca9557_config, &pca9557_));

        pca9557_output_state_ = kPca9557DefaultOutput;
        ESP_ERROR_CHECK(WritePca9557Reg(kPca9557OutputPort, pca9557_output_state_));
        ESP_ERROR_CHECK(WritePca9557Reg(kPca9557ConfigurationPort, kPca9557OutputConfig));
        ESP_LOGI(TAG, "PCA9557 initialized for LCD CS, PA EN and DVP PWDN");
    }

    void SelectLcdPanel() {
        SetPca9557Output(kPca9557LcdCsBit, false);
    }

    void DrawLcdBootPattern(esp_lcd_panel_handle_t panel) {
        auto* line = static_cast<uint16_t*>(heap_caps_malloc(
            DISPLAY_WIDTH * sizeof(uint16_t), MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL));
        if (line == nullptr) {
            ESP_LOGW(TAG, "Failed to allocate LCD boot pattern buffer");
            ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel, true));
            return;
        }

        for (int y = 0; y < DISPLAY_HEIGHT; ++y) {
            const uint16_t color = (y < DISPLAY_HEIGHT / 3) ? 0xF800 :
                                   (y < DISPLAY_HEIGHT * 2 / 3) ? 0x07E0 : 0x001F;
            for (int x = 0; x < DISPLAY_WIDTH; ++x) {
                line[x] = color;
            }
            ESP_ERROR_CHECK(esp_lcd_panel_draw_bitmap(panel, 0, y, DISPLAY_WIDTH, y + 1, line));
        }

        ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel, true));
        heap_caps_free(line);
        ESP_LOGI(TAG, "LCD boot test pattern drawn");
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
        io_config.pclk_hz = DISPLAY_PIXEL_CLOCK_HZ;
        io_config.trans_queue_depth = 10;
        io_config.lcd_cmd_bits = 8;
        io_config.lcd_param_bits = 8;
        ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(SPI3_HOST, &io_config, &panel_io));

        esp_lcd_panel_dev_config_t panel_config = {};
        panel_config.reset_gpio_num = DISPLAY_RST_PIN;
        panel_config.rgb_ele_order = DISPLAY_RGB_ORDER;
        panel_config.bits_per_pixel = 16;

        ESP_ERROR_CHECK(esp_lcd_new_panel_st7789(panel_io, &panel_config, &panel));

        ESP_ERROR_CHECK(esp_lcd_panel_reset(panel));
        SelectLcdPanel();
        ESP_ERROR_CHECK(esp_lcd_panel_init(panel));
        ESP_ERROR_CHECK(esp_lcd_panel_invert_color(panel, DISPLAY_INVERT_COLOR));
        ESP_ERROR_CHECK(esp_lcd_panel_swap_xy(panel, DISPLAY_SWAP_XY));
        ESP_ERROR_CHECK(esp_lcd_panel_mirror(panel, DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y));
        DrawLcdBootPattern(panel);
        GetBacklight()->SetBrightness(75);
        vTaskDelay(pdMS_TO_TICKS(300));

        display_ = new UiLcdDisplay(panel_io, panel, DISPLAY_WIDTH, DISPLAY_HEIGHT,
                                    DISPLAY_OFFSET_X, DISPLAY_OFFSET_Y,
                                    DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y, DISPLAY_SWAP_XY);
    }

    void InitializeTouch() {
        esp_lcd_touch_handle_t tp = nullptr;
        esp_err_t ret = ESP_OK;

        esp_lcd_panel_io_handle_t tp_io_handle = nullptr;
        esp_lcd_panel_io_i2c_config_t tp_io_config = {
            .dev_addr = TOUCH_I2C_ADDRESS,
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

        ret = esp_lcd_touch_new_i2c_ft5x06(tp_io_handle, &tp_cfg, &tp);

        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Touch screen initialization failed: %s (0x%x)", esp_err_to_name(ret), ret);
            return;
        }

        const lvgl_port_touch_cfg_t touch_cfg = {
            .disp = lv_display_get_default(),
            .handle = tp,
        };
        lvgl_port_add_touch(&touch_cfg);
        ESP_LOGI(TAG, "Touch screen initialized");
    }

    void InitializeCamera() {
        if (camera_ != nullptr || camera_probe_attempted_) {
            return;
        }
        camera_probe_attempted_ = true;

        SetPca9557Output(kPca9557DvpPwdnBit, false);
        vTaskDelay(pdMS_TO_TICKS(10));

        camera_config_t config = {};
        config.ledc_channel = LEDC_CHANNEL_1;
        config.ledc_timer = LEDC_TIMER_1;
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
        config.sccb_i2c_port = BSP_I2C_NUM;
        config.pin_pwdn = CAMERA_PIN_PWDN;
        config.pin_reset = CAMERA_PIN_RESET;
        config.xclk_freq_hz = XCLK_FREQ_HZ;
        config.pixel_format = PIXFORMAT_RGB565;
        config.frame_size = FRAMESIZE_QVGA;
        config.jpeg_quality = 12;
        config.fb_count = 2;
        config.fb_location = CAMERA_FB_IN_PSRAM;
        config.grab_mode = CAMERA_GRAB_WHEN_EMPTY;
        auto* camera = new Esp32Camera(config);
        if (camera == nullptr) {
            ESP_LOGW(TAG, "Failed to allocate camera instance");
            return;
        }

        if (!camera->IsReady()) {
            ESP_LOGW(TAG, "Camera not detected, continuing without camera support");
            delete camera;
            return;
        }

        camera_ = camera;
        camera_->SetHMirror(CAMERA_MIRROR_X);
        camera_->SetVFlip(CAMERA_FLIP_Y);
        if (!pending_camera_explain_url_.empty()) {
            camera_->SetExplainUrl(pending_camera_explain_url_, pending_camera_explain_token_);
        }
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

public:
    LichuangS3Board() : WifiBoard(),
                        boot_button_(BOOT_BUTTON_GPIO) {
        InitializeI2c();
        InitializePca9557();
        InitializeSpi();
        InitializeLcdDisplay();
        InitializeTouch();
        InitializeButtons();
        display_->SetCameraPreviewCallback([this]() {
            auto* camera = GetCamera();
            if (camera != nullptr) {
                camera->Capture();
            }
        });
        MountStorage();
        GetBacklight()->RestoreBrightness();

        ESP_LOGI(TAG, "LichuangS3Board initialized");
    }

    ~LichuangS3Board() override {
        delete camera_;
        delete display_;
    }

    void StartNetwork() override {
        GetDisplay()->SetStatus(Lang::Strings::CONNECTING);
        WifiBoard::StartNetwork();
    }

    Led* GetLed() override {
        static SingleLed led(BUILTIN_LED_GPIO);
        return &led;
    }

    AudioCodec* GetAudioCodec() override {
        static LichuangAudioCodec audio_codec(AUDIO_INPUT_SAMPLE_RATE, AUDIO_OUTPUT_SAMPLE_RATE,
            i2c_bus_, [this](bool enable) {
                SetPca9557Output(kPca9557PaEnBit, enable);
            });
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
        static PwmBacklight backlight(DISPLAY_BACKLIGHT_PIN, DISPLAY_BACKLIGHT_OUTPUT_INVERT,
                                      DISPLAY_BACKLIGHT_PWM_FREQ_HZ);
        return &backlight;
    }
};

DECLARE_BOARD(LichuangS3Board);
