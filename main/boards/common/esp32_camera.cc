#include "esp32_camera.h"
#include "mcp_server.h"
#include "display.h"
#include "board.h"
#include "system_info.h"
#include "lvgl_display.h"
#include "jpg/image_to_jpeg.h"
#include <esp_spiffs.h> 
#include <esp_log.h>
#include <esp_heap_caps.h>
#include <esp_camera.h>
#include <img_converters.h>
#include <cstring>
#include <ctime>
#include <cstdio>

#define TAG "Esp32Camera"

Esp32Camera::Esp32Camera(const camera_config_t& config) {
    // 创建互斥锁
    capture_mutex_ = xSemaphoreCreateMutex();
    if (capture_mutex_ == nullptr) {
        ESP_LOGE(TAG, "Failed to create capture mutex");
    }
    // camera init
    esp_err_t err = esp_camera_init(&config); // 配置上面定义的参数
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Camera init failed with error 0x%x", err);
        return;
    }

    sensor_t *s = esp_camera_sensor_get(); // 获取摄像头型号
    if (s->id.PID == GC0308_PID) {
        s->set_hmirror(s, 0);  // 这里控制摄像头镜像 写1镜像 写0不镜像
    }
}

Esp32Camera::~Esp32Camera() {
    if (fb_) {
        esp_camera_fb_return(fb_);
        fb_ = nullptr;
    }
    if (capture_mutex_ != nullptr) {
        vSemaphoreDelete(capture_mutex_);
        capture_mutex_ = nullptr;
    }
    esp_camera_deinit();
}

void Esp32Camera::SetExplainUrl(const std::string& url, const std::string& token) {
    explain_url_ = url;
    explain_token_ = token;
}

bool Esp32Camera::Capture() {
    // 获取互斥锁，防止并发调用
    if (capture_mutex_ != nullptr) {
        if (xSemaphoreTake(capture_mutex_, pdMS_TO_TICKS(1000)) != pdTRUE) {
            ESP_LOGE(TAG, "Failed to acquire capture mutex (timeout)");
            return false;
        }
    }

    // 使用 RAII 方式确保互斥锁被释放
    struct MutexGuard {
        SemaphoreHandle_t* mutex;
        ~MutexGuard() {
            if (*mutex != nullptr) {
                xSemaphoreGive(*mutex);
            }
        }
    } guard{&capture_mutex_};

    if (encoder_thread_.joinable()) {
        encoder_thread_.join();
    }

    auto start_time = esp_timer_get_time();
    int frames_to_get = 1;
    // Try to get a stable frame
    for (int i = 0; i < frames_to_get; i++) {
        if (fb_ != nullptr) {
            esp_camera_fb_return(fb_);
        }
        fb_ = esp_camera_fb_get();
        if (fb_ == nullptr) {
            ESP_LOGE(TAG, "Camera capture failed");
            return false;
        }
    }
    auto end_time = esp_timer_get_time();
    ESP_LOGI(TAG, "Camera captured %d frames in %d ms", frames_to_get, int((end_time - start_time) / 1000));

    // 显示预览图片
    auto display = dynamic_cast<LvglDisplay*>(Board::GetInstance().GetDisplay());
    if (display != nullptr) {
        auto data = (uint8_t*)heap_caps_malloc(fb_->len, MALLOC_CAP_SPIRAM);
        if (data == nullptr) {
            ESP_LOGE(TAG, "Failed to allocate memory for preview image");
            return false;
        }

        auto src = (uint16_t*)fb_->buf;
        auto dst = (uint16_t*)data;
        size_t pixel_count = fb_->len / 2;
        for (size_t i = 0; i < pixel_count; i++) {
            // 交换每个16位字内的字节
            dst[i] = __builtin_bswap16(src[i]);
        }

        auto image = std::make_unique<LvglAllocatedImage>(data, fb_->len, fb_->width, fb_->height, fb_->width * 2, LV_COLOR_FORMAT_RGB565);
        display->SetPreviewImage(std::move(image));
    }
    return true;
}

