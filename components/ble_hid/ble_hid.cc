#include "ble_hid.h"

#include <cstdio>
#include <cstring>

#include <esp_bt.h>
#include <esp_hidd.h>
#include <esp_heap_caps.h>
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
constexpr char kDeviceName[] = "Xiaozhi-HID";
constexpr uint8_t kKeyboardReportId = 1;
constexpr uint8_t kMouseReportId = 2;
constexpr uint8_t kConsumerReportId = 3;
constexpr uint8_t kMouseButtonLeft = 0x01;
constexpr int8_t kSwipeStepPixels = 18;
constexpr int kSwipeStepCount = 4;
constexpr TickType_t kInputPulseDelayTicks = pdMS_TO_TICKS(8);

constexpr uint16_t kConsumerUsagePlayPause = 0x00CD;
constexpr uint16_t kConsumerUsageVolumeUp = 0x00E9;
constexpr uint16_t kConsumerUsageVolumeDown = 0x00EA;

constexpr uint8_t kKeycodeRightArrow = 0x4F;
constexpr uint8_t kKeycodeLeftArrow = 0x50;
constexpr uint8_t kKeycodeDownArrow = 0x51;
constexpr uint8_t kKeycodeUpArrow = 0x52;

struct HeapSnapshot {
    size_t total_free = 0;
    size_t internal_free = 0;
    size_t spiram_free = 0;
    size_t largest_internal_block = 0;
};

HeapSnapshot CaptureHeapSnapshot() {
    return {
        .total_free = heap_caps_get_free_size(MALLOC_CAP_8BIT),
        .internal_free = heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
        .spiram_free = heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
        .largest_internal_block = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL),
    };
}

void LogHeapSnapshot(const char* stage, const HeapSnapshot& snapshot) {
    ESP_LOGI(kTag,
        "%s heap: total=%u, internal=%u, spiram=%u, largest_internal=%u",
        stage,
        static_cast<unsigned>(snapshot.total_free),
        static_cast<unsigned>(snapshot.internal_free),
        static_cast<unsigned>(snapshot.spiram_free),
        static_cast<unsigned>(snapshot.largest_internal_block));
}

void LogHeapDelta(const char* stage, const HeapSnapshot& before, const HeapSnapshot& after) {
    ESP_LOGI(kTag,
        "%s delta: total=%d, internal=%d, spiram=%d",
        stage,
        static_cast<int>(before.total_free - after.total_free),
        static_cast<int>(before.internal_free - after.internal_free),
        static_cast<int>(before.spiram_free - after.spiram_free));
}

#if CONFIG_BT_ENABLED && CONFIG_BT_NIMBLE_ENABLED && CONFIG_BT_NIMBLE_HID_SERVICE
constexpr uint16_t kHidServiceUuid = 0x1812;
constexpr int32_t kAdvertisingDurationMs = 180000;
constexpr int32_t kPreferredReconnectAdvertisingDurationMs = 15000;
const ble_addr_t kAnyBleAddress = {0, {0, 0, 0, 0, 0, 0}};

