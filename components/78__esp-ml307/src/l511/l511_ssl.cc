#include "l511_ssl.h"
#include <esp_log.h>

static const char *TAG = "L511Ssl";

L511Ssl::L511Ssl(std::shared_ptr<AtUart> at_uart, int tcp_id) : L511Tcp(at_uart, tcp_id) {
}

bool L511Ssl::ConfigureSsl(int port) {
    // L511文档中没有AT+MIPCFG和AT+MSSLCFG命令
    // SSL配置可能需要通过其他方式，或者L511不支持普通TCP的SSL连接
    // 如果AT+MSSLCFG支持，可以尝试配置
    std::string command = "AT+MSSLCFG=\"auth\",0,0";
    if (!at_uart_->SendCommand(command)) {
        ESP_LOGW(TAG, "AT+MSSLCFG failed, SSL may not be supported for TCP connections");
        // 不返回false，继续尝试连接
    }

    // L511文档中没有AT+MIPCFG命令，移除SSL配置
    // 如果L511支持TCP SSL，可能需要在AT+MIPOPEN时指定协议类型
    return true;
} 