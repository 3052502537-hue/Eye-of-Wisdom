#ifndef RD03D_H
#define RD03D_H

#include <stdint.h>
#include <stdbool.h>
#include "driver/uart.h"
#include "types.h"

class RD03D {
public:
    RD03D();
    ~RD03D();

    bool init(uart_port_t uartNum, int txPin, int rxPin, uint32_t baudrate = 115200);
    bool readTargets(RadarTarget_t* targets, uint8_t* count);
    bool setSensitivity(uint8_t level);
    bool setMaxDistance(float distance);
    bool setBaudrate(uint32_t baudrate);
    void reset(void);

private:
    uart_port_t _uartNum;
    bool _initialized;

    bool parseFrame(const uint8_t* data, size_t len, RadarTarget_t* targets, uint8_t* count);
    bool sendCommand(uint8_t cmd, const uint8_t* payload, uint8_t payloadLen);
    size_t readAvailable(uint8_t* buf, size_t bufSize, uint32_t timeoutMs);
    bool findFrameHeader(const uint8_t* data, size_t len, size_t* offset);
    uint8_t calculateChecksum(const uint8_t* data, size_t len);
};

#endif