const uint8_t kCompositeReportMap[] = {
    0x05, 0x01,
    0x09, 0x02,
    0xA1, 0x01,
    0x85, kMouseReportId,
    0x09, 0x01,
    0xA1, 0x00,
    0x05, 0x09,
    0x19, 0x01,
    0x29, 0x03,
    0x15, 0x00,
    0x25, 0x01,
    0x75, 0x01,
    0x95, 0x03,
    0x81, 0x02,
    0x75, 0x05,
    0x95, 0x01,
    0x81, 0x01,
    0x05, 0x01,
    0x09, 0x30,
    0x09, 0x31,
    0x09, 0x38,
    0x15, 0x81,
    0x25, 0x7F,
    0x75, 0x08,
    0x95, 0x03,
    0x81, 0x06,
    0xC0,
    0xC0,

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

void FormatBleAddress(const ble_addr_t& address, char* buffer, size_t buffer_size) {
    if (buffer == nullptr || buffer_size == 0) {
        return;
    }

    std::snprintf(buffer,
        buffer_size,
        "%02X:%02X:%02X:%02X:%02X:%02X",
        address.val[5],
        address.val[4],
        address.val[3],
        address.val[2],
        address.val[1],
        address.val[0]);
}

bool LoadBondedPeerAddress(ble_addr_t* out_address) {
    if (out_address == nullptr) {
        return false;
    }

    ble_store_key_sec key = {};
    ble_store_value_sec value = {};
    key.peer_addr = kAnyBleAddress;
    key.idx = 0;

    const int rc = ble_store_read_peer_sec(&key, &value);
    if (rc != 0 || ble_addr_cmp(&value.peer_addr, &kAnyBleAddress) == 0) {
        return false;
    }

    *out_address = value.peer_addr;
    return true;
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
    prefer_bonded_reconnect_ = true;
    directed_advertising_active_ = false;
    if (was_connected) {
        NotifyConnectionStateChanged(false);
    }
}

#if CONFIG_BT_ENABLED && CONFIG_BT_NIMBLE_ENABLED && CONFIG_BT_NIMBLE_HID_SERVICE
int BleHid::GapEventCallback(struct ble_gap_event* event, void* /*arg*/) {
    if (event == nullptr) {
        return 0;
    }

    auto& self = GetInstance();

    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        if (event->connect.status != 0) {
            ESP_LOGW(kTag, "BLE GAP connect attempt failed: %d", event->connect.status);
            self.directed_advertising_active_ = false;
            if (self.screen_active_ && !self.IsConnected()) {
                self.StartAdvertising();
            }
        }
        return 0;

    case BLE_GAP_EVENT_ADV_COMPLETE:
        ESP_LOGI(kTag,
            "BLE advertising complete, reason=%d, directed=%s",
            event->adv_complete.reason,
            self.directed_advertising_active_ ? "true" : "false");
        self.directed_advertising_active_ = false;
        if (self.screen_active_ && !self.IsConnected()) {
            self.StartAdvertising();
        }
        return 0;

    default:
        return 0;
    }
}

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
        if (self.screen_active_) {
            self.StartAdvertising();
        }
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

    esp_err_t ret = nimble_port_deinit();
    if (ret != ESP_OK) {
        ESP_LOGW(kTag, "nimble_port_deinit failed: %s", esp_err_to_name(ret));
    }
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
    if (ble_gap_adv_active()) {
        return true;
    }

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

    auto start_general_advertising = [&]() -> bool {
        struct ble_gap_adv_params adv_params;
        std::memset(&adv_params, 0, sizeof(adv_params));
        adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;
        adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;
        adv_params.itvl_min = BLE_GAP_ADV_ITVL_MS(30);
        adv_params.itvl_max = BLE_GAP_ADV_ITVL_MS(50);

        directed_advertising_active_ = false;
        rc = ble_gap_adv_start(
            own_addr_type,
            nullptr,
            kAdvertisingDurationMs,
            &adv_params,
            GapEventCallback,
            nullptr);
        if (rc != 0) {
            ESP_LOGE(kTag, "Start general advertising failed: %d", rc);
            return false;
        }

        ESP_LOGI(kTag, "Started general BLE advertising");
        return true;
    };

    if (prefer_bonded_reconnect_) {
        ble_addr_t bonded_peer = kAnyBleAddress;
        prefer_bonded_reconnect_ = false;

        if (LoadBondedPeerAddress(&bonded_peer)) {
            struct ble_gap_adv_params adv_params;
            std::memset(&adv_params, 0, sizeof(adv_params));
            adv_params.conn_mode = BLE_GAP_CONN_MODE_DIR;
            adv_params.itvl_min = BLE_GAP_ADV_ITVL_MS(30);
            adv_params.itvl_max = BLE_GAP_ADV_ITVL_MS(50);

            char address_text[18] = {};
            FormatBleAddress(bonded_peer, address_text, sizeof(address_text));
            rc = ble_gap_adv_start(
                own_addr_type,
                &bonded_peer,
                kPreferredReconnectAdvertisingDurationMs,
                &adv_params,
                GapEventCallback,
                nullptr);
            if (rc == 0) {
                directed_advertising_active_ = true;
                ESP_LOGI(kTag,
                    "Started directed BLE advertising to bonded peer %s (type=%u)",
                    address_text,
                    bonded_peer.type);
                return true;
            }

            ESP_LOGW(kTag,
                "Directed advertising to bonded peer %s failed: %d, falling back to general advertising",
                address_text,
                rc);
        } else {
            ESP_LOGI(kTag, "No bonded peer found, using general BLE advertising");
        }
    }

    return start_general_advertising();
}

void BleHid::StopAdvertising() {
    if (ble_gap_adv_active()) {
        int rc = ble_gap_adv_stop();
        if (rc != 0 && rc != BLE_HS_EALREADY) {
            ESP_LOGW(kTag, "Stop advertising failed: %d", rc);
        }
    }
    directed_advertising_active_ = false;
}

bool BleHid::InitializeImpl() {
    const HeapSnapshot before_init = CaptureHeapSnapshot();
    LogHeapSnapshot("Before BLE HID init", before_init);

    if (!InitializeNimble()) {
        LogHeapSnapshot("BLE HID init failed after NimBLE init", CaptureHeapSnapshot());
        return false;
    }

    const HeapSnapshot after_nimble = CaptureHeapSnapshot();
    LogHeapSnapshot("After NimBLE init", after_nimble);
    LogHeapDelta("NimBLE stack usage", before_init, after_nimble);

    ConfigureAdvertising();

    auto config = CreateBleHidConfig();
    esp_err_t ret = esp_hidd_dev_init(&config, ESP_HID_TRANSPORT_BLE, HiddEventCallback, &hid_dev_);
    if (ret != ESP_OK) {
        ESP_LOGE(kTag, "esp_hidd_dev_init failed: %s", esp_err_to_name(ret));
        LogHeapSnapshot("BLE HID init failed after esp_hidd_dev_init", CaptureHeapSnapshot());
        return false;
    }

    const HeapSnapshot after_hid = CaptureHeapSnapshot();
    LogHeapSnapshot("After esp_hidd_dev_init", after_hid);
    LogHeapDelta("BLE HID profile usage", after_nimble, after_hid);

    ret = esp_nimble_enable(reinterpret_cast<void*>(BleHidHostTask));
    if (ret != ESP_OK) {
        ESP_LOGE(kTag, "esp_nimble_enable failed: %s", esp_err_to_name(ret));
        LogHeapSnapshot("BLE HID init failed after esp_nimble_enable", CaptureHeapSnapshot());
        return false;
    }

    const HeapSnapshot after_enable = CaptureHeapSnapshot();
    LogHeapSnapshot("After esp_nimble_enable", after_enable);
    LogHeapDelta("NimBLE host task usage", after_hid, after_enable);
    LogHeapDelta("BLE stack total usage", before_init, after_enable);

    host_task_started_ = true;
    return true;
}

