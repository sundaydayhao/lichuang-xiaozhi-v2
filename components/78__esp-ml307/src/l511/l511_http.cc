#include "l511_http.h"
#include <esp_log.h>
#include <cstring>
#include <sstream>
#include <algorithm>
#include <chrono>

static const char *TAG = "L511Http";

L511Http::L511Http(std::shared_ptr<AtUart> at_uart) : at_uart_(at_uart) {
    event_group_handle_ = xEventGroupCreate();

    urc_callback_it_ = at_uart_->RegisterUrcCallback([this](const std::string& command, const std::vector<AtArgumentValue>& arguments) {
        if (command == "HTTPPOST") {
            HandleHttpPostUrc(arguments);
        } else if (command == "FIFO_OVERFLOW") {
            xEventGroupSetBits(event_group_handle_, L511_HTTP_EVENT_ERROR);
            Close();
        }
    });
}

L511Http::~L511Http() {
    Close();
    if (event_group_handle_) {
        vEventGroupDelete(event_group_handle_);
    }
    at_uart_->UnregisterUrcCallback(urc_callback_it_);
}

void L511Http::SetTimeout(int timeout_ms) {
    timeout_ms_ = timeout_ms;
}

void L511Http::SetHeader(const std::string& key, const std::string& value) {
    headers_[key] = value;
    }
    
void L511Http::SetContent(std::string&& content) {
    content_ = std::move(content);
}

bool L511Http::ParseUrl(const std::string& url) {
    // 解析 URL: protocol://host:port/path
    size_t protocol_end = url.find("://");
    if (protocol_end == std::string::npos) {
        ESP_LOGE(TAG, "Invalid URL format: %s", url.c_str());
        return false;
    }
    
    protocol_ = url.substr(0, protocol_end);
    std::transform(protocol_.begin(), protocol_.end(), protocol_.begin(), ::tolower);

    size_t host_start = protocol_end + 3;
    size_t path_start = url.find("/", host_start);
    size_t port_start = url.find(":", host_start);

    // 默认端口
    if (protocol_ == "https") {
        port_ = 443;
    } else {
        port_ = 80;
    }

    if (path_start == std::string::npos) {
        path_ = "/";
        if (port_start != std::string::npos) {
            host_ = url.substr(host_start, port_start - host_start);
            std::string port_str = url.substr(port_start + 1);
            char* endptr;
            long port = strtol(port_str.c_str(), &endptr, 10);
            if (endptr != port_str.c_str() && *endptr == '\0' && port > 0 && port <= 65535) {
                port_ = static_cast<int>(port);
            } else {
                ESP_LOGE(TAG, "Invalid port: %s", port_str.c_str());
                return false;
            }
        } else {
            host_ = url.substr(host_start);
        }
    } else {
        path_ = url.substr(path_start);
        if (port_start != std::string::npos && port_start < path_start) {
            host_ = url.substr(host_start, port_start - host_start);
            std::string port_str = url.substr(port_start + 1, path_start - port_start - 1);
            char* endptr;
            long port = strtol(port_str.c_str(), &endptr, 10);
            if (endptr != port_str.c_str() && *endptr == '\0' && port > 0 && port <= 65535) {
                port_ = static_cast<int>(port);
            } else {
                ESP_LOGE(TAG, "Invalid port: %s", port_str.c_str());
                return false;
}
        } else {
            host_ = url.substr(host_start, path_start - host_start);
    }
    }

    ESP_LOGD(TAG, "Parsed URL: protocol=%s, host=%s, port=%d, path=%s",
             protocol_.c_str(), host_.c_str(), port_, path_.c_str());
    return true;
}

std::string L511Http::BuildHttpRequest() {
    std::ostringstream request;
    
    // 请求行
    request << method_ << " " << path_ << " HTTP/1.1\r\n";
    
    // Host头（必需）
    request << "Host: " << host_ << "\r\n";
    
    // 添加其他请求头
    for (const auto& [key, value] : headers_) {
        request << key << ": " << value << "\r\n";
}

    // Content-Length（如果有内容）
    if (content_.has_value()) {
        request << "Content-Length: " << content_->length() << "\r\n";
}

    // 结束请求头
    request << "\r\n";
    
    // 添加请求体（如果有）
    if (content_.has_value()) {
        request << *content_;
    }
    
    return request.str();
}

