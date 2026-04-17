#include "l511_tcp.h"
#include <esp_log.h>
#include <cstring>

#define TAG "L511Tcp"

L511Tcp::L511Tcp(std::shared_ptr<AtUart> at_uart, int tcp_id) : at_uart_(at_uart), tcp_id_(tcp_id) {
    event_group_handle_ = xEventGroupCreate();

    urc_callback_it_ = at_uart_->RegisterUrcCallback([this](const std::string& command, const std::vector<AtArgumentValue>& arguments) {
        if (command == "MIPOPEN" && arguments.size() == 2) {
            if (arguments[0].int_value == tcp_id_) {
                connected_ = arguments[1].int_value == 0;
                if (connected_) {
                    instance_active_ = true;
                    xEventGroupClearBits(event_group_handle_, L511_TCP_DISCONNECTED | L511_TCP_ERROR);
                    xEventGroupSetBits(event_group_handle_, L511_TCP_CONNECTED);
                } else {
                    xEventGroupSetBits(event_group_handle_, L511_TCP_ERROR);
                }
            }
        } else if (command == "MIPCLOSE" && arguments.size() == 1) {
            if (arguments[0].int_value == tcp_id_) {
                instance_active_ = false;
                xEventGroupSetBits(event_group_handle_, L511_TCP_DISCONNECTED);
            }
        } else if (command == "MIPSEND" && arguments.size() == 2) {
            if (arguments[0].int_value == tcp_id_) {
                xEventGroupSetBits(event_group_handle_, L511_TCP_SEND_COMPLETE);
            }
        } else if (command == "MIPURC" && arguments.size() >= 3) {
            if (arguments[1].int_value == tcp_id_) {
                if (arguments[0].string_value == "rtcp") {
                    if (connected_ && stream_callback_) {
                        stream_callback_(at_uart_->DecodeHex(arguments[3].string_value));
                    }
                } else if (arguments[0].string_value == "disconn") {
                    if (connected_) {
                        connected_ = false;
                        if (disconnect_callback_) {
                            disconnect_callback_();
                        }
                    }
                    instance_active_ = false;
                    xEventGroupSetBits(event_group_handle_, L511_TCP_DISCONNECTED);
                } else {
                    ESP_LOGE(TAG, "Unknown MIPURC command: %s", arguments[0].string_value.c_str());
                }
            }
        } else if (command == "FIFO_OVERFLOW") {
            xEventGroupSetBits(event_group_handle_, L511_TCP_ERROR);
            Disconnect();
        }
    });
}

L511Tcp::~L511Tcp() {
    Disconnect();
    at_uart_->UnregisterUrcCallback(urc_callback_it_);
    if (event_group_handle_) {
        vEventGroupDelete(event_group_handle_);
    }
}

bool L511Tcp::Connect(const std::string& host, int port) {
    // Clear bits
    xEventGroupClearBits(event_group_handle_, L511_TCP_CONNECTED | L511_TCP_DISCONNECTED | L511_TCP_ERROR);

    // L511不支持AT+MIPSTATE查询命令，直接尝试关闭旧连接（如果存在）
    if (instance_active_) {
        std::string command = "AT+MIPCLOSE=" + std::to_string(tcp_id_);
        if (at_uart_->SendCommand(command)) {
            // 等待断开完成
            xEventGroupWaitBits(event_group_handle_, L511_TCP_DISCONNECTED, pdTRUE, pdFALSE, pdMS_TO_TICKS(2000));
        }
        instance_active_ = false;
        connected_ = false;
    }

    // 配置SSL（子类可以重写）
    if (!ConfigureSsl(port)) {
        ESP_LOGE(TAG, "Failed to configure SSL");
        return false;
    }

    // L511文档中没有AT+MIPCFG命令，直接尝试打开TCP连接
    // 如果MIPSEND需要HEX编码，可能默认支持或需要在发送时处理
    std::string command = "AT+MIPOPEN=" + std::to_string(tcp_id_) + ",\"TCP\",\"" + host + "\"," + std::to_string(port) + ",,0";
    if (!at_uart_->SendCommand(command)) {
        ESP_LOGE(TAG, "Failed to open TCP connection, error=%d", at_uart_->GetCmeErrorCode());
        return false;
    }

    // 等待连接完成
    auto bits = xEventGroupWaitBits(event_group_handle_, L511_TCP_CONNECTED | L511_TCP_ERROR, pdTRUE, pdFALSE, TCP_CONNECT_TIMEOUT_MS / portTICK_PERIOD_MS);
    if (bits & L511_TCP_ERROR) {
        ESP_LOGE(TAG, "Failed to connect to %s:%d", host.c_str(), port);
        return false;
    }
    if (!(bits & L511_TCP_CONNECTED)) {
        ESP_LOGE(TAG, "TCP connection timeout");
        return false;
    }
    return true;
}

