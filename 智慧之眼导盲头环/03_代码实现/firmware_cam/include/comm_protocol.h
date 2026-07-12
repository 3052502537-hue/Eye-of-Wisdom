#ifndef COMM_PROTOCOL_H
#define COMM_PROTOCOL_H

#include <stdint.h>
#include <stddef.h>

#define SPI_FRAME_HEADER     0xAA55
#define SPI_FRAME_TAIL       0x55AA
#define SPI_FRAME_HEADER_LEN 2
#define SPI_FRAME_TAIL_LEN   2
#define SPI_FRAME_CMD_LEN    1
#define SPI_FRAME_LEN_LEN    2
#define SPI_FRAME_CRC_LEN    1
#define SPI_FRAME_OVERHEAD   (SPI_FRAME_HEADER_LEN + SPI_FRAME_CMD_LEN + SPI_FRAME_LEN_LEN + SPI_FRAME_CRC_LEN + SPI_FRAME_TAIL_LEN)

#define SPI_CMD_IMG_FRAME_START   0x01
#define SPI_CMD_IMG_FRAME_DATA    0x02
#define SPI_CMD_IMG_FRAME_END     0x03
#define SPI_CMD_SET_RESOLUTION    0x10
#define SPI_CMD_SET_FPS           0x11
#define SPI_CMD_ACK               0x20
#define SPI_CMD_HEARTBEAT         0x30

#define IMG_FMT_RGB565  0
#define IMG_FMT_GRAYSCALE 1
#define IMG_FMT_JPEG     2

#define RESOLUTION_QVGA    0
#define RESOLUTION_VGA     1
#define RESOLUTION_QVGA_W  320
#define RESOLUTION_QVGA_H  240
#define RESOLUTION_VGA_W   640
#define RESOLUTION_VGA_H   480

#define SPI_MAX_DATA_LEN  4096

typedef struct {
    uint8_t  cmd;
    uint16_t dataLen;
    uint8_t  data[SPI_MAX_DATA_LEN];
} SpiFrame_t;

typedef struct {
    uint16_t width;
    uint16_t height;
    uint8_t  format;
    uint32_t totalSize;
} ImgFrameStart_t;

typedef struct {
    uint16_t blockIndex;
    uint16_t blockSize;
    uint8_t* data;
} ImgFrameData_t;

typedef struct {
    uint16_t totalBlocks;
} ImgFrameEnd_t;

typedef struct {
    uint8_t  originalCmd;
    uint8_t  status;
} AckPayload_t;

typedef struct {
    uint32_t timestamp;
} HeartbeatPayload_t;

static inline uint8_t crc8(const uint8_t* data, size_t len)
{
    uint8_t crc = 0x00;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (uint8_t j = 0; j < 8; j++) {
            if (crc & 0x80) {
                crc = (crc << 1) ^ 0x07;
            } else {
                crc <<= 1;
            }
        }
    }
    return crc;
}

#endif
