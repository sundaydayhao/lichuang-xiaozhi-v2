#pragma once

#ifdef __cplusplus

#include <cstddef>
#include <cstdint>
#include <functional>

#include <esp_event.h>

struct esp_hidd_dev_s;
typedef struct esp_hidd_dev_s esp_hidd_dev_t;
struct ble_gap_event;

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
    bool SendMouseClick(uint8_t button_mask = 0x01);
    bool SendSwipeUp();
    bool SendSwipeDown();
    bool SendSwipeLeft();
    bool SendSwipeRight();
    void SetScreenActive(bool active);

    bool IsConnected() const { return connected_ && sec_conn_; }

    void OnConnectionStateChanged(std::function<void(bool connected)> callback) {
        connection_state_callback_ = callback;
    }

private:
    BleHid() = default;
    ~BleHid() = default;

    static void HiddEventCallback(void* handler_arg, esp_event_base_t event_base, int32_t event_id, void* event_data);
    static int GapEventCallback(struct ble_gap_event* event, void* arg);

    void NotifyConnectionStateChanged(bool connected);
    void HandleConnected();
    void HandleDisconnected();

    bool InitializeImpl();
    void DeinitializeImpl();
    bool InitializeNimble();
    void DeinitializeNimble();
    bool ConfigureAdvertising();
    bool StartAdvertising();
    void StopAdvertising();

    bool SendInputReport(uint8_t report_id, uint8_t* data, size_t length);
    bool SendConsumerUsage(uint16_t usage);
    bool SendKeyboardUsage(uint8_t keycode);
    bool SendMouseReport(uint8_t buttons, int8_t delta_x, int8_t delta_y, int8_t wheel = 0);
    bool SendSwipeGesture(int8_t delta_x, int8_t delta_y);

    bool initialized_ = false;
    bool connected_ = false;
    bool sec_conn_ = false;
    bool nimble_initialized_ = false;
    bool host_task_started_ = false;
    bool screen_active_ = false;
    bool prefer_bonded_reconnect_ = true;
    bool directed_advertising_active_ = false;
    std::function<void(bool connected)> connection_state_callback_ = nullptr;
    esp_hidd_dev_t* hid_dev_ = nullptr;

    static BleHid* instance_;
};

#endif  // __cplusplus