void L511Http::HandleHttpPostUrc(const std::vector<AtArgumentValue>& arguments) {
    // +HTTPPOST: <percent>,<status_code>,<data_len>
    if (arguments.size() >= 3) {
        int percent = arguments[0].int_value;
        status_code_ = arguments[1].int_value;
        content_length_ = arguments[2].int_value;
        
        ESP_LOGI(TAG, "HTTP Response URC: percent=%d, status=%d, length=%zu", 
                 percent, status_code_, content_length_);
        
        if (percent == 0) {
            // 开始接收响应
            std::lock_guard<std::mutex> lock(mutex_);
            body_.clear();
            body_offset_ = 0;
            eof_ = false;
        } else if (percent == 100) {
            // 响应完成
            // 从UART的response_中读取响应数据
            // 响应数据在+HTTPPOST: 0和+HTTPPOST: 100之间通过UART接收
            // 这些数据会被ParseResponse处理，放入response_中
            std::string response_data = at_uart_->GetResponse();
            
            std::lock_guard<std::mutex> lock(mutex_);

            // 如果response_中包含响应数据（不是URC），添加到body_
            if (!response_data.empty() && response_data.find("+HTTPPOST:") == std::string::npos && 
                response_data.find("AT+HTTPREAD") == std::string::npos &&
                response_data.find("OK") == std::string::npos &&
                response_data.find("CONNECT") == std::string::npos) {
                // 这是响应数据
                body_ = response_data;
                ESP_LOGD(TAG, "Received HTTP response body: %zu bytes", body_.length());
            }
            
            eof_ = true;
            xEventGroupSetBits(event_group_handle_, L511_HTTP_EVENT_RESPONSE_RECEIVED);
            cv_.notify_one();
        }
    }
}

bool L511Http::Open(const std::string& method, const std::string& url) {
    method_ = method;
    url_ = url;
    
    // 重置状态
    status_code_ = -1;
    content_length_ = 0;
    body_.clear();
    body_offset_ = 0;
    eof_ = false;
    url_sent_ = false;
    post_sent_ = false;
    instance_active_ = true;
    
    xEventGroupClearBits(event_group_handle_, 
                         L511_HTTP_EVENT_URL_SENT |
                         L511_HTTP_EVENT_POST_SENT |
                         L511_HTTP_EVENT_RESPONSE_RECEIVED |
                         L511_HTTP_EVENT_ERROR);
    
    // 解析URL
    if (!ParseUrl(url)) {
        return false;
    }

    // 配置SSL（如果是HTTPS）
    if (protocol_ == "https") {
        ESP_LOGI(TAG, "Configuring SSL for HTTPS");
        
        // AT+SSLCFG="ciphersuite",0,0xffff
        std::string command = "AT+SSLCFG=\"ciphersuite\",0,0xffff";
        if (!at_uart_->SendCommand(command, timeout_ms_)) {
            ESP_LOGE(TAG, "Failed to configure SSL ciphersuite");
        return false;
    }

        // AT+SSLCFG="sslversion",0,4
        command = "AT+SSLCFG=\"sslversion\",0,4";
        if (!at_uart_->SendCommand(command, timeout_ms_)) {
            ESP_LOGE(TAG, "Failed to configure SSL version");
        return false;
    }
        
        // AT+SSLCFG="seclevel",0,0
        command = "AT+SSLCFG=\"seclevel\",0,0";
        if (!at_uart_->SendCommand(command, timeout_ms_)) {
            ESP_LOGE(TAG, "Failed to configure SSL security level");
            return false;
        }
        
        // AT+HTTPCFG="sslctxid",0
        command = "AT+HTTPCFG=\"sslctxid\",0";
        if (!at_uart_->SendCommand(command, timeout_ms_)) {
            ESP_LOGE(TAG, "Failed to configure HTTP SSL context");
            return false;
        }
    }

    // AT+HTTPCFG="requestheader",1
    std::string command = "AT+HTTPCFG=\"requestheader\",1";
    if (!at_uart_->SendCommand(command, timeout_ms_)) {
        ESP_LOGE(TAG, "Failed to configure HTTP request header");
        return false;
    }
    
    // AT+HTTPURL=<url_len>,<timeout>
    // 发送URL长度和超时时间（秒）
    int url_len = url.length();
    int timeout_sec = timeout_ms_ / 1000;
    if (timeout_sec < 1) timeout_sec = 1;
    if (timeout_sec > 60) timeout_sec = 60;

    // 使用SendCommandWithData一次性发送AT+HTTPURL命令和URL数据
    // SendCommandWithData会在收到CONNECT后自动发送数据
    command = "AT+HTTPURL=" + std::to_string(url_len) + "," + std::to_string(timeout_sec);
    if (!at_uart_->SendCommandWithData(command, timeout_ms_, true, url.data(), url.length())) {
        ESP_LOGE(TAG, "Failed to send HTTP URL and data");
        return false;
    }
    
    url_sent_ = true;
    xEventGroupSetBits(event_group_handle_, L511_HTTP_EVENT_URL_SENT);
    
    // 等待URL发送完成
    vTaskDelay(pdMS_TO_TICKS(500));
    
    // 构建HTTP请求
    std::string http_request = BuildHttpRequest();
    int content_len = http_request.length();
    
    ESP_LOGD(TAG, "HTTP Request length: %d", content_len);
    ESP_LOGD(TAG, "HTTP Request:\n%.*s", std::min(512, content_len), http_request.c_str());
    
    // 使用SendCommandWithData一次性发送AT+HTTPPOST命令和HTTP请求体
    // SendCommandWithData会在收到CONNECT后自动发送数据
    command = "AT+HTTPPOST=" + std::to_string(content_len) + "," + std::to_string(timeout_sec);
    if (!at_uart_->SendCommandWithData(command, timeout_ms_, true, http_request.data(), http_request.length())) {
        ESP_LOGE(TAG, "Failed to send HTTP POST and request body");
        return false;
    }

    post_sent_ = true;
    xEventGroupSetBits(event_group_handle_, L511_HTTP_EVENT_POST_SENT);
    
    // 根据demo：AT+HTTPPOST后收到OK就完成，不需要等待URC
    // 然后直接发送AT+HTTPREAD
    // AT+HTTPREAD → OK → CONNECT → +HTTPPOST: 0,200,464 → 响应数据 → +HTTPPOST: 100,200,464
    
    // 清除响应接收事件，准备等待AT+HTTPREAD后的URC
    xEventGroupClearBits(event_group_handle_, L511_HTTP_EVENT_RESPONSE_RECEIVED);
    
    // 发送AT+HTTPREAD命令
    command = "AT+HTTPREAD";
    if (!at_uart_->SendCommand(command, timeout_ms_)) {
        ESP_LOGE(TAG, "Failed to read HTTP response");
        return false;
    }
    
    // 等待响应URC（+HTTPPOST: ...）
    // 根据demo：AT+HTTPREAD会返回OK，然后CONNECT，然后+HTTPPOST: 0,200,464，然后数据，然后+HTTPPOST: 100,200,464
    auto bits = xEventGroupWaitBits(event_group_handle_, 
                                    L511_HTTP_EVENT_RESPONSE_RECEIVED | L511_HTTP_EVENT_ERROR,
                                    pdTRUE, pdFALSE, 
                                    pdMS_TO_TICKS(timeout_ms_));
    
    if (bits & L511_HTTP_EVENT_ERROR) {
        ESP_LOGE(TAG, "HTTP request failed");
        return false;
    }

    if (!(bits & L511_HTTP_EVENT_RESPONSE_RECEIVED)) {
        ESP_LOGE(TAG, "Timeout waiting for HTTP response URC");
        return false;
    }
    
    // 等待一小段时间，确保响应数据已经被处理
    vTaskDelay(pdMS_TO_TICKS(100));
    
    // 再次尝试从UART的response_中读取响应数据（如果HandleHttpPostUrc中没有读取到）
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (body_.empty()) {
            std::string response_data = at_uart_->GetResponse();
            if (!response_data.empty() && response_data.find("+HTTPPOST:") == std::string::npos && 
                response_data.find("AT+HTTPREAD") == std::string::npos &&
                response_data.find("OK") == std::string::npos &&
                response_data.find("CONNECT") == std::string::npos) {
                body_ = response_data;
                ESP_LOGD(TAG, "Received HTTP response body from GetResponse: %zu bytes", body_.length());
        }
    }
    }
    
    return status_code_ > 0;
}

