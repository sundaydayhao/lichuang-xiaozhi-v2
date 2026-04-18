#include "ble_hid.h"

#include <cstring>

#include <esp_bt.h>
#include <esp_hidd.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#if CONFIG_BT_ENABLED && CONFIG_BT_NIMBLE_ENABLED && CONFIG_BT_NIMBLE_HID_SERVICE
#include <host/ble_hs.h>
#include <host/ble_store.h>
#include <nimble/nimble_port.h>
#include <nimble/nimble_port_freertos.h>
#endif

namespace {

constexpr char kTag[] = "BLE_HID";
constexpr char kDeviceName[] = "xiaozhi-hid";
constexpr uint8_t kKeyboardReportId = 1;
constexpr uint8_t kConsumerReportId = 3;

constexpr uint16_t kConsumerUsagePlayPause = 0x00CD;
constexpr uint16_t kConsumerUsageVolumeUp = 0x00E9;
constexpr uint16_t kConsumerUsageVolumeDown = 0x00EA;

constexpr uint8_t kKeycodeRightArrow = 0x4F;
constexpr uint8_t kKeycodeLeftArrow = 0x50;
constexpr uint8_t kKeycodeDownArrow = 0x51;
constexpr uint8_t kKeycodeUpArrow = 0x52;

#if CONFIG_BT_ENABLED && CONFIG_BT_NIMBLE_ENABLED && CONFIG_BT_NIMBLE_HID_SERVICE
constexpr uint16_t kHidServiceUuid = 0x1812;
constexpr int32_t kAdvertisingDurationMs = 180000;

const uint8_t kCompositeReportMap[] = {
    0x05, 0x01,
    0x09, 0x06,
    0xA1, 0x01,
    0x85, kKeyboardReportId,
    0x05, 0x07,
    0x19, 0xE0,
    0x29, 0xE7,
    0x15, 0x00,
    0x25, 0x01,
    0x75, 0x01,
    0x95, 0x08,
    0x81, 0x02,
    0x95, 0x01,
    0x75, 0x08,
    0x81, 0x01,
    0x95, 0x06,
    0x75, 0x08,
    0x15, 0x00,
    0x25, 0x65,
    0x05, 0x07,
    0x19, 0x00,
    0x29, 0x65,
    0x81, 0x00,
    0xC0,

    0x05, 0x0C,
    0x09, 0x01,
    0xA1, 0x01,
    0x85, kConsumerReportId,
    0x15, 0x00,
    0x26, 0xFF, 0x03,
    0x19, 0x00,
    0x2A, 0xFF, 0x03,
    0x75, 0x10,
    0x95, 0x01,
    0x81, 0x00,
    0xC0,
};

esp_hid_raw_report_map_t kReportMaps[] = {
    {
        .data = kCompositeReportMap,
        .len = sizeof(kCompositeReportMap),
    },
};

esp_hid_device_config_t CreateBleHidConfig() {
    return {
        .vendor_id = 0x303A,
        .product_id = 0x4001,
        .version = 0x0100,
        .device_name = kDeviceName,
        .manufacturer_name = "Xiaozhi",
        .serial_number = "0001",
        .report_maps = kReportMaps,
        .report_maps_len = 1,
    };
}

extern "C" void ble_store_config_init(void);

void BleHidHostTask(void* /*param*/) {
    nimble_port_run();
    nimble_port_freertos_deinit();
}
#endif

}  // namespace

BleHid* BleHid::instance_ = nullptr;

BleHid& BleHid::GetInstance() {
    if (!instance_) {
        instance_ = new BleHid();
    }
    return *instance_;
}

void BleHid::NotifyConnectionStateChanged(bool connected) {
    if (connection_state_callback_) {
        connection_state_callback_(connected);
    }
}

void BleHid::HandleConnected() {
    if (connected_ && sec_conn_) {
        return;
    }

    connected_ = true;
    sec_conn_ = true;
    NotifyConnectionStateChanged(true);
}

void BleHid::HandleDisconnected() {
    const bool was_connected = connected_ || sec_conn_;
    connected_ = false;
    sec_conn_ = false;
    if (was_connected) {
        NotifyConnectionStateChanged(false);
    }
}

