#ifndef SPI_SLAVE_H
#define SPI_SLAVE_H

#include <stdint.h>
#include <stdbool.h>
#include "comm_protocol.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"

#define SPI_SLAVE_MAX_IMG_BUF_SIZE (320 * 240 * 2

class SpiSlave {
public:
    SpiSlave();
    ~SpiSlave();

    bool init(int mosiPin, int misoPin, int sclkPin, int csPin);
    void start(void);
    void stop(void);

    bool isImageReady(void) const { return _imageReady; }
    uint8_t* getImageBuffer(void) { return _imageBuffer; }
    int getImageWidth(void) const { return _imageWidth; }
    int getImageHeight(void) const { return _imageHeight; }
    uint8_t getImageFormat(void) const { return _imageFormat; }
    void clearImageReady(void) { _imageReady = false; }

    bool sendCommand(uint8_t cmd, const uint8_t* data, uint16_t len);
    bool waitForImage(uint32_t timeoutMs);

    QueueHandle_t getFrameQueue(void) const { return _frameQueue; }

    void registerImageCallback(void (*callback)(void* arg), void* arg);

private:
    int _mosiPin;
    int _misoPin;
    int _sclkPin;
    int _csPin;
    bool _initialized;
    bool _running;

    uint8_t* _imageBuffer;
    int      _imageWidth;
    int      _imageHeight;
    uint8_t  _imageFormat;
    bool     _imageReady;

    uint32_t _imgTotalSize;
    uint16_t _imgTotalBlocks;
    uint16_t _imgReceivedBlocks;

    QueueHandle_t _frameQueue;

    void (*_imageCallback)(void* arg);
    void* _imageCallbackArg;

    bool parseFrame(const uint8_t* data, size_t len);
    bool handleFrameStart(const uint8_t* data, uint16_t len);
    bool handleFrameData(const uint8_t* data, uint16_t len);
    bool handleFrameEnd(const uint8_t* data, uint16_t len);
    bool handleCommand(const uint8_t* data, uint16_t len);
    bool handleAck(const uint8_t* data, uint16_t len);
    bool handleHeartbeat(const uint8_t* data, uint16_t len);

    bool sendAck(uint8_t originalCmd, uint8_t status);
    bool buildFrame(uint8_t cmd, const uint8_t* data, uint16_t len,
                   uint8_t* outBuf, uint16_t* outLen);

    static void spiTaskFunc(void* arg);
    void spiTask(void);
};

#endif