std::unique_ptr<Esp32Camera::CaptureLock> Esp32Camera::CaptureForExplain() {
    // 先获取锁
    auto lock = std::make_unique<CaptureLock>(&capture_mutex_);
    if (!lock->IsLocked()) {
        ESP_LOGE(TAG, "Failed to acquire capture mutex for explain");
        return nullptr;
    }
    
    if (encoder_thread_.joinable()) {
        encoder_thread_.join();
    }

    auto start_time = esp_timer_get_time();
    int frames_to_get = 2;
    // Try to get a stable frame
    for (int i = 0; i < frames_to_get; i++) {
        if (fb_ != nullptr) {
            esp_camera_fb_return(fb_);
        }
        fb_ = esp_camera_fb_get();
        if (fb_ == nullptr) {
            ESP_LOGE(TAG, "Camera capture failed");
            lock->Release();
            return nullptr;
        }
    }
    auto end_time = esp_timer_get_time();
    ESP_LOGI(TAG, "Camera captured %d frames in %d ms", frames_to_get, int((end_time - start_time) / 1000));

    // 显示预览图片
    auto display = dynamic_cast<LvglDisplay*>(Board::GetInstance().GetDisplay());
    if (display != nullptr) {
        auto data = (uint8_t*)heap_caps_malloc(fb_->len, MALLOC_CAP_SPIRAM);
        if (data == nullptr) {
            ESP_LOGE(TAG, "Failed to allocate memory for preview image");
            lock->Release();
            return nullptr;
        }

        auto src = (uint16_t*)fb_->buf;
        auto dst = (uint16_t*)data;
        size_t pixel_count = fb_->len / 2;
        for (size_t i = 0; i < pixel_count; i++) {
            // 交换每个16位字内的字节
            dst[i] = __builtin_bswap16(src[i]);
        }

        auto image = std::make_unique<LvglAllocatedImage>(data, fb_->len, fb_->width, fb_->height, fb_->width * 2, LV_COLOR_FORMAT_RGB565);
        display->SetPreviewImage(std::move(image));
    }
    
    // 返回锁对象，调用者负责释放
    return lock;
}

bool Esp32Camera::SetHMirror(bool enabled) {
    sensor_t *s = esp_camera_sensor_get();
    if (s == nullptr) {
        ESP_LOGE(TAG, "Failed to get camera sensor");
        return false;
    }
    
    esp_err_t err = s->set_hmirror(s, enabled);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set horizontal mirror: %d", err);
        return false;
    }
    
    ESP_LOGI(TAG, "Camera horizontal mirror set to: %s", enabled ? "enabled" : "disabled");
    return true;
}

bool Esp32Camera::SetVFlip(bool enabled) {
    sensor_t *s = esp_camera_sensor_get();
    if (s == nullptr) {
        ESP_LOGE(TAG, "Failed to get camera sensor");
        return false;
    }
    
    esp_err_t err = s->set_vflip(s, enabled);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set vertical flip: %d", err);
        return false;
    }
    
    ESP_LOGI(TAG, "Camera vertical flip set to: %s", enabled ? "enabled" : "disabled");
    return true;
}

/**
 * @brief 将摄像头捕获的图像发送到远程服务器进行AI分析和解释
 * 
 * 该函数将当前摄像头缓冲区中的图像编码为JPEG格式，并通过HTTP POST请求
 * 以multipart/form-data的形式发送到指定的解释服务器。服务器将根据提供的
 * 问题对图像进行AI分析并返回结果。
 * 
 * 实现特点：
 * - 使用独立线程编码JPEG，与主线程分离
 * - 采用分块传输编码(chunked transfer encoding)优化内存使用
 * - 通过队列机制实现编码线程和发送线程的数据同步
 * - 支持设备ID、客户端ID和认证令牌的HTTP头部配置
 * 
 * @param question 要向AI提出的关于图像的问题，将作为表单字段发送
 * @return std::string 服务器返回的JSON格式响应字符串
 *         成功时包含AI分析结果，失败时包含错误信息
 *         格式示例：{"success": true, "result": "分析结果"}
 *                  {"success": false, "message": "错误信息"}
 * 
 * @note 调用此函数前必须先调用SetExplainUrl()设置服务器URL
 * @note 函数会等待之前的编码线程完成后再开始新的处理
 * @warning 如果摄像头缓冲区为空或网络连接失败，将返回错误信息
 */
