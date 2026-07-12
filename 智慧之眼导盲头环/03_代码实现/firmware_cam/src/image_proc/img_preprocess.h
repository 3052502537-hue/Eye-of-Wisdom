#ifndef IMG_PREPROCESS_H
#define IMG_PREPROCESS_H

#include <stdint.h>
#include <stdbool.h>

class ImagePreprocessor {
public:
    ImagePreprocessor();
    ~ImagePreprocessor();

    bool init(int maxWidth, int maxHeight);

    bool resize(const uint8_t* src, int srcW, int srcH,
                uint8_t* dst, int dstW, int dstH);

    bool crop(const uint8_t* src, int srcW, int srcH,
              uint8_t* dst, int x, int y, int w, int h);

    bool rgb565ToGrayscale(const uint8_t* src, uint8_t* dst, int w, int h);

    bool jpegToRgb565(const uint8_t* jpegData, uint32_t jpegLen,
                      uint8_t* rgbBuf, int* outW, int* outH);

    int getScratchBufferSize(void) const { return _scratchBufSize; }
    uint8_t* getScratchBuffer(void) { return _scratchBuf; }

private:
    uint8_t* _scratchBuf;
    int      _scratchBufSize;
    bool     _initialized;

    void rgb565ToRgb888(uint16_t pixel, uint8_t* r, uint8_t* g, uint8_t* b);
    uint16_t rgb888ToRgb565(uint8_t r, uint8_t g, uint8_t b);
};

#endif
