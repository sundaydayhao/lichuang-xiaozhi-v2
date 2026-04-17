#pragma once

#include <lvgl.h>
#include <memory>

void* AllocateImageBuffer(size_t size);
void FreeImageBuffer(void* data);
std::shared_ptr<class LvglImage> CreateCachedRgb565Image(void* data, size_t size);


// Wrap around lv_img_dsc_t
class LvglImage {
public:
    virtual const lv_img_dsc_t* image_dsc() const = 0;
    virtual bool IsGif() const { return false; }
    virtual ~LvglImage() = default;
};


class LvglRawImage : public LvglImage {
public:
    LvglRawImage(void* data, size_t size);
    virtual ~LvglRawImage();
    virtual const lv_img_dsc_t* image_dsc() const override { return &image_dsc_; }
    virtual bool IsGif() const;

private:
    lv_img_dsc_t image_dsc_;
    bool owns_data_ = false;
};

class LvglCBinImage : public LvglImage {
public:
    LvglCBinImage(void* data);
    LvglCBinImage(void* data, size_t size, bool copy_to_psram);
    virtual ~LvglCBinImage();
    virtual const lv_img_dsc_t* image_dsc() const override { return image_dsc_; }

private:
    lv_img_dsc_t* image_dsc_ = nullptr;
    void* source_data_ = nullptr;
    bool owns_source_data_ = false;
};

class LvglSourceImage : public LvglImage {
public:
    LvglSourceImage(const lv_img_dsc_t* image_dsc) : image_dsc_(image_dsc) {}
    virtual const lv_img_dsc_t* image_dsc() const override { return image_dsc_; }

private:
    const lv_img_dsc_t* image_dsc_;
};

class LvglAllocatedImage : public LvglImage {
public:
    LvglAllocatedImage(void* data, size_t size);
    LvglAllocatedImage(void* data, size_t size, int width, int height, int stride, int color_format);
    virtual ~LvglAllocatedImage();
    virtual const lv_img_dsc_t* image_dsc() const override { return &image_dsc_; }

private:
    lv_img_dsc_t image_dsc_;
};
