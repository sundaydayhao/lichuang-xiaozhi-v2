#include "l511_udp.h"

#include <esp_log.h>

#define TAG "L511Udp"


L511Udp::L511Udp(std::shared_ptr<AtUart> at_uart, int udp_id) : at_uart_(at_uart), udp_id_(udp_id) {
    event_group_handle_ = xEventGroupCreate();

    urc_callback_it_ = at_uart_->RegisterUrcCallback([this](const std::string& command, const std::vector<AtArgumentValue>& arguments) {
        if (command == "QIOPEN" && arguments.size() == 2) {
            if (arguments[0].int_value == udp_id_) {
                connected_ = arguments[1].int_value == 0;  // err=0表示成功
                if (connected_) {
                    instance_active_ = true;
                    xEventGroupClearBits(event_group_handle_, L511_UDP_DISCONNECTED | L511_UDP_ERROR);
                    xEventGroupSetBits(event_group_handle_, L511_UDP_CONNECTED);
                } else {
                    xEventGroupSetBits(event_group_handle_, L511_UDP_ERROR);
                }
            }
        } else if (command == "QICLOSE" && arguments.size() >= 1) {
            if (arguments[0].int_value == udp_id_) {
                instance_active_ = false;
                connected_ = false;
                xEventGroupSetBits(event_group_handle_, L511_UDP_DISCONNECTED);
            }
        } else if (command == "QISEND" && arguments.size() >= 1) {
            if (arguments[0].int_value == udp_id_) {
                xEventGroupSetBits(event_group_handle_, L511_UDP_SEND_COMPLETE);
            }
        } else if (command == "QIURC" && arguments.size() >= 2) {
            if (arguments[0].string_value == "recv" && arguments[1].int_value == udp_id_) {
                // +QIURC: "recv",<connectID>,<length>
                // 数据在下一行，AtUart已经将其作为最后一个参数传递
                // ParseResponse已经trim了空白字符，直接使用
                if (connected_ && message_callback_ && arguments.size() >= 4) {
                    // 直接调用，ParseResponse已经trim了空白字符
                    message_callback_(at_uart_->DecodeHex(arguments[3].string_value));
                }
            } else if (arguments[0].string_value == "closed" && arguments[1].int_value == udp_id_) {
                connected_ = false;
                instance_active_ = false;
                xEventGroupSetBits(event_group_handle_, L511_UDP_DISCONNECTED);
            }
        } else if (command == "FIFO_OVERFLOW") {
            xEventGroupSetBits(event_group_handle_, L511_UDP_ERROR);
            Disconnect();
        }
    });
}

L511Udp::~L511Udp() {
    Disconnect();
    at_uart_->UnregisterUrcCallback(urc_callback_it_);
    if (event_group_handle_) {
        vEventGroupDelete(event_group_handle_);
    }
}

bool L511Udp::Connect(const std::string& host, int port) {
    // Clear bits
    xEventGroupClearBits(event_group_handle_, L511_UDP_CONNECTED | L511_UDP_DISCONNECTED | L511_UDP_ERROR);

    // 关闭旧连接（如果存在）
    if (instance_active_) {
        std::string command = "AT+QICLOSE=" + std::to_string(udp_id_);
        if (at_uart_->SendCommand(command)) {
            // 等待断开完成
            xEventGroupWaitBits(event_group_handle_, L511_UDP_DISCONNECTED, pdTRUE, pdFALSE, pdMS_TO_TICKS(2000));
        }
        instance_active_ = false;
        connected_ = false;
    }

    // 设置数据格式为HEX (发送和接收都是HEX格式)
    // AT+QICFG="dataformat",1,1
    std::string command = "AT+QICFG=\"dataformat\",1,1";
    if (!at_uart_->SendCommand(command)) {
        ESP_LOGW(TAG, "Failed to set data format, continuing anyway");
    }

    // 打开UDP连接
    // AT+QIOPEN=1,<connectID>,"udp","<IP>",<port>,0,1
    // contextID=1, connectID=udp_id_, local_port=0(自动), access_mode=1(normal mode)
    command = "AT+QIOPEN=1," + std::to_string(udp_id_) + ",\"udp\",\"" + host + "\"," + std::to_string(port) + ",0,1";
    if (!at_uart_->SendCommand(command, 150000)) {  // 最大响应时间150秒
        ESP_LOGE(TAG, "Failed to open UDP connection");
        return false;
    }

    // 等待连接完成
    auto bits = xEventGroupWaitBits(event_group_handle_, L511_UDP_CONNECTED | L511_UDP_ERROR, pdTRUE, pdFALSE, UDP_CONNECT_TIMEOUT_MS / portTICK_PERIOD_MS);
    if (bits & L511_UDP_ERROR) {
        ESP_LOGE(TAG, "Failed to connect to %s:%d", host.c_str(), port);
        return false;
    }
    if (!(bits & L511_UDP_CONNECTED)) {
        ESP_LOGE(TAG, "UDP connection timeout");
        return false;
    }
    return true;
}


void L511Udp::Disconnect() {
    if (!instance_active_) {
        return;
    }

    at_uart_->SendCommand("AT+QICLOSE=" + std::to_string(udp_id_));
    connected_ = false;
    instance_active_ = false;
}

int L511Udp::Send(const std::string& data) {
    const size_t MAX_PACKET_SIZE = 256;  // QISENDEX最大512字节HEX字符串(256字节原始数据)

    if (!connected_) {
        ESP_LOGE(TAG, "Not connected");
        return -1;
    }

    // 转换为HEX字符串，最大256字节原始数据 = 512字节HEX
    if (data.size() > MAX_PACKET_SIZE) {
        ESP_LOGE(TAG, "Data chunk exceeds maximum limit (%d bytes)", MAX_PACKET_SIZE);
        return -1;
    }

    // 使用AT+QISENDEX直接发送HEX数据
    // AT+QISENDEX=<connectID>,<hex_string>
    std::string hex_data = at_uart_->EncodeHex(data);
    std::string command = "AT+QISENDEX=" + std::to_string(udp_id_) + ",\"" + hex_data + "\"";
    
    // SendCommand会等待OK或SEND OK，如果返回true说明发送成功
    if (!at_uart_->SendCommand(command, 30000)) {  // 最大响应时间30秒
        ESP_LOGE(TAG, "Failed to send data chunk");
        return -1;
    }

    // SendCommand已经成功返回，说明收到了OK或SEND OK
    // 不需要再次检查响应，直接返回成功
    return data.size();
}
