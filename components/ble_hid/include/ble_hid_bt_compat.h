#ifndef BLE_HID_BT_COMPAT_H_
#define BLE_HID_BT_COMPAT_H_

#include <stdbool.h>
#include <stdint.h>

#include "esp_assert.h"
#include "esp_err.h"
#include "esp_task.h"
#include "esp_bredr_cfg.h"
#include "sdkconfig.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ESP_BT_CONTROLLER_CONFIG_MAGIC_VAL 0x20250318

typedef enum {
    ESP_BT_MODE_IDLE = 0x00,
    ESP_BT_MODE_BLE = 0x01,
    ESP_BT_MODE_CLASSIC_BT = 0x02,
    ESP_BT_MODE_BTDM = 0x03,
} esp_bt_mode_t;

typedef enum {
    ESP_BLE_SCA_500PPM = 0,
    ESP_BLE_SCA_250PPM,
    ESP_BLE_SCA_150PPM,
    ESP_BLE_SCA_100PPM,
    ESP_BLE_SCA_75PPM,
    ESP_BLE_SCA_50PPM,
    ESP_BLE_SCA_30PPM,
    ESP_BLE_SCA_20PPM,
} esp_ble_sca_t;

#ifdef CONFIG_BT_ENABLED
#define SCAN_SEND_ADV_RESERVED_SIZE 1000
#define CONTROLLER_ADV_LOST_DEBUG_BIT (0 << 0)

#ifdef CONFIG_BTDM_CTRL_HCI_UART_NO
#define BT_HCI_UART_NO_DEFAULT CONFIG_BTDM_CTRL_HCI_UART_NO
#else
#define BT_HCI_UART_NO_DEFAULT 1
#endif

#ifdef CONFIG_BTDM_CTRL_HCI_UART_BAUDRATE
#define BT_HCI_UART_BAUDRATE_DEFAULT CONFIG_BTDM_CTRL_HCI_UART_BAUDRATE
#else
#define BT_HCI_UART_BAUDRATE_DEFAULT 921600
#endif

#ifdef CONFIG_BTDM_SCAN_DUPL_TYPE
#define SCAN_DUPLICATE_TYPE_VALUE CONFIG_BTDM_SCAN_DUPL_TYPE
#else
#define SCAN_DUPLICATE_TYPE_VALUE 0
#endif

#ifdef CONFIG_BTDM_SCAN_DUPL_CACHE_SIZE
#define NORMAL_SCAN_DUPLICATE_CACHE_SIZE CONFIG_BTDM_SCAN_DUPL_CACHE_SIZE
#else
#define NORMAL_SCAN_DUPLICATE_CACHE_SIZE 20
#endif

#ifndef CONFIG_BTDM_BLE_MESH_SCAN_DUPL_EN
#define CONFIG_BTDM_BLE_MESH_SCAN_DUPL_EN FALSE
#endif

#define SCAN_DUPLICATE_MODE_NORMAL_ADV_ONLY 0
#define SCAN_DUPLICATE_MODE_NORMAL_ADV_MESH_ADV 1

#if CONFIG_BTDM_BLE_MESH_SCAN_DUPL_EN
#define SCAN_DUPLICATE_MODE SCAN_DUPLICATE_MODE_NORMAL_ADV_MESH_ADV
#ifdef CONFIG_BTDM_MESH_DUPL_SCAN_CACHE_SIZE
#define MESH_DUPLICATE_SCAN_CACHE_SIZE CONFIG_BTDM_MESH_DUPL_SCAN_CACHE_SIZE
#else
#define MESH_DUPLICATE_SCAN_CACHE_SIZE 50
#endif
#else
#define SCAN_DUPLICATE_MODE SCAN_DUPLICATE_MODE_NORMAL_ADV_ONLY
#define MESH_DUPLICATE_SCAN_CACHE_SIZE 0
#endif

#ifdef CONFIG_BTDM_SCAN_DUPL_CACHE_REFRESH_PERIOD
#define SCAN_DUPL_CACHE_REFRESH_PERIOD CONFIG_BTDM_SCAN_DUPL_CACHE_REFRESH_PERIOD
#else
#define SCAN_DUPL_CACHE_REFRESH_PERIOD 0
#endif

#if defined(CONFIG_BTDM_CTRL_MODE_BLE_ONLY)
#define BTDM_CONTROLLER_MODE_EFF ESP_BT_MODE_BLE
#elif defined(CONFIG_BTDM_CTRL_MODE_BR_EDR_ONLY)
#define BTDM_CONTROLLER_MODE_EFF ESP_BT_MODE_CLASSIC_BT
#else
#define BTDM_CONTROLLER_MODE_EFF ESP_BT_MODE_BTDM
#endif

