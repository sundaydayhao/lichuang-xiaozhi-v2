#ifndef L511_HTTP_H
#define L511_HTTP_H

#include "at_uart.h"
#include "http.h"
#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>

#include <map>
#include <string>
#include <mutex>
#include <condition_variable>
#include <optional>

#define L511_HTTP_EVENT_URL_SENT (1 << 0)
#define L511_HTTP_EVENT_POST_SENT (1 << 1)
#define L511_HTTP_EVENT_RESPONSE_RECEIVED (1 << 2)
#define L511_HTTP_EVENT_ERROR (1 << 3)
#define L511_HTTP_EVENT_READY (1 << 4)

class L511Http : public Http {
public:
    L511Http(std::shared_ptr<AtUart> at_uart);
    ~L511Http();

    void SetTimeout(int timeout_ms) override;
    void SetHeader(const std::string& key, const std::string& value) override;
    void SetContent(std::string&& content) override;
    bool Open(const std::string& method, const std::string& url) override;
    void Close() override;
    int Read(char* buffer, size_t buffer_size) override;
    int Write(const char* buffer, size_t buffer_size) override;

    int GetStatusCode() override;
    std::string GetResponseHeader(const std::string& key) const override;
    size_t GetBodyLength() override;
    std::string ReadAll() override;

private:
    std::shared_ptr<AtUart> at_uart_;
    EventGroupHandle_t event_group_handle_;
    std::mutex mutex_;
    std::condition_variable cv_;

    int timeout_ms_ = 30000;
    std::map<std::string, std::string> headers_;
    std::optional<std::string> content_ = std::nullopt;
    std::string method_;
    std::string url_;
    std::string protocol_;
    std::string host_;
    std::string path_;
    int port_ = 80;
    
    int status_code_ = -1;
    size_t content_length_ = 0;
    std::string body_;
    size_t body_offset_ = 0;
    bool eof_ = false;
    bool instance_active_ = false;
    bool url_sent_ = false;
    bool post_sent_ = false;
    
    std::list<UrcCallback>::iterator urc_callback_it_;

    bool ParseUrl(const std::string& url);
    std::string BuildHttpRequest();
    void HandleHttpPostUrc(const std::vector<AtArgumentValue>& arguments);
};

#endif // L511_HTTP_H
