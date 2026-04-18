#include "ble_hid.h"
#include <esp_log.h>
#include <esp_bt.h>
#include <esp_bt_main.h>
#include <esp_bt_device.h>
#include <esp_err.h>
#include <esp_heap_caps.h>
#include "hid_dev.h"
#include "esp_hidd_api.h"
#include "hidd_le_prf_int.h"

#define TAG "BLE_HID"

BleHid* BleHid::instance_ = nullptr;

namespace {

void LogInternalHeap(const char* stage) {
    ESP_LOGI(TAG,
             "%s internal heap free=%u largest=%u minimum=%u",
             stage,
             static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)),
             static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL)),
             static_cast<unsigned>(heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL)));
}

void CleanupBleStackAfterInitFailure() {
    const esp_bluedroid_status_t bluedroid_status = esp_bluedroid_get_status();
    const esp_bt_controller_status_t controller_status = esp_bt_controller_get_status();

    if (bluedroid_status == ESP_BLUEDROID_STATUS_ENABLED) {
        esp_err_t ret = esp_bluedroid_disable();
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "esp_bluedroid_disable cleanup failed: %s (%d)",
                     esp_err_to_name(ret), ret);
        }
    }

    if (bluedroid_status == ESP_BLUEDROID_STATUS_ENABLED ||
        bluedroid_status == ESP_BLUEDROID_STATUS_INITIALIZED) {
        esp_err_t ret = esp_bluedroid_deinit();
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "esp_bluedroid_deinit cleanup failed: %s (%d)",
                     esp_err_to_name(ret), ret);
        }
    }

    if (controller_status == ESP_BT_CONTROLLER_STATUS_ENABLED) {
        esp_err_t ret = esp_bt_controller_disable();
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "esp_bt_controller_disable cleanup failed: %s (%d)",
                     esp_err_to_name(ret), ret);
        }
    }

    if (controller_status == ESP_BT_CONTROLLER_STATUS_ENABLED ||
        controller_status == ESP_BT_CONTROLLER_STATUS_INITED) {
        esp_err_t ret = esp_bt_controller_deinit();
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "esp_bt_controller_deinit cleanup failed: %s (%d)",
                     esp_err_to_name(ret), ret);
        }
    }
}

} // namespace

#define HIDD_DEVICE_NAME "Xiaozhi_HID"
static uint8_t hidd_service_uuid128[] = {
    0xfb, 0x34, 0x9b, 0x5f, 0x80, 0x00, 0x00, 0x80, 0x00, 0x10, 0x00, 0x00, 0x12, 0x18, 0x00, 0x00,
};

static esp_ble_adv_data_t hidd_adv_data = {
    .set_scan_rsp = false,
    .include_name = true,
    .include_txpower = true,
    .min_interval = 0x0006,
    .max_interval = 0x0010,
    .appearance = 0x03c0,
    .manufacturer_len = 0,
    .p_manufacturer_data = NULL,
    .service_data_len = 0,
    .p_service_data = NULL,
    .service_uuid_len = sizeof(hidd_service_uuid128),
    .p_service_uuid = hidd_service_uuid128,
    .flag = 0x6,
};

static esp_ble_adv_params_t hidd_adv_params = {
    .adv_int_min = 0x20,
    .adv_int_max = 0x30,
    .adv_type = ADV_TYPE_IND,
    .own_addr_type = BLE_ADDR_TYPE_PUBLIC,
    .channel_map = ADV_CHNL_ALL,
    .adv_filter_policy = ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY,
};

void BleHid::HiddEventCallback(esp_hidd_cb_event_t event, esp_hidd_cb_param_t *param) {
    auto* self = BleHid::instance_;
    if (!self) return;
    
    switch(event) {
        case ESP_HIDD_EVENT_REG_FINISH:
            if (param->init_finish.state == ESP_HIDD_INIT_OK) {
                esp_ble_gap_set_device_name(HIDD_DEVICE_NAME);
                esp_ble_gap_config_adv_data(&hidd_adv_data);
            }
            break;
        case ESP_HIDD_EVENT_BLE_CONNECT:
            ESP_LOGI(TAG, "BLE HID Connected");
            self->connected_ = true;
            self->hid_conn_id_ = param->connect.conn_id;
            if (self->connection_state_callback_) {
                self->connection_state_callback_(true);
            }
            break;
        case ESP_HIDD_EVENT_BLE_DISCONNECT:
            ESP_LOGI(TAG, "BLE HID Disconnected");
            self->connected_ = false;
            self->sec_conn_ = false;
            esp_ble_gap_start_advertising(&hidd_adv_params);
            if (self->connection_state_callback_) {
                self->connection_state_callback_(false);
            }
            break;
        default:
            break;
    }
}

void BleHid::GapEventHandler(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param) {
    switch (event) {
        case ESP_GAP_BLE_ADV_DATA_SET_COMPLETE_EVT:
            esp_ble_gap_start_advertising(&hidd_adv_params);
            break;
        case ESP_GAP_BLE_SEC_REQ_EVT:
            esp_ble_gap_security_rsp(param->ble_security.ble_req.bd_addr, true);
            break;
        case ESP_GAP_BLE_AUTH_CMPL_EVT:
            if (param->ble_security.auth_cmpl.success) {
                auto* self = BleHid::instance_;
                self->sec_conn_ = true;
                ESP_LOGI(TAG, "BLE HID Security connection established");
                // 安全连接建立后也通知一次（此时才真正可用）
                if (self && self->connection_state_callback_ && self->connected_) {
                    self->connection_state_callback_(true);
                }
            }
            break;
        default:
            break;
    }
}