void L511Http::Close() {
    if (!instance_active_) {
        return;
        }
    
    instance_active_ = false;
    at_uart_->SendCommand("AT+HTTPTERM", 5000);
}

int L511Http::Read(char* buffer, size_t buffer_size) {
    std::unique_lock<std::mutex> lock(mutex_);
    
    if (eof_ && body_.empty()) {
        return 0;
    }
    
    // 等待数据
    auto timeout = std::chrono::milliseconds(timeout_ms_);
    bool received = cv_.wait_for(lock, timeout, [this] { 
        return !body_.empty() || eof_; 
    });
    
    if (!received) {
        ESP_LOGE(TAG, "Timeout waiting for HTTP content");
        return -1;
    }

    if (!instance_active_) {
        return -1;
    }
    
    size_t bytes_to_read = std::min(body_.size(), buffer_size);
    if (bytes_to_read > 0) {
        std::memcpy(buffer, body_.data(), bytes_to_read);
        body_.erase(0, bytes_to_read);
    }
    
    return bytes_to_read;
}

int L511Http::Write(const char* buffer, size_t buffer_size) {
    // L511使用AT+HTTP*命令，不支持流式写入
    ESP_LOGW(TAG, "Write not supported for L511 HTTP");
    return -1;
}

int L511Http::GetStatusCode() {
    return status_code_;
}

std::string L511Http::GetResponseHeader(const std::string& key) const {
    // L511的AT+HTTP*命令不返回响应头
    // 如果需要响应头，需要解析HTTP响应
    return "";
}

size_t L511Http::GetBodyLength() {
    return content_length_;
}

std::string L511Http::ReadAll() {
    std::string result;
    char buffer[512];
    
    while (true) {
        int ret = Read(buffer, sizeof(buffer));
        if (ret <= 0) {
            break;
        }
        result.append(buffer, ret);
    }
    
    return result;
}
