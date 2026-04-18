#pragma once

#include <driver/gpio.h>

#if CONFIG_BT_ENABLED
#include "esp_hidd_prf_api.h"
#include "hid_dev.h"
#include "hidd_le_prf_int.h"

#include <esp_gap_ble_api.h>
#include <esp_bt_defs.h>
#endif

#ifdef __cplusplus
#include <functional>

class BleHid {
public:
    static BleHid& GetInstance();
    
    bool Initialize();
    void Deinitialize();
    
    // 发送音量控制
    bool SendVolumeUp();
    bool SendVolumeDown();
    bool SendPlayPause();
    
    // 连接状态
    bool IsConnected() const { return connected_; }
    
    // 连接状态变化回调
    void OnConnectionStateChanged(std::function<void(bool connected)> callback) {
        connection_state_callback_ = callback;
    }
    
private:
    BleHid() = default;
    ~BleHid() = default;

#if CONFIG_BT_ENABLED
    static void HiddEventCallback(esp_hidd_cb_event_t event, esp_hidd_cb_param_t *param);
    static void GapEventHandler(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param);
#endif

    bool initialized_ = false;
    bool connected_ = false;
    bool sec_conn_ = false;
    uint16_t hid_conn_id_ = 0;
    std::function<void(bool connected)> connection_state_callback_ = nullptr;
    
    static BleHid* instance_;
};

#endif // __cplusplus
