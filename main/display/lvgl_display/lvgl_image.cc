#include "lvgl_image.h"
#include <cbin_font.h>
#include <jpeg_decoder.h>
#include <src/draw/lv_image_decoder_private.h>

#include <cstring>
#include <esp_heap_caps.h>
#include <esp_log.h>
#include <stdexcept>

#define TAG "LvglImage"

namespace {

constexpr size_t kJpegWorkBufferSize = 16 * 1024;

bool IsJpeg(const uint8_t* data, size_t size) {
    return data != nullptr && size >= 3 && data[0] == 0xFF && data[1] == 0xD8 &&
           data[2] == 0xFF;
}

size_t GetDecodedBufferSize(const lv_draw_buf_t* decoded) {
    if (decoded == nullptr || decoded->header.w == 0 || decoded->header.h == 0) {
        return 0;
    }

    const size_t base_size = static_cast<size_t>(decoded->header.stride) * decoded->header.h;
    switch (decoded->header.cf) {
    case LV_COLOR_FORMAT_RGB565A8:
        return base_size + static_cast<size_t>(decoded->header.w) * decoded->header.h;
    default:
        return base_size;
    }
}

}  // namespace

void* AllocateImageBuffer(size_t size) {
#if CONFIG_SPIRAM
    void* data = heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (data != nullptr) {
        return data;
    }
#endif
    return heap_caps_malloc(size, MALLOC_CAP_8BIT);
}

void FreeImageBuffer(void* data) {
    if (data != nullptr) {
        heap_caps_free(data);
    }
}

std::unique_ptr<LvglImage> DecodeImageToPsram(void* data, size_t size) {
    if (data == nullptr || size == 0) {
        return nullptr;
    }

    lv_img_dsc_t source = {};
    source.data = static_cast<const uint8_t*>(data);
    source.data_size = size;
    source.header.magic = LV_IMAGE_HEADER_MAGIC;

    lv_image_decoder_dsc_t decoder_dsc;
    lv_image_decoder_args_t decoder_args = {};
    decoder_args.no_cache = true;
    if (lv_image_decoder_open(&decoder_dsc, &source, &decoder_args) != LV_RESULT_OK ||
        decoder_dsc.decoded == nullptr) {
        return std::make_unique<LvglRawImage>(data, size, true);
    }

    const uint32_t width = decoder_dsc.decoded->header.w;
    const uint32_t height = decoder_dsc.decoded->header.h;
    if (width == 0 || height == 0) {
        lv_image_decoder_close(&decoder_dsc);
        return std::make_unique<LvglRawImage>(data, size, true);
    }

    const size_t decoded_size = GetDecodedBufferSize(decoder_dsc.decoded);
    auto* decoded_data = static_cast<uint8_t*>(AllocateImageBuffer(decoded_size));
    if (decoded_data == nullptr) {
        lv_image_decoder_close(&decoder_dsc);
        return std::make_unique<LvglRawImage>(data, size, true);
    }

    std::memcpy(decoded_data, decoder_dsc.decoded->data, decoded_size);
    const int stride = static_cast<int>(decoder_dsc.decoded->header.stride);
    const int color_format = static_cast<int>(decoder_dsc.decoded->header.cf);
    lv_image_decoder_close(&decoder_dsc);
    return std::make_unique<LvglAllocatedImage>(decoded_data, decoded_size,
                                                static_cast<int>(width), static_cast<int>(height),
                                                stride, color_format);
}

std::shared_ptr<LvglImage> CreateCachedDecodedImage(void* data, size_t size) {
    auto image = DecodeImageToPsram(data, size);
    if (!image) {
        return nullptr;
    }
    return std::shared_ptr<LvglImage>(std::move(image));
}

LvglRawImage::LvglRawImage(void* data, size_t size)
    : LvglRawImage(data, size, false) {
}

