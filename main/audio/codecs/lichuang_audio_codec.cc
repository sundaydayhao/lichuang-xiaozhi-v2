#include "lichuang_audio_codec.h"
#include "boards/lichuang-s3/config.h"

#include <driver/i2s_std.h>
#include <esp_err.h>
#include <esp_log.h>

#include <cassert>
#include <utility>

#define TAG "LichuangAudioCodec"

namespace {
constexpr int kRawInputChannels = ADC_I2S_CHANNEL;
constexpr int kAfeInputChannels = 3;
constexpr int kCodecBitsPerSample = CODEC_DEFAULT_BIT_WIDTH;
constexpr int kCodecChannels = CODEC_DEFAULT_CHANNEL;
} // namespace

LichuangAudioCodec::LichuangAudioCodec(int input_sample_rate, int output_sample_rate,
                                       i2c_master_bus_handle_t i2c_bus,
                                       std::function<void(bool)> set_pa_power)
    : i2c_bus_(i2c_bus),
      set_pa_power_(std::move(set_pa_power)) {
    duplex_ = true;
    input_reference_ = true;
    input_sample_rate_ = input_sample_rate;
    output_sample_rate_ = output_sample_rate;
    input_channels_ = kAfeInputChannels;
    output_channels_ = 1;

    assert(i2c_bus_ != nullptr);
    InitializeI2s();
    InitializeCodecDevices();
    OpenCodecDevices();
    ESP_LOGI(TAG, "Audio codec initialized: ES8311 output + ES7210 input");
}

LichuangAudioCodec::~LichuangAudioCodec() {
    if (set_pa_power_) {
        set_pa_power_(false);
    }
    if (output_dev_ != nullptr) {
        esp_codec_dev_delete(output_dev_);
    }
    if (input_dev_ != nullptr) {
        esp_codec_dev_delete(input_dev_);
    }
    if (es8311_codec_if_ != nullptr) {
        audio_codec_delete_codec_if(es8311_codec_if_);
    }
    if (es7210_codec_if_ != nullptr) {
        audio_codec_delete_codec_if(es7210_codec_if_);
    }
    if (es8311_ctrl_if_ != nullptr) {
        audio_codec_delete_ctrl_if(es8311_ctrl_if_);
    }
    if (es7210_ctrl_if_ != nullptr) {
        audio_codec_delete_ctrl_if(es7210_ctrl_if_);
    }
    if (gpio_if_ != nullptr) {
        audio_codec_delete_gpio_if(gpio_if_);
    }
    if (i2s_data_if_ != nullptr) {
        audio_codec_delete_data_if(i2s_data_if_);
    }
    if (i2s_tx_chan_ != nullptr) {
        i2s_del_channel(i2s_tx_chan_);
    }
    if (i2s_rx_chan_ != nullptr) {
        i2s_del_channel(i2s_rx_chan_);
    }
}

void LichuangAudioCodec::InitializeI2s() {
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(BSP_I2S_NUM, I2S_ROLE_MASTER);
    chan_cfg.dma_desc_num = AUDIO_CODEC_DMA_DESC_NUM;
    chan_cfg.dma_frame_num = AUDIO_CODEC_DMA_FRAME_NUM;
    chan_cfg.auto_clear_after_cb = true;
    chan_cfg.auto_clear_before_cb = false;
    ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, &i2s_tx_chan_, &i2s_rx_chan_));

    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG((uint32_t)input_sample_rate_),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = GPIO_I2S_MCLK,
            .bclk = GPIO_I2S_SCLK,
            .ws = GPIO_I2S_LRCK,
            .dout = GPIO_I2S_DOUT,
            .din = GPIO_I2S_SDIN,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        },
    };
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(i2s_tx_chan_, &std_cfg));
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(i2s_rx_chan_, &std_cfg));
    ESP_ERROR_CHECK(i2s_channel_enable(i2s_tx_chan_));
    ESP_ERROR_CHECK(i2s_channel_enable(i2s_rx_chan_));

    audio_codec_i2s_cfg_t i2s_cfg = {
        .port = BSP_I2S_NUM,
        .rx_handle = i2s_rx_chan_,
        .tx_handle = i2s_tx_chan_,
    };
    i2s_data_if_ = audio_codec_new_i2s_data(&i2s_cfg);
    assert(i2s_data_if_ != nullptr);
}

