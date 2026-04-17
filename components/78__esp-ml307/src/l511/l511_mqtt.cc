#include "l511_mqtt.h"
#include <esp_log.h>
#include <sstream>

static const char *TAG = "L511Mqtt";

L511Mqtt::L511Mqtt(std::shared_ptr<AtUart> at_uart, int mqtt_id) : at_uart_(at_uart), mqtt_id_(mqtt_id) {
    event_group_handle_ = xEventGroupCreate();

    urc_callback_it_ = at_uart_->RegisterUrcCallback([this](const std::string& command, const std::vector<AtArgumentValue>& arguments) {
        if (command == "MCONNECT") {
            // +MCONNECT: <result>
            if (arguments.size() >= 1) {
                std::string result = arguments[0].string_value;
                if (result == "SUCCESS") {
                            connected_ = true;
                            if (on_connected_callback_) {
                                on_connected_callback_();
                        }
                        xEventGroupSetBits(event_group_handle_, MQTT_CONNECTED_EVENT);
                } else if (result == "FAILURE") {
                            connected_ = false;
                            if (on_disconnected_callback_) {
                                on_disconnected_callback_();
                            }
                        xEventGroupSetBits(event_group_handle_, MQTT_DISCONNECTED_EVENT);
                    if (on_error_callback_) {
                        on_error_callback_("MQTT connection failed");
                    }
                }
            }
        } else if (command == "MIPSTART") {
            // +MIPSTART: <result>
            if (arguments.size() >= 1) {
                std::string result = arguments[0].string_value;
                if (result == "SUCCESS") {
                    xEventGroupSetBits(event_group_handle_, MQTT_INITIALIZED_EVENT);
                } else if (result == "FAILURE") {
                    xEventGroupClearBits(event_group_handle_, MQTT_INITIALIZED_EVENT);
                }
            }
        } else if (command == "MSUB") {
            // +MSUB: <result> 或 +MSUB: <topic>,<len> bytes,<message>
            // 注意：message可能包含逗号，所以arguments可能被错误分割
            if (arguments.size() >= 1) {
                std::string first_arg = arguments[0].string_value;
                if (first_arg == "SUCCESS" || first_arg == "FAILURE") {
                    // 这是订阅结果
                    ESP_LOGI(TAG, "Subscribe result: %s", first_arg.c_str());
                } else if (arguments.size() >= 3) {
                    // 这是收到的订阅消息: +MSUB: <topic>,<len> bytes,<message>
                    std::string topic = first_arg;
                    std::string len_str = arguments[1].string_value;
                    std::string message = arguments[2].string_value;
                    
                    ESP_LOGI(TAG, "MSUB raw: topic=%s, len_str=%s, message_raw_len=%d, message_raw=%.200s, args_count=%d", 
                             topic.c_str(), len_str.c_str(), (int)message.length(), message.c_str(), (int)arguments.size());
                    
                    // 打印所有参数用于调试
                    for (size_t i = 0; i < arguments.size(); i++) {
                        ESP_LOGI(TAG, "MSUB arg[%d]: len=%d, value=%.100s", (int)i, (int)arguments[i].string_value.length(), arguments[i].string_value.c_str());
                    }
                    
                    // 移除 " bytes" 后缀
                    size_t bytes_pos = len_str.find(" bytes");
                    if (bytes_pos != std::string::npos) {
                        len_str = len_str.substr(0, bytes_pos);
                    }
                    
                    // 如果message被截断（因为JSON中包含逗号），尝试合并后续参数
                    // ParseResponse现在应该能正确处理引号内的逗号，但如果还有问题，这里作为备用
                    if (arguments.size() > 3) {
                        ESP_LOGD(TAG, "MSUB has more than 3 arguments, merging...");
                        // 检查message是否以引号开始但没有以引号结束（说明被截断了）
                        if (!message.empty() && message.front() == '"' && message.back() != '"') {
                            // 合并后续参数直到找到结束引号
                            for (size_t i = 3; i < arguments.size(); i++) {
                                message += "," + arguments[i].string_value;
                                ESP_LOGD(TAG, "MSUB merged arg[%zu]: %.100s", i, arguments[i].string_value.c_str());
                                // 如果这个参数以引号结束，说明message完整了
                                if (!arguments[i].string_value.empty() && arguments[i].string_value.back() == '"') {
                                    break;
                                }
                            }
                        }
                    }
                    
                    // 移除 message 的引号（如果存在）
                    // message 可能是 "{"type":"hello",...}" 格式，需要移除外层引号
                    if (!message.empty() && message.front() == '"' && message.back() == '"') {
                        message = message.substr(1, message.length() - 2);
                        ESP_LOGD(TAG, "MSUB removed quotes from message");
                    }
                    
                    ESP_LOGI(TAG, "Received MQTT message: topic=%s, len=%s, message_len=%d", 
                             topic.c_str(), len_str.c_str(), (int)message.length());
                    ESP_LOGI(TAG, "MQTT message content: %.500s", message.c_str());
                    
                        if (on_message_callback_) {
                        on_message_callback_(topic, message);
                    }
                }
            }
        } else if (command == "MPUB") {
            // +MPUB: <result>
            if (arguments.size() >= 1) {
                std::string result = arguments[0].string_value;
                ESP_LOGI(TAG, "Publish result: %s", result.c_str());
                        }
        } else if (command == "MPUBEX") {
            // +MPUBEX: <result>
            if (arguments.size() >= 1) {
                std::string result = arguments[0].string_value;
                ESP_LOGI(TAG, "PublishEX result: %s", result.c_str());
            }
        } else if (command == "MUNSUB") {
            // +MUNSUB: <result>
            if (arguments.size() >= 1) {
                std::string result = arguments[0].string_value;
                ESP_LOGI(TAG, "Unsubscribe result: %s", result.c_str());
            }
        } else if (command == "MDISCONNECT") {
            // +MDISCONNECT: <result>
            if (arguments.size() >= 1) {
                std::string result = arguments[0].string_value;
                if (result == "SUCCESS" || result == "Already disconnect") {
                    connected_ = false;
                    if (on_disconnected_callback_) {
                        on_disconnected_callback_();
                    }
                    xEventGroupSetBits(event_group_handle_, MQTT_DISCONNECTED_EVENT);
                        }
                    }
        } else if (command == "MIPCLOSE") {
            // +MIPCLOSE: <result>
            if (arguments.size() >= 1) {
                std::string result = arguments[0].string_value;
                if (result == "SUCCESS") {
                    connected_ = false;
                    xEventGroupSetBits(event_group_handle_, MQTT_DISCONNECTED_EVENT);
                }
            }
        } else if (command == "MQTTSTATU") {
            // +MQTTSTATU: <statu>
            if (arguments.size() >= 1) {
                int status = arguments[0].int_value;
                connected_ = (status == 1);
            xEventGroupSetBits(event_group_handle_, MQTT_INITIALIZED_EVENT);
            }
        } else if (command == "MCONFIG") {
            // +MCONFIG: <clientid>[,<username>,<password>...]
            // 这是查询命令的响应，保存配置信息
            if (arguments.size() >= 1) {
                mconfig_client_id_ = arguments[0].string_value;
                mconfig_has_config_ = !mconfig_client_id_.empty();
                ESP_LOGI(TAG, "MCONFIG query result: client_id=%s, has_config=%d", 
                         mconfig_client_id_.c_str(), mconfig_has_config_);
            }
            xEventGroupSetBits(event_group_handle_, MQTT_MCONFIG_QUERIED_EVENT);
        }
    });
}

