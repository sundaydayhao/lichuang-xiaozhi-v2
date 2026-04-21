#ifndef ESP32_CAMERA_H
#define ESP32_CAMERA_H

#include <esp_camera.h>
#include <lvgl.h>
#include <thread>
#include <memory>

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

#include "camera.h"

struct JpegChunk {
    uint8_t* data;
    size_t len;
};

class Esp32Camera : public Camera {
private:
    camera_fb_t* fb_ = nullptr;
    std::string explain_url_;
    std::string explain_token_;
    std::thread encoder_thread_;
    SemaphoreHandle_t capture_mutex_ = nullptr;  // 添加互斥锁，防止并发调用Capture
    bool initialized_ = false;

    void SavePhotoToStorage();  // 保存照片到 storage 分区

public:
// 锁管理类，用于在拍照上传期间保持锁
    class CaptureLock {
    private:
        SemaphoreHandle_t* mutex_;
        bool locked_;
    public:
        CaptureLock(SemaphoreHandle_t* mutex) : mutex_(mutex), locked_(false) {
            if (mutex_ != nullptr) {
                if (xSemaphoreTake(*mutex_, pdMS_TO_TICKS(1000)) == pdTRUE) {
                    locked_ = true;
                }
            }
        }
        ~CaptureLock() {
            Release();
        }
        void Release() {
            if (locked_ && mutex_ != nullptr) {
                xSemaphoreGive(*mutex_);
                locked_ = false;
            }
        }
        bool IsLocked() const { return locked_; }
    };

    Esp32Camera(const camera_config_t& config);
    ~Esp32Camera();

    bool IsReady() const { return initialized_; }
    virtual void SetExplainUrl(const std::string& url, const std::string& token);
    virtual bool Capture();
    // 拍照并返回锁对象，用于在上传期间保持锁
    std::unique_ptr<CaptureLock> CaptureForExplain();
    // 翻转控制函数
    virtual bool SetHMirror(bool enabled) override;
    virtual bool SetVFlip(bool enabled) override;
    virtual std::string Explain(const std::string& question);
};

#endif // ESP32_CAMERA_H