BleHid& BleHid::GetInstance() {
    if (!instance_) {
        instance_ = new BleHid();
    }
    return *instance_;
}

bool BleHid::Initialize() {
    if (initialized_) {
        return true;
    }
    
    esp_err_t ret;
    connected_ = false;
    sec_conn_ = false;
    hid_conn_id_ = 0;

    LogInternalHeap("Before BLE init");
    
    // 释放经典蓝牙内存
    ret = esp_bt_mem_release(ESP_BT_MODE_CLASSIC_BT);
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGW(TAG, "esp_bt_mem_release failed: %s (%d)", esp_err_to_name(ret), ret);
    }
    LogInternalHeap("After BT classic mem release");
    
    // 初始化蓝牙控制器
    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    ret = esp_bt_controller_init(&bt_cfg);
    if (ret != ESP_OK) {
        LogInternalHeap("BT controller init failed");
        ESP_LOGE(TAG, "BT controller init failed: %s (%d)", esp_err_to_name(ret), ret);
        CleanupBleStackAfterInitFailure();
        return false;
    }
    
    ret = esp_bt_controller_enable(ESP_BT_MODE_BLE);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "BT controller enable failed: %s (%d)", esp_err_to_name(ret), ret);
        CleanupBleStackAfterInitFailure();
        return false;
    }
    
    // 初始化 Bluedroid
    ret = esp_bluedroid_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Bluedroid init failed: %s (%d)", esp_err_to_name(ret), ret);
        CleanupBleStackAfterInitFailure();
        return false;
    }
    
    ret = esp_bluedroid_enable();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Bluedroid enable failed: %s (%d)", esp_err_to_name(ret), ret);
        CleanupBleStackAfterInitFailure();
        return false;
    }
    
    // 初始化 HID Profile
    ret = esp_hidd_profile_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "HID profile init failed: %s (%d)", esp_err_to_name(ret), ret);
        CleanupBleStackAfterInitFailure();
        return false;
    }
    
    // 注册回调
    ret = esp_ble_gap_register_callback(GapEventHandler);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "BLE GAP callback registration failed: %s (%d)",
                 esp_err_to_name(ret), ret);
        CleanupBleStackAfterInitFailure();
        return false;
    }

    ret = esp_hidd_register_callbacks(HiddEventCallback);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "BLE HID callback registration failed: %s (%d)",
                 esp_err_to_name(ret), ret);
        CleanupBleStackAfterInitFailure();
        return false;
    }
    
    // 设置安全参数
    esp_ble_auth_req_t auth_req = ESP_LE_AUTH_BOND;
    esp_ble_io_cap_t iocap = ESP_IO_CAP_NONE;
    uint8_t key_size = 16;
    uint8_t init_key = ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK;
    uint8_t rsp_key = ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK;
    
    esp_ble_gap_set_security_param(ESP_BLE_SM_AUTHEN_REQ_MODE, &auth_req, sizeof(uint8_t));
    esp_ble_gap_set_security_param(ESP_BLE_SM_IOCAP_MODE, &iocap, sizeof(uint8_t));
    esp_ble_gap_set_security_param(ESP_BLE_SM_MAX_KEY_SIZE, &key_size, sizeof(uint8_t));
    esp_ble_gap_set_security_param(ESP_BLE_SM_SET_INIT_KEY, &init_key, sizeof(uint8_t));
    esp_ble_gap_set_security_param(ESP_BLE_SM_SET_RSP_KEY, &rsp_key, sizeof(uint8_t));
    
    initialized_ = true;
    LogInternalHeap("BLE HID initialized");
    ESP_LOGI(TAG, "BLE HID initialized");
    return true;
}

void BleHid::Deinitialize() {
    if (!initialized_) return;
    
    // 先反初始化 HID Profile
    esp_hidd_profile_deinit();
    
    // 注销回调
    esp_ble_gap_register_callback(NULL);
    esp_hidd_register_callbacks(NULL);
    
    CleanupBleStackAfterInitFailure();
    
    initialized_ = false;
    connected_ = false;
    sec_conn_ = false;
}

bool BleHid::SendVolumeUp() {
    if (!connected_ || !sec_conn_) {
        return false;
    }
    esp_hidd_send_consumer_value(hid_conn_id_, HID_CONSUMER_VOLUME_UP, true);
    vTaskDelay(pdMS_TO_TICKS(3));
    esp_hidd_send_consumer_value(hid_conn_id_, HID_CONSUMER_VOLUME_UP, false);
    return true;
}

bool BleHid::SendVolumeDown() {
    if (!connected_ || !sec_conn_) {
        return false;
    }
    esp_hidd_send_consumer_value(hid_conn_id_, HID_CONSUMER_VOLUME_DOWN, true);
    vTaskDelay(pdMS_TO_TICKS(3));
    esp_hidd_send_consumer_value(hid_conn_id_, HID_CONSUMER_VOLUME_DOWN, false);
    return true;
}

bool BleHid::SendPlayPause() {
    if (!connected_ || !sec_conn_) {
        return false;
    }
    esp_hidd_send_consumer_value(hid_conn_id_, HID_CONSUMER_PLAY_PAUSE, true);
    vTaskDelay(pdMS_TO_TICKS(3));
    esp_hidd_send_consumer_value(hid_conn_id_, HID_CONSUMER_PLAY_PAUSE, false);
    return true;
}