L511Mqtt::~L511Mqtt() {
    Disconnect();
    at_uart_->UnregisterUrcCallback(urc_callback_it_);
    if (event_group_handle_) {
    vEventGroupDelete(event_group_handle_);
}
}

bool L511Mqtt::Connect(const std::string broker_address, int broker_port, const std::string client_id, const std::string username, const std::string password) {
    // 根据FAE文档，L511的MQTT连接流程：
    // 1. AT+MCONFIG=<clientid>[,<username>,<password>]  (只能设置一次，重复会报错)
    // 2. AT+MQTTSSL=<action>[,cert] (如果需要SSL)
    // 3. AT+MIPSTART=<hostname>,<port>[,<version>]
    // 4. AT+MCONNECT=<clean_session>,<keepalive>
    
    std::string command;
    
    // 步骤1: 先查询是否已经设置过 MCONFIG
    // AT+MCONFIG? 返回: +MCONFIG:<clientid>[,<username>,<password>...]
    ESP_LOGI(TAG, "Querying MQTT config status...");
    mconfig_has_config_ = false;
    mconfig_client_id_.clear();
    xEventGroupClearBits(event_group_handle_, MQTT_MCONFIG_QUERIED_EVENT);
    
    bool mconfig_already_set = false;
    if (at_uart_->SendCommand("AT+MCONFIG?", 1000)) {
        // 等待查询响应（URC回调会设置 mconfig_has_config_）
        auto bits = xEventGroupWaitBits(event_group_handle_, MQTT_MCONFIG_QUERIED_EVENT, pdTRUE, pdFALSE, pdMS_TO_TICKS(1000));
        if (bits & MQTT_MCONFIG_QUERIED_EVENT && mconfig_has_config_ && !mconfig_client_id_.empty()) {
            mconfig_already_set = true;
            ESP_LOGI(TAG, "MCONFIG already set (client_id=%s), skipping configuration", mconfig_client_id_.c_str());
        } else {
            ESP_LOGI(TAG, "MCONFIG not set yet, will configure");
        }
    } else {
        ESP_LOGW(TAG, "Failed to query MCONFIG, will try to set anyway");
    }
    
    // 步骤1: 配置MQTT参数（如果还没有设置过）
    if (!mconfig_already_set) {
        ESP_LOGI(TAG, "Setting MQTT config (first time)");
        command = "AT+MCONFIG=\"" + client_id + "\"";
        // 只有当username和password都不为空时才添加
        if (!username.empty() && !password.empty()) {
            command += ",\"" + username + "\",\"" + password + "\"";
        }
        
        ESP_LOGI(TAG, "Sending AT+MCONFIG command (length: %zu)", command.length());
        if (!at_uart_->SendCommand(command, 5000)) {
            ESP_LOGE(TAG, "Failed to configure MQTT parameters (command returned ERROR)");
            return false;
        }
        ESP_LOGI(TAG, "MQTT config set successfully");
    }

    // 步骤2: 配置SSL（如果需要）
    if (broker_port == 8883) {
        // AT+MQTTSSL=1,0 (不使用证书) 或 AT+MQTTSSL=1,1 (使用证书)
        // 默认使用0（忽略证书）
        command = "AT+MQTTSSL=1,0";
        if (!at_uart_->SendCommand(command, 5000)) {
            ESP_LOGE(TAG, "Failed to configure MQTT SSL");
            return false;
        }
    }

    // 步骤3: 配置服务器地址、端口和协议版本
    // AT+MIPSTART=<hostname>,<port>[,<version>]
    // version: 3=MQTT 3.1, 4=MQTT 3.1.1 (默认3)
    int version = 4; // 使用MQTT 3.1.1
    command = "AT+MIPSTART=\"" + broker_address + "\"," + std::to_string(broker_port) + "," + std::to_string(version);
    
    xEventGroupClearBits(event_group_handle_, MQTT_INITIALIZED_EVENT);
    if (!at_uart_->SendCommand(command, 5000)) {
        ESP_LOGE(TAG, "Failed to configure MQTT server address");
        return false;
    }

    // 等待+MIPSTART: SUCCESS
    auto bits = xEventGroupWaitBits(event_group_handle_, MQTT_INITIALIZED_EVENT, pdTRUE, pdFALSE, pdMS_TO_TICKS(5000));
    if (!(bits & MQTT_INITIALIZED_EVENT)) {
        ESP_LOGE(TAG, "Failed to configure MQTT server (timeout)");
        return false;
    }

    // 步骤4: 请求连接MQTT服务器
    // AT+MCONNECT=<clean_session>,<keepalive>
    // clean_session: 0=保留会话, 1=清除会话
    // keepalive: 30-1800秒，默认60
    int clean_session = 1;
    int keepalive = keep_alive_seconds_;
    if (keepalive < 30) keepalive = 30;
    if (keepalive > 1800) keepalive = 1800;
    
    command = "AT+MCONNECT=" + std::to_string(clean_session) + "," + std::to_string(keepalive);

    xEventGroupClearBits(event_group_handle_, MQTT_CONNECTED_EVENT | MQTT_DISCONNECTED_EVENT);
    if (!at_uart_->SendCommand(command, 10000)) {
        ESP_LOGE(TAG, "Failed to send MQTT connect command");
        return false;
    }

    // 等待+MCONNECT: SUCCESS
    bits = xEventGroupWaitBits(event_group_handle_, MQTT_CONNECTED_EVENT | MQTT_DISCONNECTED_EVENT, pdTRUE, pdFALSE, pdMS_TO_TICKS(MQTT_CONNECT_TIMEOUT_MS));
    if (!(bits & MQTT_CONNECTED_EVENT)) {
        ESP_LOGE(TAG, "Failed to connect to MQTT broker");
        return false;
    }
    
    return true;
}

