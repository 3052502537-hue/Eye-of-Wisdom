#ifndef SPI_MASTER_H
#define SPI_MASTER_H

#include <stdint.h>
#include <stdbool.h>
#include "comm_protocol.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

class SpiMaster {
public:
    SpiMaster();
    ~SpiMaster();

    bool init(int mosiPin, int misoPin, int sclkPin, int csPin);
    void start(void);
    void stop(void);

    bool sendImageFrame(const uint8_t* image, uint16_t width, uint16_t height,
                        uint8_t format, uint32_t totalSize);
    bool sendCommand(uint8_t cmd, const uint8_t* data, uint16_t len);
    bool sendHeartbeat(uint32_t timestamp);

    QueueHandle_t getRxQueue(void) const { return _rxQueue; }

private:
    int _mosiPin;
    int _misoPin;
    int _sclkPin;
    int _csPin;
    bool _initialized;
    bool _running;

    uint8_t* _txBuffer;
    uint8_t* _rxBuffer;

    QueueHandle_t _rxQueue;

    bool buildAndSendFrame(uint8_t cmd, const uint8_t* data, uint16_t len);
    bool buildFrame(uint8_t cmd, const uint8_t* data, uint16_t len,
                    uint8_t* outBuf, uint16_t* outLen);
    bool transmitReceive(uint8_t* txBuf, uint8_t* rxBuf, uint16_t len);
    bool parseRxData(const uint8_t* data, size_t len);

    static void spiTaskFunc(void* arg);
    void spiTask(void);
};

#endif