std::string Esp32Camera::Explain(const std::string& question) {
    if (explain_url_.empty()) {
        throw std::runtime_error("Image explain URL or token is not set");
    }

    // 创建局部的 JPEG 队列, 40 entries is about to store 512 * 40 = 20480 bytes of JPEG data
    QueueHandle_t jpeg_queue = xQueueCreate(40, sizeof(JpegChunk));
    if (jpeg_queue == nullptr) {
        ESP_LOGE(TAG, "Failed to create JPEG queue");
        throw std::runtime_error("Failed to create JPEG queue");
    }

    // We spawn a thread to encode the image to JPEG using optimized encoder (cost about 500ms and 8KB SRAM)
    encoder_thread_ = std::thread([this, jpeg_queue]() {
        image_to_jpeg_cb(fb_->buf, fb_->len, fb_->width, fb_->height, fb_->format, 80,
            [](void* arg, size_t index, const void* data, size_t len) -> size_t {
            auto jpeg_queue = (QueueHandle_t)arg;
            JpegChunk chunk = {
                .data = (uint8_t*)heap_caps_aligned_alloc(16, len, MALLOC_CAP_SPIRAM),
                .len = len
            };
            memcpy(chunk.data, data, len);
            xQueueSend(jpeg_queue, &chunk, portMAX_DELAY);
            return len;
        }, jpeg_queue);
    });

    auto network = Board::GetInstance().GetNetwork();
    auto http = network->CreateHttp(3);
    // 构造multipart/form-data请求体
    std::string boundary = "----ESP32_CAMERA_BOUNDARY";

    // 配置HTTP客户端，使用分块传输编码
    http->SetHeader("Device-Id", SystemInfo::GetMacAddress().c_str());
    http->SetHeader("Client-Id", Board::GetInstance().GetUuid().c_str());
    if (!explain_token_.empty()) {
        http->SetHeader("Authorization", "Bearer " + explain_token_);
    }
    http->SetHeader("Content-Type", "multipart/form-data; boundary=" + boundary);
    http->SetHeader("Transfer-Encoding", "chunked");
    if (!http->Open("POST", explain_url_)) {
        ESP_LOGE(TAG, "Failed to connect to explain URL");
        // Clear the queue
        encoder_thread_.join();
        JpegChunk chunk;
        while (xQueueReceive(jpeg_queue, &chunk, portMAX_DELAY) == pdPASS) {
            if (chunk.data != nullptr) {
                heap_caps_free(chunk.data);
            } else {
                break;
            }
        }
        vQueueDelete(jpeg_queue);
        throw std::runtime_error("Failed to connect to explain URL");
    }
    
    {
        // 第一块：question字段
        std::string question_field;
        question_field += "--" + boundary + "\r\n";
        question_field += "Content-Disposition: form-data; name=\"question\"\r\n";
        question_field += "\r\n";
        question_field += question + "\r\n";
        http->Write(question_field.c_str(), question_field.size());
    }
    {
        // 第二块：文件字段头部
        std::string file_header;
        file_header += "--" + boundary + "\r\n";
        file_header += "Content-Disposition: form-data; name=\"file\"; filename=\"camera.jpg\"\r\n";
        file_header += "Content-Type: image/jpeg\r\n";
        file_header += "\r\n";
        http->Write(file_header.c_str(), file_header.size());
    }

    // 第三块：JPEG数据
    size_t total_sent = 0;
    while (true) {
        JpegChunk chunk;
        if (xQueueReceive(jpeg_queue, &chunk, portMAX_DELAY) != pdPASS) {
            ESP_LOGE(TAG, "Failed to receive JPEG chunk");
            break;
        }
        if (chunk.data == nullptr) {
            break; // The last chunk
        }
        http->Write((const char*)chunk.data, chunk.len);
        total_sent += chunk.len;
        heap_caps_free(chunk.data);
    }
    // Wait for the encoder thread to finish
    encoder_thread_.join();
    // 清理队列
    vQueueDelete(jpeg_queue);

    {
        // 第四块：multipart尾部
        std::string multipart_footer;
        multipart_footer += "\r\n--" + boundary + "--\r\n";
        http->Write(multipart_footer.c_str(), multipart_footer.size());
    }
    // 结束块
    http->Write("", 0);

    if (http->GetStatusCode() != 200) {
        ESP_LOGE(TAG, "Failed to upload photo, status code: %d", http->GetStatusCode());
        throw std::runtime_error("Failed to upload photo");
    }

    std::string result = http->ReadAll();
    http->Close();

    // 保存照片到 storage 分区
    SavePhotoToStorage();

    // Get remain task stack size
    size_t remain_stack_size = uxTaskGetStackHighWaterMark(nullptr);
    ESP_LOGI(TAG, "Explain image size=%dx%d, compressed size=%d, remain stack size=%d, question=%s\n%s",
        fb_->width, fb_->height, total_sent, remain_stack_size, question.c_str(), result.c_str());
    return result;
}

