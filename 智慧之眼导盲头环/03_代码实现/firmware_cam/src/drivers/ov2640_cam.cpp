#include "drivers/ov2640_cam.h"
#include "config.h"
#include "esp_log.h"
#include "esp_camera.h"
#include "camera_pins.h"

static const char* TAG = "OV2640";

OV2640Cam::OV2640Cam()
{
    _initialized = false;
    _width = 0;
    _height = 0;
    _pixelFormat = PIXFORMAT_RGB565;
}

OV2640Cam::~OV2640Cam()
{
    if (_initialized) {
        esp_camera_deinit();
        _initialized = false;
    }
}

bool OV2640Cam::init(int pwdnPin, int resetPin, int xclkPin,
                      int siodPin, int siocPin,
                      int vsyncPin, int hrefPin, int pclkPin,
                      int d0, int d1, int d2, int d3,
                      int d4, int d5, int d6, int d7)
{
    if (_initialized) {
        return false;
    }

    camera_config_t config = {
        .pin_pwdn = pwdnPin,
        .pin_reset = resetPin,
        .pin_xclk = xclkPin,
        .pin_sccb_sda = siodPin,
        .pin_sccb_scl = siocPin,
        .pin_d7 = d7,
        .pin_d6 = d6,
        .pin_d5 = d5,
        .pin_d4 = d4,
        .pin_d3 = d3,
        .pin_d2 = d2,
        .pin_d1 = d1,
        .pin_d0 = d0,
        .pin_vsync = vsyncPin,
        .pin_href = hrefPin,
        .pin_pclk = pclkPin,
        .xclk_freq_hz = 20000000,
        .ledc_timer = LEDC_TIMER_0,
        .ledc_channel = LEDC_CHANNEL_0,
        .pixel_format = PIXFORMAT_RGB565,
        .frame_size = FRAMESIZE_QVGA,
        .jpeg_quality = 12,
        .fb_count = 2,
        .fb_location = CAMERA_FB_IN_PSRAM,
        .grab_mode = CAMERA_GRAB_LATEST,
    };

    esp_err_t ret = esp_camera_init(&config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Camera init failed: 0x%x", ret);
        return false;
    }

    sensor_t* s = esp_camera_sensor_get();
    if (s) {
        s->set_brightness(s, 0);
        s->set_contrast(s, 0);
        s->set_saturation(s, 0);
    }

    _width = 320;
    _height = 240;
    _pixelFormat = PIXFORMAT_RGB565;
    _initialized = true;

    ESP_LOGI(TAG, "OV2640 initialized: %dx%d, fmt=RGB565", _width, _height);
    return true;
}

bool OV2640Cam::capture(camera_fb_t** frameBuffer)
{
    if (!_initialized || !frameBuffer) {
        return false;
    }

    *frameBuffer = esp_camera_fb_get();
    if (!*frameBuffer) {
        ESP_LOGW(TAG, "Camera capture failed");
        return false;
    }

    return true;
}

void OV2640Cam::returnFrame(camera_fb_t* fb)
{
    if (fb) {
        esp_camera_fb_return(fb);
    }
}

bool OV2640Cam::setResolution(uint16_t width, uint16_t height)
{
    if (!_initialized) return false;

    framesize_t size = FRAMESIZE_QVGA;

    if (width <= 160 && height <= 120) {
        size = FRAMESIZE_QQVGA;
    } else if (width <= 320 && height <= 240) {
        size = FRAMESIZE_QVGA;
    } else if (width <= 640 && height <= 480) {
        size = FRAMESIZE_VGA;
    } else {
        ESP_LOGW(TAG, "Resolution %dx%d not supported", width, height);
        return false;
    }

    return setFrameSize(size);
}

bool OV2640Cam::setPixelFormat(pixformat_t format)
{
    if (!_initialized) return false;

    sensor_t* s = esp_camera_sensor_get();
    if (!s) return false;

    esp_err_t ret = s->set_pixformat(s, format);
    if (ret == ESP_OK) {
        _pixelFormat = format;
        ESP_LOGI(TAG, "Pixel format set to %d", format);
        return true;
    }

    return false;
}

bool OV2640Cam::setFrameSize(framesize_t size)
{
    if (!_initialized) return false;

    sensor_t* s = esp_camera_sensor_get();
    if (!s) return false;

    esp_err_t ret = s->set_framesize(s, size);
    if (ret == ESP_OK) {
        switch (size) {
            case FRAMESIZE_QQVGA:  _width = 160; _height = 120; break;
            case FRAMESIZE_QVGA:   _width = 320; _height = 240; break;
            case FRAMESIZE_VGA:    _width = 640; _height = 480; break;
            default: break;
        }
        ESP_LOGI(TAG, "Frame size set to %dx%d", _width, _height);
        return true;
    }

    return false;
}

bool OV2640Cam::setQuality(int quality)
{
    if (!_initialized) return false;

    sensor_t* s = esp_camera_sensor_get();
    if (!s) return false;

    return (s->set_quality(s, quality) == ESP_OK);
}

bool OV2640Cam::setBrightness(int level)
{
    if (!_initialized) return false;

    sensor_t* s = esp_camera_sensor_get();
    if (!s) return false;

    return (s->set_brightness(s, level) == ESP_OK);
}

bool OV2640Cam::setContrast(int level)
{
    if (!_initialized) return false;

    sensor_t* s = esp_camera_sensor_get();
    if (!s) return false;

    return (s->set_contrast(s, level) == ESP_OK);
}