LvglRawImage::LvglRawImage(void* data, size_t size, bool copy_to_psram) {
    bzero(&image_dsc_, sizeof(image_dsc_));
    image_dsc_.data_size = size;
    image_dsc_.data = static_cast<uint8_t*>(data);
    image_dsc_.header.magic = LV_IMAGE_HEADER_MAGIC;
    if (IsJpeg(static_cast<const uint8_t*>(data), size)) {
        auto* jpeg_input = static_cast<uint8_t*>(AllocateImageBuffer(size));
        if (jpeg_input == nullptr) {
            ESP_LOGE(TAG, "Failed to allocate JPEG input buffer, size: %u", static_cast<unsigned>(size));
            goto fallback_to_raw_decoder;
        }
        memcpy(jpeg_input, data, size);

        esp_jpeg_image_cfg_t jpeg_cfg = {};
        jpeg_cfg.indata = jpeg_input;
        jpeg_cfg.indata_size = static_cast<uint32_t>(size);
        jpeg_cfg.out_format = JPEG_IMAGE_FORMAT_RGB565;
        jpeg_cfg.out_scale = JPEG_IMAGE_SCALE_0;
        auto* jpeg_workbuf = static_cast<uint8_t*>(AllocateImageBuffer(kJpegWorkBufferSize));
        if (jpeg_workbuf == nullptr) {
            ESP_LOGE(TAG, "Failed to allocate JPEG work buffer, size: %u",
                     static_cast<unsigned>(kJpegWorkBufferSize));
            FreeImageBuffer(jpeg_input);
            goto fallback_to_raw_decoder;
        }
        jpeg_cfg.advanced.working_buffer = jpeg_workbuf;
        jpeg_cfg.advanced.working_buffer_size = kJpegWorkBufferSize;

        esp_jpeg_image_output_t output = {};
        if (esp_jpeg_get_image_info(&jpeg_cfg, &output) == ESP_OK && output.output_len > 0) {
            auto* decoded = static_cast<uint8_t*>(AllocateImageBuffer(output.output_len));
            if (decoded != nullptr) {
                jpeg_cfg.outbuf = decoded;
                jpeg_cfg.outbuf_size = static_cast<uint32_t>(output.output_len);
                if (esp_jpeg_decode(&jpeg_cfg, &output) == ESP_OK) {
                    image_dsc_.data = decoded;
                    image_dsc_.data_size = output.output_len;
                    image_dsc_.header.cf = LV_COLOR_FORMAT_RGB565;
                    image_dsc_.header.w = output.width;
                    image_dsc_.header.h = output.height;
                    image_dsc_.header.stride = output.width * 2;
                    owns_data_ = true;
                    FreeImageBuffer(jpeg_workbuf);
                    FreeImageBuffer(jpeg_input);
                    return;
                }

                FreeImageBuffer(decoded);
            }
            ESP_LOGE(TAG, "Failed to decode JPEG image, data: %p size: %u", data, static_cast<unsigned>(size));
        }
        FreeImageBuffer(jpeg_workbuf);
        FreeImageBuffer(jpeg_input);
    }

fallback_to_raw_decoder:
    if (copy_to_psram && data != nullptr && size > 0) {
        auto* copied = static_cast<uint8_t*>(AllocateImageBuffer(size));
        if (copied != nullptr) {
            memcpy(copied, data, size);
            image_dsc_.data = copied;
            owns_data_ = true;
        } else {
            ESP_LOGW(TAG, "Failed to copy raw image data to PSRAM, size: %u",
                     static_cast<unsigned>(size));
        }
    }

    if (lv_image_decoder_get_info(&image_dsc_, &image_dsc_.header) != LV_RESULT_OK) {
        image_dsc_.header.magic = LV_IMAGE_HEADER_MAGIC;
        image_dsc_.header.cf = LV_COLOR_FORMAT_RAW_ALPHA;
        image_dsc_.header.w = 0;
        image_dsc_.header.h = 0;
        image_dsc_.header.stride = 0;
    }
}

LvglRawImage::~LvglRawImage() {
    if (owns_data_ && image_dsc_.data != nullptr) {
        FreeImageBuffer((void*)image_dsc_.data);
        image_dsc_.data = nullptr;
    }
}

bool LvglRawImage::IsGif() const {
    auto ptr = static_cast<const uint8_t*>(image_dsc_.data);
    if (ptr == nullptr || image_dsc_.data_size < 3) {
        return false;
    }
    return ptr[0] == 'G' && ptr[1] == 'I' && ptr[2] == 'F';
}

LvglCBinImage::LvglCBinImage(void* data) {
    image_dsc_ = cbin_img_dsc_create(static_cast<uint8_t*>(data));
}

LvglCBinImage::LvglCBinImage(void* data, size_t size, bool copy_to_psram) {
    void* image_data = data;
    if (copy_to_psram && data != nullptr && size > 0) {
        source_data_ = AllocateImageBuffer(size);
        if (source_data_ != nullptr) {
            memcpy(source_data_, data, size);
            image_data = source_data_;
            owns_source_data_ = true;
        } else {
            ESP_LOGW(TAG, "Failed to allocate PSRAM buffer for CBIN image, size: %u",
                     static_cast<unsigned>(size));
        }
    }

    if (image_data != nullptr) {
        image_dsc_ = cbin_img_dsc_create(static_cast<uint8_t*>(image_data));
    }
}

LvglCBinImage::~LvglCBinImage() {
    if (image_dsc_ != nullptr) {
        cbin_img_dsc_delete(image_dsc_);
    }
    if (owns_source_data_ && source_data_ != nullptr) {
        FreeImageBuffer(source_data_);
        source_data_ = nullptr;
    }
}

LvglAllocatedImage::LvglAllocatedImage(void* data, size_t size) {
    bzero(&image_dsc_, sizeof(image_dsc_));
    image_dsc_.data_size = size;
    image_dsc_.data = static_cast<uint8_t*>(data);

    if (lv_image_decoder_get_info(&image_dsc_, &image_dsc_.header) != LV_RESULT_OK) {
        ESP_LOGE(TAG, "Failed to get image info, data: %p size: %u", data, static_cast<unsigned>(size));
        throw std::runtime_error("Failed to get image info");
    }
}

LvglAllocatedImage::LvglAllocatedImage(void* data, size_t size, int width, int height, int stride, int color_format) {
    bzero(&image_dsc_, sizeof(image_dsc_));
    image_dsc_.data_size = size;
    image_dsc_.data = static_cast<uint8_t*>(data);
    image_dsc_.header.magic = LV_IMAGE_HEADER_MAGIC;
    image_dsc_.header.cf = color_format;
    image_dsc_.header.w = width;
    image_dsc_.header.h = height;
    image_dsc_.header.stride = stride;
}

LvglAllocatedImage::~LvglAllocatedImage() {
    if (image_dsc_.data) {
        FreeImageBuffer((void*)image_dsc_.data);
        image_dsc_.data = nullptr;
    }
}
