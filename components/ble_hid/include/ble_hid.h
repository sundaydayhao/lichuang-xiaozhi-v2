#pragma once

#ifdef __cplusplus

#include <cstddef>
#include <cstdint>
#include <functional>

#include <esp_event.h>

struct esp_hidd_dev_s;
typedef struct esp_hidd_dev_s esp_hidd_dev_t;

class BleHid {
public:
    static BleHid& GetInstance();

    bool Initialize();
    void Deinitialize();

    bool SendVolumeUp();
    bool SendVolumeDown();
    bool SendPlayPause();

    bool SendArrowUp();
    bool SendArrowDown();
    bool SendArrowLeft();
    bool SendArrowRight();

    bool IsConnected() const { return connected_ && sec_conn_; }

    void OnConnectionStateChanged(std::function<void(bool connected)> callback) {
        connection_state_callback_ = callback;
    }

private:
    BleHid() = default;
    ~BleHid() = default;

    static void HiddEventCallback(void* handler_arg, esp_event_base_t event_base, int32_t event_id, void* event_data);

    void NotifyConnectionStateChanged(bool connected);
    void HandleConnected();
    void HandleDisconnected();

    bool InitializeImpl();
    void DeinitializeImpl();
    bool InitializeNimble();
    void DeinitializeNimble();
    bool ConfigureAdvertising();
    bool StartAdvertising();

    bool SendInputReport(uint8_t report_id, uint8_t* data, size_t length);
    bool SendConsumerUsage(uint16_t usage);
    bool SendKeyboardUsage(uint8_t keycode);

    bool initialized_ = false;
    bool connected_ = false;
    bool sec_conn_ = false;
    bool nimble_initialized_ = false;
    bool host_task_started_ = false;
    std::function<void(bool connected)> connection_state_callback_ = nullptr;
    esp_hidd_dev_t* hid_dev_ = nullptr;

    static BleHid* instance_;
};

#endif  // __cplusplus