#ifdef CONFIG_BTDM_CTRL_AUTO_LATENCY_EFF
#define BTDM_CTRL_AUTO_LATENCY_EFF CONFIG_BTDM_CTRL_AUTO_LATENCY_EFF
#else
#define BTDM_CTRL_AUTO_LATENCY_EFF false
#endif

#ifdef CONFIG_BTDM_CTRL_HLI
#define BTDM_CTRL_HLI CONFIG_BTDM_CTRL_HLI
#else
#define BTDM_CTRL_HLI false
#endif

#ifdef CONFIG_BTDM_CTRL_LEGACY_AUTH_VENDOR_EVT_EFF
#define BTDM_CTRL_LEGACY_AUTH_VENDOR_EVT_EFF CONFIG_BTDM_CTRL_LEGACY_AUTH_VENDOR_EVT_EFF
#else
#define BTDM_CTRL_LEGACY_AUTH_VENDOR_EVT_EFF false
#endif

#ifdef CONFIG_BTDM_CTRL_SCAN_BACKOFF_UPPERLIMITMAX
#define BTDM_CTRL_SCAN_BACKOFF_UPPERLIMITMAX CONFIG_BTDM_CTRL_SCAN_BACKOFF_UPPERLIMITMAX
#else
#define BTDM_CTRL_SCAN_BACKOFF_UPPERLIMITMAX 0
#endif

#ifdef CONFIG_BTDM_BLE_LLCP_CONN_UPDATE
#define BTDM_BLE_LLCP_CONN_UPDATE (1 << 0)
#else
#define BTDM_BLE_LLCP_CONN_UPDATE (0 << 0)
#endif

#ifdef CONFIG_BTDM_BLE_LLCP_CHAN_MAP_UPDATE
#define BTDM_BLE_LLCP_CHAN_MAP_UPDATE (1 << 1)
#else
#define BTDM_BLE_LLCP_CHAN_MAP_UPDATE (0 << 1)
#endif

#define BTDM_BLE_LLCP_DISC_FLAG (BTDM_BLE_LLCP_CONN_UPDATE | BTDM_BLE_LLCP_CHAN_MAP_UPDATE)

#ifdef CONFIG_BTDM_CTRL_CHECK_CONNECT_IND_ACCESS_ADDRESS
#define BTDM_CTRL_CHECK_CONNECT_IND_ACCESS_ADDRESS_ENABLED CONFIG_BTDM_CTRL_CHECK_CONNECT_IND_ACCESS_ADDRESS
#else
#define BTDM_CTRL_CHECK_CONNECT_IND_ACCESS_ADDRESS_ENABLED 0
#endif

#if defined(CONFIG_BTDM_BLE_CHAN_ASS_EN)
#define BTDM_BLE_CHAN_ASS_EN CONFIG_BTDM_BLE_CHAN_ASS_EN
#else
#define BTDM_BLE_CHAN_ASS_EN 0
#endif

#if CONFIG_BTDM_CTRL_CONTROLLER_DEBUG_MODE_1
#define BTDM_CTRL_CONTROLLER_DEBUG_MODE_1 (1 << 1)
#else
#define BTDM_CTRL_CONTROLLER_DEBUG_MODE_1 0
#endif

#ifndef BTDM_CTRL_CONTROLLER_DEBUG_FLAG
#define BTDM_CTRL_CONTROLLER_DEBUG_FLAG (BTDM_CTRL_CONTROLLER_DEBUG_MODE_1 | CONTROLLER_ADV_LOST_DEBUG_BIT)
#endif

#if defined(CONFIG_BTDM_BLE_PING_EN)
#define BTDM_BLE_PING_EN CONFIG_BTDM_BLE_PING_EN
#else
#define BTDM_BLE_PING_EN 0
#endif