#if CONFIG_BT_ENABLED && CONFIG_BT_NIMBLE_ENABLED && CONFIG_BT_NIMBLE_HID_SERVICE
void BleHid::HiddEventCallback(void* /*handler_arg*/, esp_event_base_t event_base, int32_t event_id, void* event_data) {
    if (event_base != ESP_HIDD_EVENTS) {
        return;
    }

    auto& self = GetInstance();
    auto* param = static_cast<esp_hidd_event_data_t*>(event_data);

    switch (event_id) {
    case ESP_HIDD_START_EVENT:
        ESP_LOGI(kTag, "BLE HID stack started");
        self.StartAdvertising();
        break;

    case ESP_HIDD_CONNECT_EVENT:
        ESP_LOGI(kTag, "BLE HID connected");
        self.HandleConnected();
        break;

    case ESP_HIDD_DISCONNECT_EVENT:
        ESP_LOGI(kTag, "BLE HID disconnected, reason=%d", param ? param->disconnect.reason : -1);
        self.HandleDisconnected();
        self.StartAdvertising();
        break;

    default:
        break;
    }
}

bool BleHid::InitializeNimble() {
    esp_err_t ret = esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT);
    if (ret != ESP_OK) {
        ESP_LOGE(kTag, "Release BT classic memory failed: %s", esp_err_to_name(ret));
        return false;
    }

    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    ret = esp_bt_controller_init(&bt_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(kTag, "BT controller init failed: %s", esp_err_to_name(ret));
        return false;
    }

    ret = esp_bt_controller_enable(ESP_BT_MODE_BLE);
    if (ret != ESP_OK) {
        ESP_LOGE(kTag, "BT controller enable failed: %s", esp_err_to_name(ret));
        return false;
    }

    ret = esp_nimble_init();
    if (ret != ESP_OK) {
        ESP_LOGE(kTag, "NimBLE init failed: %s", esp_err_to_name(ret));
        return false;
    }

    ble_store_config_init();
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;
    nimble_initialized_ = true;
    return true;
}

void BleHid::DeinitializeNimble() {
    if (!nimble_initialized_) {
        return;
    }

    if (host_task_started_) {
        nimble_port_stop();
        host_task_started_ = false;
    }

    esp_nimble_deinit();
    esp_bt_controller_disable();
    esp_bt_controller_deinit();
    nimble_initialized_ = false;
}

bool BleHid::ConfigureAdvertising() {
    ble_hs_cfg.sm_io_cap = BLE_SM_IO_CAP_NO_IO;
    ble_hs_cfg.sm_bonding = 1;
    ble_hs_cfg.sm_mitm = 0;
    ble_hs_cfg.sm_sc = 1;
    ble_hs_cfg.sm_our_key_dist = BLE_SM_PAIR_KEY_DIST_ID | BLE_SM_PAIR_KEY_DIST_ENC;
    ble_hs_cfg.sm_their_key_dist = BLE_SM_PAIR_KEY_DIST_ID | BLE_SM_PAIR_KEY_DIST_ENC;
    return true;
}

bool BleHid::StartAdvertising() {
    struct ble_hs_adv_fields fields;
    std::memset(&fields, 0, sizeof(fields));

    ble_uuid16_t hid_uuid = BLE_UUID16_INIT(kHidServiceUuid);
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.appearance = ESP_HID_APPEARANCE_KEYBOARD;
    fields.appearance_is_present = 1;
    fields.tx_pwr_lvl_is_present = 1;
    fields.tx_pwr_lvl = BLE_HS_ADV_TX_PWR_LVL_AUTO;
    fields.name = reinterpret_cast<const uint8_t*>(kDeviceName);
    fields.name_len = std::strlen(kDeviceName);
    fields.name_is_complete = 1;
    fields.uuids16 = &hid_uuid;
    fields.num_uuids16 = 1;
    fields.uuids16_is_complete = 1;

    int rc = ble_gap_adv_set_fields(&fields);
    if (rc != 0) {
        ESP_LOGE(kTag, "Set advertising fields failed: %d", rc);
        return false;
    }

    uint8_t own_addr_type = BLE_OWN_ADDR_PUBLIC;
    rc = ble_hs_id_infer_auto(0, &own_addr_type);
    if (rc != 0) {
        ESP_LOGE(kTag, "Infer own address type failed: %d", rc);
        return false;
    }

    struct ble_gap_adv_params adv_params;
    std::memset(&adv_params, 0, sizeof(adv_params));
    adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;
    adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;
    adv_params.itvl_min = BLE_GAP_ADV_ITVL_MS(30);
    adv_params.itvl_max = BLE_GAP_ADV_ITVL_MS(50);

    rc = ble_gap_adv_start(own_addr_type, nullptr, kAdvertisingDurationMs, &adv_params, nullptr, nullptr);
    if (rc != 0) {
        ESP_LOGE(kTag, "Start advertising failed: %d", rc);
        return false;
    }

    return true;
}

