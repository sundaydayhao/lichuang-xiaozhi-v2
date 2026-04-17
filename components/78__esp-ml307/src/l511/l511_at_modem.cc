#include "l511_at_modem.h"
#include <esp_log.h>
#include <esp_err.h>
#include <cassert>
#include <sstream>
#include <iomanip>
#include <cstring>
#include "l511_tcp.h"
#include "l511_ssl.h"
#include "l511_udp.h"
#include "l511_mqtt.h"
#include "l511_http.h"
#include "web_socket.h"

#define TAG "L511AtModem"


L511AtModem::L511AtModem(std::shared_ptr<AtUart> at_uart) : AtModem(at_uart) {
    // 子类特定的初始化在这里
    // L511使用HttpClient，不需要ResetConnections
}

void L511AtModem::ResetConnections() {
    // L511使用HttpClient，不需要重置HTTP连接
}

void L511AtModem::HandleUrc(const std::string& command, const std::vector<AtArgumentValue>& arguments) {
    // Handle Common URC
    AtModem::HandleUrc(command, arguments);
    // Handle L511 URC
    if (command == "QIACT" && arguments.size() >= 4) {
        // +QIACT: <contextID>,<state>,<type>,<IP>
        // state=1 表示已激活，type=1 表示IPV4
        int context_id = arguments[0].int_value;
        int state = arguments[1].int_value;
        int type = arguments[2].int_value;
        std::string ip = arguments[3].string_value;
        
        ESP_LOGI(TAG, "Received QIACT URC: contextID=%d, state=%d, type=%d, IP=%s", 
                 context_id, state, type, ip.c_str());
        
        if (state == 1) { // state=1 表示已激活
            ESP_LOGI(TAG, "PDP Context %d activated, IP: %s", context_id, ip.c_str());
            network_ready_ = true;
            xEventGroupSetBits(event_group_handle_, AT_EVENT_NETWORK_READY);
        }
    } else if (command == "MIPCALL" && arguments.size() >= 3) {
        if (arguments[1].int_value == 1) {
            auto ip = arguments[2].string_value;
            ESP_LOGI(TAG, "PDP Context %d IP: %s", arguments[0].int_value, ip.c_str());
            network_ready_ = true;
            xEventGroupSetBits(event_group_handle_, AT_EVENT_NETWORK_READY);
        }
    } else if (command == "MATREADY") {
        if (network_ready_) {
            network_ready_ = false;
            if (on_network_state_changed_) {
                on_network_state_changed_(false);
            }
        }
    }
}

void L511AtModem::Reboot() {
    at_uart_->SendCommand("AT+MREBOOT=0");
}

bool L511AtModem::SetSleepMode(bool enable, int delay_seconds) {
    if (enable) {
        if (delay_seconds > 0) {
            at_uart_->SendCommand("AT+MLPMCFG=\"delaysleep\"," + std::to_string(delay_seconds));
        }
        return at_uart_->SendCommand("AT+MLPMCFG=\"sleepmode\",2,0");
    } else {
        return at_uart_->SendCommand("AT+MLPMCFG=\"sleepmode\",0,0");
    }
}

NetworkStatus L511AtModem::WaitForNetworkReady(int timeout_ms) {
    NetworkStatus status = AtModem::WaitForNetworkReady(timeout_ms);
    if (status == NetworkStatus::Ready) {
        ESP_LOGI(TAG, "Network registered, trying to get IP address...");
        
        // L511使用AT+QIACT?查询IP地址
        // 先尝试激活PDP上下文（如果需要）
        ESP_LOGI(TAG, "Sending AT+QIACT=1 to activate PDP context...");
        at_uart_->SendCommand("AT+QIACT=1", 5000);
        vTaskDelay(pdMS_TO_TICKS(2000)); // 等待PDP激活
        
        // 清除网络就绪事件位，准备等待QIACT URC
        xEventGroupClearBits(event_group_handle_, AT_EVENT_NETWORK_READY);
        
        // 查询IP地址，通过URC回调获取
        for (int i = 0; i < 10; i++) {
            ESP_LOGI(TAG, "Query attempt %d/10: AT+QIACT?", i + 1);
            
            // 发送查询命令
            if (at_uart_->SendCommand("AT+QIACT?", 5000)) {
                // 等待URC回调设置AT_EVENT_NETWORK_READY事件
                // QIACT URC会在HandleUrc中处理并设置事件
                auto bits = xEventGroupWaitBits(event_group_handle_, AT_EVENT_NETWORK_READY, 
                                                pdTRUE, pdFALSE, pdMS_TO_TICKS(1000));
            if (bits & AT_EVENT_NETWORK_READY) {
                    ESP_LOGI(TAG, "IP address obtained via QIACT URC");
                return NetworkStatus::Ready;
            }
            } else {
                ESP_LOGE(TAG, "AT+QIACT? command failed (attempt %d)", i + 1);
                int error_code = at_uart_->GetCmeErrorCode();
                if (error_code != 0) {
                    ESP_LOGE(TAG, "CME Error code: %d", error_code);
                }
            }
            vTaskDelay(pdMS_TO_TICKS(500));
        }
        ESP_LOGE(TAG, "Network ready but no IP address after 10 attempts");
    }
    return status;
}

std::unique_ptr<Http> L511AtModem::CreateHttp(int connect_id) {
    assert(connect_id >= 0);
    return std::make_unique<L511Http>(at_uart_);
}

std::unique_ptr<Tcp> L511AtModem::CreateTcp(int connect_id) {
    assert(connect_id >= 0);
    return std::make_unique<L511Tcp>(at_uart_, connect_id);
}

std::unique_ptr<Tcp> L511AtModem::CreateSsl(int connect_id) {
    assert(connect_id >= 0);
    return std::make_unique<L511Ssl>(at_uart_, connect_id);
}

std::unique_ptr<Udp> L511AtModem::CreateUdp(int connect_id) {
    assert(connect_id >= 0);
    return std::make_unique<L511Udp>(at_uart_, connect_id);
}

std::unique_ptr<Mqtt> L511AtModem::CreateMqtt(int connect_id) {
    assert(connect_id >= 0);
    return std::make_unique<L511Mqtt>(at_uart_, connect_id);
}

std::unique_ptr<WebSocket> L511AtModem::CreateWebSocket(int connect_id) {
    assert(connect_id >= 0);
    return std::make_unique<WebSocket>(this, connect_id);
}