#define BT_CONTROLLER_INIT_CONFIG_DEFAULT() {                           \
    .controller_task_stack_size = ESP_TASK_BT_CONTROLLER_STACK,         \
    .controller_task_prio = ESP_TASK_BT_CONTROLLER_PRIO,                \
    .hci_uart_no = BT_HCI_UART_NO_DEFAULT,                              \
    .hci_uart_baudrate = BT_HCI_UART_BAUDRATE_DEFAULT,                  \
    .scan_duplicate_mode = SCAN_DUPLICATE_MODE,                         \
    .scan_duplicate_type = SCAN_DUPLICATE_TYPE_VALUE,                   \
    .normal_adv_size = NORMAL_SCAN_DUPLICATE_CACHE_SIZE,                \
    .mesh_adv_size = MESH_DUPLICATE_SCAN_CACHE_SIZE,                    \
    .send_adv_reserved_size = SCAN_SEND_ADV_RESERVED_SIZE,              \
    .controller_debug_flag = BTDM_CTRL_CONTROLLER_DEBUG_FLAG,           \
    .mode = BTDM_CONTROLLER_MODE_EFF,                                   \
    .ble_max_conn = CONFIG_BTDM_CTRL_BLE_MAX_CONN_EFF,                  \
    .bt_max_acl_conn = CONFIG_BTDM_CTRL_BR_EDR_MAX_ACL_CONN_EFF,        \
    .bt_sco_datapath = CONFIG_BTDM_CTRL_BR_EDR_SCO_DATA_PATH_EFF,       \
    .auto_latency = BTDM_CTRL_AUTO_LATENCY_EFF,                         \
    .bt_legacy_auth_vs_evt = BTDM_CTRL_LEGACY_AUTH_VENDOR_EVT_EFF,      \
    .bt_max_sync_conn = UT_BR_EDR_CTRL_MAX_SYNC_CONN_EFF,               \
    .ble_sca = CONFIG_BTDM_BLE_SLEEP_CLOCK_ACCURACY_INDEX_EFF,          \
    .pcm_role = CONFIG_BTDM_CTRL_PCM_ROLE_EFF,                          \
    .pcm_polar = CONFIG_BTDM_CTRL_PCM_POLAR_EFF,                        \
    .pcm_fsyncshp = CONFIG_BTDM_CTRL_PCM_FSYNCSHP_EFF,                  \
    .hli = BTDM_CTRL_HLI,                                               \
    .enc_key_sz_min = CONFIG_BTDM_CTRL_BR_EDR_MIN_ENC_KEY_SZ_DFT_EFF,   \
    .dup_list_refresh_period = SCAN_DUPL_CACHE_REFRESH_PERIOD,          \
    .ble_scan_backoff = BTDM_CTRL_SCAN_BACKOFF_UPPERLIMITMAX,           \
    .ble_llcp_disc_flag = BTDM_BLE_LLCP_DISC_FLAG,                      \
    .ble_aa_check = BTDM_CTRL_CHECK_CONNECT_IND_ACCESS_ADDRESS_ENABLED, \
    .ble_chan_ass_en = BTDM_BLE_CHAN_ASS_EN,                            \
    .ble_ping_en = BTDM_BLE_PING_EN,                                    \
    .magic = ESP_BT_CONTROLLER_CONFIG_MAGIC_VAL,                        \
}
#else
#define BT_CONTROLLER_INIT_CONFIG_DEFAULT() \
    { 0 };                                  \
    ESP_STATIC_ASSERT(0, "please enable bluetooth in menuconfig to use esp_bt.h")
#endif

typedef struct {
    uint16_t controller_task_stack_size;
    uint8_t controller_task_prio;
    uint8_t hci_uart_no;
    uint32_t hci_uart_baudrate;
    uint8_t scan_duplicate_mode;
    uint8_t scan_duplicate_type;
    uint16_t normal_adv_size;
    uint16_t mesh_adv_size;
    uint16_t send_adv_reserved_size;
    uint32_t controller_debug_flag;
    uint8_t mode;
    uint8_t ble_max_conn;
    uint8_t bt_max_acl_conn;
    uint8_t bt_sco_datapath;
    bool auto_latency;
    bool bt_legacy_auth_vs_evt;
    uint8_t bt_max_sync_conn;
    uint8_t ble_sca;
    uint8_t pcm_role;
    uint8_t pcm_polar;
    uint8_t pcm_fsyncshp;
    bool hli;
    uint8_t enc_key_sz_min;
    uint16_t dup_list_refresh_period;
    bool ble_scan_backoff;
    uint8_t ble_llcp_disc_flag;
    bool ble_aa_check;
    uint8_t ble_chan_ass_en;
    uint8_t ble_ping_en;
    uint32_t magic;
} esp_bt_controller_config_t;

typedef enum {
    ESP_BT_CONTROLLER_STATUS_IDLE = 0,
    ESP_BT_CONTROLLER_STATUS_INITED,
    ESP_BT_CONTROLLER_STATUS_ENABLED,
    ESP_BT_CONTROLLER_STATUS_NUM,
} esp_bt_controller_status_t;

esp_err_t esp_bt_controller_init(esp_bt_controller_config_t *cfg);
esp_err_t esp_bt_controller_deinit(void);
esp_err_t esp_bt_controller_enable(esp_bt_mode_t mode);
esp_err_t esp_bt_controller_disable(void);
esp_bt_controller_status_t esp_bt_controller_get_status(void);
esp_err_t esp_bt_controller_mem_release(esp_bt_mode_t mode);

#ifdef __cplusplus
}
#endif

#endif