void Esp32Camera::SavePhotoToStorage() {
    if (fb_ == nullptr) {
        ESP_LOGE(TAG, "Frame buffer is null, cannot save photo");
        return;
    }

    // 只支持RGB565格式直接保存为RAW
    if (fb_->format != PIXFORMAT_RGB565) {
        ESP_LOGW(TAG, "Frame format is not RGB565 (%d), skipping RAW save", fb_->format);
        return;
    }

     // ⭐ 添加：检查存储空间
      size_t total = 0, used = 0;
      if (esp_spiffs_info("storage", &total, &used) == ESP_OK) {
          size_t free_space = total - used;
          ESP_LOGI(TAG, "Storage: %zu KB total, %zu KB used, %zu KB free",
                   total/1024, used/1024, free_space/1024);

          // 如果剩余空间小于 150KB，提示用户并返回
          if (free_space < 153 * 1024) {
              ESP_LOGW(TAG, "Storage full, cannot save photo");

              // ⭐ 显示提示
              auto display = Board::GetInstance().GetDisplay();
              if (display != nullptr) {
                  display->ShowNotification("存储已满，无法保存照片", 2000);
              }
              return;
          }
      }

    // 生成文件名（使用时间戳）
    time_t now = time(NULL);
    struct tm* tm = localtime(&now);

    char filename[64];
    if (tm->tm_year >= 2025 - 1900) {
        strftime(filename, sizeof(filename), "/storage/photo_%Y%m%d%H%M%S.raw", tm);
    } else {
        // 如果时间未设置，使用微秒时间戳
        int64_t us = esp_timer_get_time();
        snprintf(filename, sizeof(filename), "/storage/photo_%lld.raw", (long long)(us / 1000000));
    }
    
    // 写入RAW文件（先写入宽度和高度，然后是像素数据）
    FILE* f = fopen(filename, "wb");
    if (f != nullptr) {
        // 写入文件头：宽度(2字节) + 高度(2字节)
        uint16_t width = fb_->width;
        uint16_t height = fb_->height;
        fwrite(&width, 2, 1, f);
        fwrite(&height, 2, 1, f);
        
        // 写入RGB565像素数据
        size_t written = fwrite(fb_->buf, 1, fb_->len, f);
        fclose(f);
        
        if (written == fb_->len) {
            ESP_LOGI(TAG, "Photo saved to %s, size: %zu bytes (RAW: %dx%d)", 
                     filename, fb_->len + 4, fb_->width, fb_->height);
        } else {
            ESP_LOGE(TAG, "Failed to write photo, written: %zu, expected: %zu", written, fb_->len);
        }
    } else {
        ESP_LOGE(TAG, "Failed to open file for writing: %s", filename);
    }
}