bool L511Mqtt::IsConnected() {
    // 使用AT+MQTTSTATU查询连接状态
    xEventGroupClearBits(event_group_handle_, MQTT_INITIALIZED_EVENT);
    
    if (!at_uart_->SendCommand("AT+MQTTSTATU", 1000)) {
        ESP_LOGW(TAG, "Failed to query MQTT status");
        return connected_; // 返回缓存的连接状态
    }
    
    // 等待+MQTTSTATU响应
    auto bits = xEventGroupWaitBits(event_group_handle_, MQTT_INITIALIZED_EVENT, pdTRUE, pdFALSE, pdMS_TO_TICKS(1000));
    if (bits & MQTT_INITIALIZED_EVENT) {
        // URC回调会更新connected_状态
        return connected_;
    }
    
    return connected_;
}

void L511Mqtt::Disconnect() {
    if (!connected_) {
        return;
    }
    
    // 步骤1: 断开MQTT连接 AT+MDISCONNECT
    xEventGroupClearBits(event_group_handle_, MQTT_DISCONNECTED_EVENT);
    at_uart_->SendCommand("AT+MDISCONNECT", 5000);
    
    // 等待断开完成
    auto bits = xEventGroupWaitBits(event_group_handle_, MQTT_DISCONNECTED_EVENT, pdTRUE, pdFALSE, pdMS_TO_TICKS(5000));
    
    // 步骤2: 关闭MQTT客户端会话 AT+MIPCLOSE
    at_uart_->SendCommand("AT+MIPCLOSE", 5000);
    
    connected_ = false;
}