void L511Tcp::Disconnect() {
    if (!instance_active_) {
        return;
    }
    
    std::string command = "AT+MIPCLOSE=" + std::to_string(tcp_id_);
    if (at_uart_->SendCommand(command)) {
        xEventGroupWaitBits(event_group_handle_, L511_TCP_DISCONNECTED, pdTRUE, pdFALSE, pdMS_TO_TICKS(TCP_CONNECT_TIMEOUT_MS));
    }

    if (connected_) {
        connected_ = false;
        if (disconnect_callback_) {
            disconnect_callback_();
        }
    }
}

bool L511Tcp::ConfigureSsl(int port) {
    // L511文档中没有AT+MIPCFG命令，普通TCP连接不需要SSL配置
    // SSL配置由子类L511Ssl处理
    return true;
}

int L511Tcp::Send(const std::string& data) {
    const size_t MAX_PACKET_SIZE = 1460 / 2;
    size_t total_sent = 0;

    if (!connected_) {
        ESP_LOGE(TAG, "Not connected");
        return -1;
    }

    // 在循环外预先分配command
    std::string command;
    command.reserve(32 + MAX_PACKET_SIZE * 2);  // 预分配最大可能需要的空间

    while (total_sent < data.size()) {
        size_t chunk_size = std::min(data.size() - total_sent, MAX_PACKET_SIZE);
        
        // 重置command并构建新的命令，利用预分配的容量
        command.clear();
        command += "AT+MIPSEND=";
        command += std::to_string(tcp_id_);
        command += ",";
        command += std::to_string(chunk_size);
        command += ",";
        
        // 直接在command字符串上进行十六进制编码
        at_uart_->EncodeHexAppend(command, data.data() + total_sent, chunk_size);
        command += "\r\n";
        
        // 根据波特率和命令长度动态计算超时：传输时间(10位/字节) + 处理余量
        int baud = at_uart_->GetBaudRate();
        if (baud <= 0) baud = 115200;
        size_t bytes_to_tx = command.size();
        // 发送位数≈字节*10（1起始+8数据+1停止），转毫秒
        uint32_t tx_time_ms = static_cast<uint32_t>((bytes_to_tx * 10ULL * 1000ULL) / static_cast<uint32_t>(baud));
        uint32_t timeout_ms = tx_time_ms + 100; // 余量

        if (!at_uart_->SendCommand(command, timeout_ms, false)) {
            ESP_LOGE(TAG, "Failed to send data chunk");
            Disconnect();
            return -1;
        }

        auto bits = xEventGroupWaitBits(event_group_handle_, L511_TCP_SEND_COMPLETE, pdTRUE, pdFALSE, pdMS_TO_TICKS(TCP_CONNECT_TIMEOUT_MS));
        if (!(bits & L511_TCP_SEND_COMPLETE)) {
            ESP_LOGE(TAG, "No send confirmation received");
            return -1;
        }

        total_sent += chunk_size;
    }
    return data.size();
}