void LichuangAudioCodec::InitializeCodecDevices() {
    gpio_if_ = audio_codec_new_gpio();
    assert(gpio_if_ != nullptr);

    audio_codec_i2c_cfg_t es8311_i2c_cfg = {
        .port = BSP_I2C_NUM,
        .addr = ES8311_CODEC_DEFAULT_ADDR,
        .bus_handle = i2c_bus_,
    };
    es8311_ctrl_if_ = audio_codec_new_i2c_ctrl(&es8311_i2c_cfg);
    assert(es8311_ctrl_if_ != nullptr);

    esp_codec_dev_hw_gain_t gain = {
        .pa_voltage = 5.0,
        .codec_dac_voltage = 3.3,
    };

    es8311_codec_cfg_t es8311_cfg = {
        .ctrl_if = es8311_ctrl_if_,
        .gpio_if = gpio_if_,
        .codec_mode = ESP_CODEC_DEV_WORK_MODE_DAC,
        .pa_pin = GPIO_PWR_CTRL,
        .pa_reverted = false,
        .master_mode = false,
        .use_mclk = true,
        .digital_mic = false,
        .invert_mclk = false,
        .invert_sclk = false,
        .hw_gain = gain,
        .no_dac_ref = false,
        .mclk_div = 0,
    };
    es8311_codec_if_ = es8311_codec_new(&es8311_cfg);
    assert(es8311_codec_if_ != nullptr);

    esp_codec_dev_cfg_t output_dev_cfg = {
        .dev_type = ESP_CODEC_DEV_TYPE_OUT,
        .codec_if = es8311_codec_if_,
        .data_if = i2s_data_if_,
    };
    output_dev_ = esp_codec_dev_new(&output_dev_cfg);
    assert(output_dev_ != nullptr);

    audio_codec_i2c_cfg_t es7210_i2c_cfg = {
        .port = BSP_I2C_NUM,
        .addr = ES7210_CODEC_ADDR,
        .bus_handle = i2c_bus_,
    };
    es7210_ctrl_if_ = audio_codec_new_i2c_ctrl(&es7210_i2c_cfg);
    assert(es7210_ctrl_if_ != nullptr);

    es7210_codec_cfg_t es7210_cfg = {
        .ctrl_if = es7210_ctrl_if_,
        .master_mode = false,
        .mic_selected = ES7120_SEL_MIC1 | ES7120_SEL_MIC2 | ES7120_SEL_MIC3 | ES7120_SEL_MIC4,
        .mclk_src = ES7210_MCLK_FROM_PAD,
        .mclk_div = 0,
    };
    es7210_codec_if_ = es7210_codec_new(&es7210_cfg);
    assert(es7210_codec_if_ != nullptr);

    esp_codec_dev_cfg_t input_dev_cfg = {
        .dev_type = ESP_CODEC_DEV_TYPE_IN,
        .codec_if = es7210_codec_if_,
        .data_if = i2s_data_if_,
    };
    input_dev_ = esp_codec_dev_new(&input_dev_cfg);
    assert(input_dev_ != nullptr);
}

void LichuangAudioCodec::OpenCodecDevices() {
    esp_codec_dev_sample_info_t fs = {};
    fs.bits_per_sample = kCodecBitsPerSample;
    fs.channel = kCodecChannels;
    fs.sample_rate = input_sample_rate_;

    ESP_ERROR_CHECK(esp_codec_dev_set_out_vol(output_dev_, output_volume_));
    ESP_ERROR_CHECK(esp_codec_dev_set_in_gain(input_dev_, CODEC_DEFAULT_ADC_VOLUME));
    ESP_ERROR_CHECK(esp_codec_dev_open(output_dev_, &fs));
    ESP_ERROR_CHECK(esp_codec_dev_open(input_dev_, &fs));
    ESP_ERROR_CHECK(esp_codec_dev_set_out_mute(output_dev_, true));
    ESP_ERROR_CHECK(esp_codec_dev_set_in_mute(input_dev_, false));
    if (set_pa_power_) {
        set_pa_power_(false);
    }
}