bool BleHid::InitializeImpl() {
    if (!InitializeNimble()) {
        return false;
    }

    ConfigureAdvertising();

    auto config = CreateBleHidConfig();
    esp_err_t ret = esp_hidd_dev_init(&config, ESP_HID_TRANSPORT_BLE, HiddEventCallback, &hid_dev_);
    if (ret != ESP_OK) {
        ESP_LOGE(kTag, "esp_hidd_dev_init failed: %s", esp_err_to_name(ret));
        return false;
    }

    ret = esp_nimble_enable(reinterpret_cast<void*>(BleHidHostTask));
    if (ret != ESP_OK) {
        ESP_LOGE(kTag, "esp_nimble_enable failed: %s", esp_err_to_name(ret));
        return false;
    }

    host_task_started_ = true;
    return true;
}

void BleHid::DeinitializeImpl() {
    if (hid_dev_ != nullptr) {
        esp_hidd_dev_deinit(hid_dev_);
        hid_dev_ = nullptr;
    }

    DeinitializeNimble();
}
#endif

bool BleHid::Initialize() {
    if (initialized_) {
        return true;
    }

#if CONFIG_BT_ENABLED && CONFIG_BT_NIMBLE_ENABLED && CONFIG_BT_NIMBLE_HID_SERVICE
    if (!InitializeImpl()) {
        Deinitialize();
        return false;
    }

    initialized_ = true;
    ESP_LOGI(kTag, "BLE HID initialized with official esp_hid + NimBLE");
    return true;
#else
    ESP_LOGI(kTag, "BLE HID disabled because BT/NimBLE HID is not enabled in sdkconfig");
    return false;
#endif
}

void BleHid::Deinitialize() {
#if CONFIG_BT_ENABLED && CONFIG_BT_NIMBLE_ENABLED && CONFIG_BT_NIMBLE_HID_SERVICE
    DeinitializeImpl();
#endif
    initialized_ = false;
    connected_ = false;
    sec_conn_ = false;
}

bool BleHid::SendInputReport(uint8_t report_id, uint8_t* data, size_t length) {
#if CONFIG_BT_ENABLED && CONFIG_BT_NIMBLE_ENABLED && CONFIG_BT_NIMBLE_HID_SERVICE
    if (!initialized_ || hid_dev_ == nullptr || !IsConnected()) {
        return false;
    }

    esp_err_t ret = esp_hidd_dev_input_set(hid_dev_, 0, report_id, data, length);
    return ret == ESP_OK;
#else
    (void)report_id;
    (void)data;
    (void)length;
    return false;
#endif
}

bool BleHid::SendConsumerUsage(uint16_t usage) {
    uint8_t report[2] = {
        static_cast<uint8_t>(usage & 0xFF),
        static_cast<uint8_t>((usage >> 8) & 0xFF),
    };

    if (!SendInputReport(kConsumerReportId, report, sizeof(report))) {
        return false;
    }
    vTaskDelay(pdMS_TO_TICKS(8));

    std::memset(report, 0, sizeof(report));
    return SendInputReport(kConsumerReportId, report, sizeof(report));
}

bool BleHid::SendKeyboardUsage(uint8_t keycode) {
    uint8_t report[8] = {};
    report[2] = keycode;
    if (!SendInputReport(kKeyboardReportId, report, sizeof(report))) {
        return false;
    }
    vTaskDelay(pdMS_TO_TICKS(8));

    std::memset(report, 0, sizeof(report));
    return SendInputReport(kKeyboardReportId, report, sizeof(report));
}

bool BleHid::SendVolumeUp() {
    return SendConsumerUsage(kConsumerUsageVolumeUp);
}

bool BleHid::SendVolumeDown() {
    return SendConsumerUsage(kConsumerUsageVolumeDown);
}

bool BleHid::SendPlayPause() {
    return SendConsumerUsage(kConsumerUsagePlayPause);
}

bool BleHid::SendArrowUp() {
    return SendKeyboardUsage(kKeycodeUpArrow);
}

bool BleHid::SendArrowDown() {
    return SendKeyboardUsage(kKeycodeDownArrow);
}

bool BleHid::SendArrowLeft() {
    return SendKeyboardUsage(kKeycodeLeftArrow);
}

bool BleHid::SendArrowRight() {
    return SendKeyboardUsage(kKeycodeRightArrow);
}
