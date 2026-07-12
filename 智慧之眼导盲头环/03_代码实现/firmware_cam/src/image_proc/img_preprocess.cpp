#include "image_proc/img_preprocess.h"
#include "esp_log.h"
#include <string.h>
#include <stdlib.h>
#include <math.h>

static const char* TAG = "ImgPreprocess";

ImagePreprocessor::ImagePreprocessor()
{
    _scratchBuf = NULL;
    _scratchBufSize = 0;
    _initialized = false;
}

ImagePreprocessor::~ImagePreprocessor()
{
    if (_scratchBuf) {
        free(_scratchBuf);
        _scratchBuf = NULL;
    }
    _initialized = false;
}

bool ImagePreprocessor::init(int maxWidth, int maxHeight)
{
    if (_initialized) {
        return false;
    }

    _scratchBufSize = maxWidth * maxHeight * 2;
    _scratchBuf = (uint8_t*)heap_caps_malloc(_scratchBufSize, MALLOC_CAP_SPIRAM);
    if (!_scratchBuf) {
        ESP_LOGE(TAG, "Failed to allocate scratch buffer: %d bytes", _scratchBufSize);
        return false;
    }

    _initialized = true;
    ESP_LOGI(TAG, "Image preprocessor initialized: max %dx%d", maxWidth, maxHeight);
    return true;
}

bool ImagePreprocessor::resize(const uint8_t* src, int srcW, int srcH,
                                uint8_t* dst, int dstW, int dstH)
{
    if (!_initialized || !src || !dst || srcW <= 0 || srcH <= 0 || dstW <= 0 || dstH <= 0) {
        return false;
    }

    ESP_LOGD(TAG, "Resize: %dx%d -> %dx%d (TODO: implement)", srcW, srcH, dstW, dstH);

    if (dstW * dstH * 2 <= _scratchBufSize) {
        memcpy(dst, src, (dstW * dstH * 2 < srcW * srcH * 2) ? dstW * dstH * 2 : srcW * srcH * 2);
    }

    return true;
}

bool ImagePreprocessor::crop(const uint8_t* src, int srcW, int srcH,
                              uint8_t* dst, int x, int y, int w, int h)
{
    if (!_initialized || !src || !dst) return false;
    if (x < 0 || y < 0 || x + w > srcW || y + h > srcH) return false;

    for (int row = 0; row < h; row++) {
        memcpy(dst + row * w * 2,
               src + ((y + row) * srcW + x) * 2,
               w * 2);
    }

    return true;
}

bool ImagePreprocessor::rgb565ToGrayscale(const uint8_t* src, uint8_t* dst, int w, int h)
{
    if (!_initialized || !src || !dst || w <= 0 || h <= 0) {
        return false;
    }

    for (int i = 0; i < w * h; i++) {
        uint16_t pixel = (src[i * 2] << 8) | src[i * 2 + 1];
        uint8_t r, g, b;
        rgb565ToRgb888(pixel, &r, &g, &b);
        dst[i] = (uint8_t)((r * 76 + g * 150 + b * 30) >> 8);
    }

    return true;
}

bool ImagePreprocessor::jpegToRgb565(const uint8_t* jpegData, uint32_t jpegLen,
                                      uint8_t* rgbBuf, int* outW, int* outH)
{
    if (!_initialized || !jpegData || !rgbBuf || !outW || !outH) {
        return false;
    }

    ESP_LOGD(TAG, "JPEG to RGB565: len=%lu (TODO: implement)", (unsigned long)jpegLen);
    return false;
}

void ImagePreprocessor::rgb565ToRgb888(uint16_t pixel, uint8_t* r, uint8_t* g, uint8_t* b)
{
    *r = (uint8_t)(((pixel >> 11) & 0x1F) * 255 / 31);
    *g = (uint8_t)(((pixel >> 5) & 0x3F) * 255 / 63);
    *b = (uint8_t)((pixel & 0x1F) * 255 / 31);
}

uint16_t ImagePreprocessor::rgb888ToRgb565(uint8_t r, uint8_t g, uint8_t b)
{
    return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}