void BleHid::DeinitializeImpl() {
    StopAdvertising();

    if (host_task_started_) {
        int rc = nimble_port_stop();
        if (rc != 0 && rc != BLE_HS_EALREADY) {
            ESP_LOGW(kTag, "nimble_port_stop failed: %d", rc);
        }
        host_task_started_ = false;
    }

    if (hid_dev_ != nullptr) {
        esp_err_t ret = esp_hidd_dev_deinit(hid_dev_);
        if (ret != ESP_OK) {
            ESP_LOGW(kTag, "esp_hidd_dev_deinit failed: %s", esp_err_to_name(ret));
        }
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
    prefer_bonded_reconnect_ = true;
    directed_advertising_active_ = false;
}

void BleHid::SetScreenActive(bool active) {
    screen_active_ = active;
    prefer_bonded_reconnect_ = true;

#if CONFIG_BT_ENABLED && CONFIG_BT_NIMBLE_ENABLED && CONFIG_BT_NIMBLE_HID_SERVICE
    if (!initialized_) {
        return;
    }

    if (active) {
        if (!IsConnected()) {
            StartAdvertising();
        }
    } else if (!IsConnected()) {
        StopAdvertising();
    }
#endif
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
    vTaskDelay(kInputPulseDelayTicks);

    std::memset(report, 0, sizeof(report));
    return SendInputReport(kConsumerReportId, report, sizeof(report));
}

bool BleHid::SendKeyboardUsage(uint8_t keycode) {
    uint8_t report[8] = {};
    report[2] = keycode;
    if (!SendInputReport(kKeyboardReportId, report, sizeof(report))) {
        return false;
    }
    vTaskDelay(kInputPulseDelayTicks);

    std::memset(report, 0, sizeof(report));
    return SendInputReport(kKeyboardReportId, report, sizeof(report));
}

bool BleHid::SendMouseReport(uint8_t buttons, int8_t delta_x, int8_t delta_y, int8_t wheel) {
    uint8_t report[4] = {
        buttons,
        static_cast<uint8_t>(delta_x),
        static_cast<uint8_t>(delta_y),
        static_cast<uint8_t>(wheel),
    };
    return SendInputReport(kMouseReportId, report, sizeof(report));
}

bool BleHid::SendSwipeGesture(int8_t delta_x, int8_t delta_y) {
    if (!SendMouseReport(kMouseButtonLeft, 0, 0, 0)) {
        return false;
    }
    vTaskDelay(kInputPulseDelayTicks);

    for (int step = 0; step < kSwipeStepCount; ++step) {
        if (!SendMouseReport(kMouseButtonLeft, delta_x, delta_y, 0)) {
            SendMouseReport(0, 0, 0, 0);
            return false;
        }
        vTaskDelay(kInputPulseDelayTicks);
    }

    if (!SendMouseReport(0, 0, 0, 0)) {
        return false;
    }
    vTaskDelay(kInputPulseDelayTicks);

    // Move the pointer back after the drag so repeated swipes don't pin the
    // on-screen cursor to a screen edge on the phone.
    for (int step = 0; step < kSwipeStepCount; ++step) {
        if (!SendMouseReport(0, static_cast<int8_t>(-delta_x), static_cast<int8_t>(-delta_y), 0)) {
            return false;
        }
        vTaskDelay(kInputPulseDelayTicks);
    }

    return true;
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

bool BleHid::SendMouseClick(uint8_t button_mask) {
    if (!SendMouseReport(button_mask, 0, 0, 0)) {
        return false;
    }
    vTaskDelay(kInputPulseDelayTicks);
    return SendMouseReport(0, 0, 0, 0);
}

bool BleHid::SendSwipeUp() {
    return SendSwipeGesture(0, kSwipeStepPixels);
}

bool BleHid::SendSwipeDown() {
    return SendSwipeGesture(0, -kSwipeStepPixels);
}

bool BleHid::SendSwipeLeft() {
    return SendSwipeGesture(kSwipeStepPixels, 0);
}

bool BleHid::SendSwipeRight() {
    return SendSwipeGesture(-kSwipeStepPixels, 0);
}
