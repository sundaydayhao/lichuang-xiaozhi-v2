#ifndef _LICHUANG_AUDIO_CODEC_H
#define _LICHUANG_AUDIO_CODEC_H

#include "audio_codec.h"

#include <driver/i2c_master.h>
#include <esp_codec_dev.h>
#include <esp_codec_dev_defaults.h>

#include <functional>
#include <mutex>
#include <vector>

class LichuangAudioCodec : public AudioCodec {
public:
    LichuangAudioCodec(int input_sample_rate, int output_sample_rate,
                       i2c_master_bus_handle_t i2c_bus,
                       std::function<void(bool)> set_pa_power);
    ~LichuangAudioCodec() override;

    void SetOutputVolume(int volume) override;
    void EnableInput(bool enable) override;
    void EnableOutput(bool enable) override;

protected:
    int Read(int16_t* dest, int samples) override;
    int Write(const int16_t* data, int samples) override;

private:
    void InitializeI2s();
    void InitializeCodecDevices();
    void OpenCodecDevices();

    i2c_master_bus_handle_t i2c_bus_ = nullptr;
    i2s_chan_handle_t i2s_tx_chan_ = nullptr;
    i2s_chan_handle_t i2s_rx_chan_ = nullptr;
    const audio_codec_data_if_t* i2s_data_if_ = nullptr;
    const audio_codec_gpio_if_t* gpio_if_ = nullptr;
    const audio_codec_ctrl_if_t* es8311_ctrl_if_ = nullptr;
    const audio_codec_ctrl_if_t* es7210_ctrl_if_ = nullptr;
    const audio_codec_if_t* es8311_codec_if_ = nullptr;
    const audio_codec_if_t* es7210_codec_if_ = nullptr;
    esp_codec_dev_handle_t output_dev_ = nullptr;
    esp_codec_dev_handle_t input_dev_ = nullptr;
    std::function<void(bool)> set_pa_power_;
    std::mutex output_mutex_;
    std::vector<int16_t> raw_input_buffer_;
    std::vector<int32_t> output_buffer_;
};

#endif // _LICHUANG_AUDIO_CODEC_H
