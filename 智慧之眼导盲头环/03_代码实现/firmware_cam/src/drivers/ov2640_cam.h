#ifndef OV2640_CAM_H
#define OV2640_CAM_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_camera.h"

class OV2640Cam {
public:
    OV2640Cam();
    ~OV2640Cam();

    bool init(int pwdnPin, int resetPin, int xclkPin,
              int siodPin, int siocPin,
              int vsyncPin, int hrefPin, int pclkPin,
              int d0, int d1, int d2, int d3,
              int d4, int d5, int d6, int d7);

    bool capture(camera_fb_t** frameBuffer);
    void returnFrame(camera_fb_t* fb);

    bool setResolution(uint16_t width, uint16_t height);
    bool setPixelFormat(pixformat_t format);
    bool setFrameSize(framesize_t size);
    bool setQuality(int quality);
    bool setBrightness(int level);
    bool setContrast(int level);

    int getWidth(void) const { return _width; }
    int getHeight(void) const { return _height; }
    bool isInitialized(void) const { return _initialized; }

private:
    bool _initialized;
    int _width;
    int _height;
    pixformat_t _pixelFormat;
};

#endif