void LichuangAudioCodec::SetOutputVolume(int volume) {
    AudioCodec::SetOutputVolume(volume);
    if (output_dev_ != nullptr) {
        ESP_ERROR_CHECK(esp_codec_dev_set_out_vol(output_dev_, output_volume_));
    }
}

void LichuangAudioCodec::EnableInput(bool enable) {
    if (enable == input_enabled_) {
        return;
    }
    AudioCodec::EnableInput(enable);
    if (input_dev_ != nullptr) {
        ESP_ERROR_CHECK(esp_codec_dev_set_in_mute(input_dev_, !enable));
        if (enable) {
            ESP_ERROR_CHECK(esp_codec_dev_set_in_gain(input_dev_, CODEC_DEFAULT_ADC_VOLUME));
        }
    }
}

void LichuangAudioCodec::EnableOutput(bool enable) {
    if (enable == output_enabled_) {
        return;
    }
    AudioCodec::EnableOutput(enable);
    if (output_dev_ != nullptr) {
        if (enable) {
            ESP_ERROR_CHECK(esp_codec_dev_set_out_vol(output_dev_, output_volume_));
            if (set_pa_power_) {
                set_pa_power_(true);
            }
            ESP_ERROR_CHECK(esp_codec_dev_set_out_mute(output_dev_, false));
        } else {
            ESP_ERROR_CHECK(esp_codec_dev_set_out_mute(output_dev_, true));
            if (set_pa_power_) {
                set_pa_power_(false);
            }
        }
    }
}

int LichuangAudioCodec::Read(int16_t* dest, int samples) {
    if (dest == nullptr || samples <= 0 || input_dev_ == nullptr) {
        return 0;
    }

    const int frames = samples / input_channels_;
    if (frames <= 0) {
        return 0;
    }

    raw_input_buffer_.resize(frames * kRawInputChannels);
    int ret = esp_codec_dev_read(input_dev_, raw_input_buffer_.data(),
                                 raw_input_buffer_.size() * sizeof(int16_t));
    if (ret != ESP_CODEC_DEV_OK) {
        ESP_LOGE(TAG, "Read failed: %s (0x%x)", esp_err_to_name(ret), ret);
        return 0;
    }

    for (int i = 0; i < frames; ++i) {
        int16_t ref = raw_input_buffer_[kRawInputChannels * i + 0];
        dest[kAfeInputChannels * i + 0] = raw_input_buffer_[kRawInputChannels * i + 1];
        dest[kAfeInputChannels * i + 1] = raw_input_buffer_[kRawInputChannels * i + 3];
        dest[kAfeInputChannels * i + 2] = ref;
    }

    return frames * input_channels_;
}

int LichuangAudioCodec::Write(const int16_t* data, int samples) {
    if (data == nullptr || samples <= 0 || output_dev_ == nullptr) {
        return 0;
    }

    std::lock_guard<std::mutex> lock(output_mutex_);
    output_buffer_.resize(samples * kCodecChannels);
    for (int i = 0; i < samples; ++i) {
        int32_t sample = static_cast<int32_t>(data[i]) << 16;
        output_buffer_[kCodecChannels * i + 0] = sample;
        output_buffer_[kCodecChannels * i + 1] = sample;
    }

    int ret = esp_codec_dev_write(output_dev_, output_buffer_.data(),
                                  output_buffer_.size() * sizeof(int32_t));
    if (ret != ESP_CODEC_DEV_OK) {
        ESP_LOGE(TAG, "Write failed: %s (0x%x)", esp_err_to_name(ret), ret);
        return 0;
    }

    return samples;
}