bool L511Mqtt::Publish(const std::string topic, const std::string payload, int qos) {
    if (!connected_) {
        ESP_LOGE(TAG, "MQTT not connected");
        return false;
    }
    
    // 检查payload是否包含特殊字符（引号、逗号等）
    bool has_special_chars = (payload.find('"') != std::string::npos || 
                              payload.find(',') != std::string::npos ||
                              payload.length() > 512);
    
    if (!has_special_chars && payload.length() <= 512) {
        // 使用AT+MPUB（简单消息，无特殊字符）
        // AT+MPUB=<topic>,<qos>,<retain>,<message>
        std::string command = "AT+MPUB=\"" + topic + "\"," + std::to_string(qos) + ",0,\"" + payload + "\"";
        return at_uart_->SendCommand(command, 5000);
    } else {
        // 使用AT+MPUBEX（包含特殊字符或长度超过512字节）
        // AT+MPUBEX=<topic>,<qos>,<retain>,<msgLen>
        // 然后发送数据
        std::string command = "AT+MPUBEX=\"" + topic + "\"," + std::to_string(qos) + ",0," + std::to_string(payload.length());
        return at_uart_->SendCommandWithData(command, 10000, true, payload.data(), payload.length());
    }
}

bool L511Mqtt::Subscribe(const std::string topic, int qos) {
    if (!connected_) {
        ESP_LOGE(TAG, "MQTT not connected");
        return false;
    }
    
    // AT+MSUB=<topic>,<qos>
    std::string command = "AT+MSUB=\"" + topic + "\"," + std::to_string(qos);
    return at_uart_->SendCommand(command, 5000);
}

bool L511Mqtt::Unsubscribe(const std::string topic) {
    if (!connected_) {
        ESP_LOGE(TAG, "MQTT not connected");
        return false;
    }
    
    // AT+MUNSUB=<topic>
    std::string command = "AT+MUNSUB=\"" + topic + "\"";
    return at_uart_->SendCommand(command, 5000);
}

std::string L511Mqtt::ErrorToString(int error_code) {
    // L511使用不同的错误码，这里保留原有实现
    switch (error_code) {
        case 0:
            return "Connected";
        case 1:
            return "Reconnecting";
        case 2:
            return "Disconnected: User initiated";
        case 3:
            return "Disconnected: Rejected (protocol version, identifier, username or password error)";
        case 4:
            return "Disconnected: Server disconnected";
        case 5:
            return "Disconnected: Ping timeout";
        case 6:
            return "Disconnected: Network error";
        case 255:
            return "Disconnected: Unknown error";
        default:
            return "Unknown error";
    }
}
