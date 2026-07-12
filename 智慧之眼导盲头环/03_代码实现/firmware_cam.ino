/*
  智慧之眼导盲头环 - 摄像头板固件 (单文件版)
  版本: v1.0
  更新日期: 2026-07-10

  Arduino IDE 配置:
  - 开发板: ESP32S3 Dev Module
  - Flash Size: 8MB
  - PSRAM: Disabled (WROOM无PSRAM)
  - Upload Mode: UART0 / Hardware CDC
  - USB Mode: Hardware CDC and JTAG
  - Partition Scheme: Default 8MB with FFAT

  引脚分配:
  - OV2640摄像头: DVP接口 GPIO 2~17
  - SPI(连主控): MOSI=35, MISO=36, SCLK=37, CS=38

  需要安装的库:
  - ESP32 Camera (Arduino IDE 中搜索 esp32-camera)

  烧录说明:
  1. 在 Arduino IDE 中选择 "ESP32S3 Dev Module" 开发板
  2. 在库管理器中安装 "esp32-camera" 库
  3. 按照上述 Arduino IDE 配置项设置开发板参数
  4. 选择正确的 COM 端口
  5. 点击上传按钮烧录固件
  6. 烧录完成后通过串口监视器查看日志 (波特率 115200)

  文件结构:
  - 系统头文件引用
  - 引脚与参数定义 (来自 config.h)
  - SPI 通信协议定义 (来自 comm_protocol.h)
  - 类声明 (OV2640Cam / SpiMaster / ImagePreprocessor)
  - 类实现
  - 主程序 (setup / loop / FreeRTOS 任务)
*/

/* ============================================================
 *  系统头文件
 * ============================================================ */
#include <Arduino.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include <freertos/queue.h>

#include "esp_camera.h"
#include "camera_pins.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"


/* ============================================================
 *  一、引脚定义和参数 (来自 config.h)
 *  说明: 修改硬件接线后只需修改此区域
 *
 *  引脚分配原则:
 *   - 摄像头 DVP 引脚与 SPI 引脚完全分开，无复用
 *   - 摄像头数据总线 (D0-D7) 尽量连续
 *   - SPI 使用 ESP32-S3 FSPIQ 封装常用引脚
 * ============================================================ */

/* --- OV2640 摄像头引脚配置 (DVP接口) ---
 * 共 16 个引脚，全部独立，不与SPI复用
 */
#define PIN_CAMERA_PWDN     2          /* 掉电控制 (高电平掉电) */
#define PIN_CAMERA_RESET    3          /* 复位引脚 (低电平复位) */
#define PIN_CAMERA_XCLK     4          /* 主时钟输出 (24MHz) */
#define PIN_CAMERA_SIOD     5          /* SCCB数据 (I2C SDA) */
#define PIN_CAMERA_SIOC     6          /* SCCB时钟 (I2C SCL) */
#define PIN_CAMERA_VSYNC    7          /* 帧同步输入 */
#define PIN_CAMERA_HREF     8          /* 行同步输入 */
#define PIN_CAMERA_PCLK     9          /* 像素时钟输入 */
#define PIN_CAMERA_D7       10         /* 数据位7 (MSB) */
#define PIN_CAMERA_D6       11         /* 数据位6 */
#define PIN_CAMERA_D5       12         /* 数据位5 */
#define PIN_CAMERA_D4       13         /* 数据位4 */
#define PIN_CAMERA_D3       14         /* 数据位3 */
#define PIN_CAMERA_D2       15         /* 数据位2 */
#define PIN_CAMERA_D1       16         /* 数据位1 */
#define PIN_CAMERA_D0       17         /* 数据位0 (LSB) */

/* --- SPI主机通信（连接主控板）---
 * 使用 SPI2_HOST，引脚全部独立，不与摄像头复用
 */
#define PIN_SPI_MOSI        35         /* 主机输出 -> 从机输入 */
#define PIN_SPI_MISO        36         /* 从机输出 -> 主机输入 */
#define PIN_SPI_SCLK        37         /* SPI时钟 (主机输出) */
#define PIN_SPI_CS          38         /* 片选 (低电平有效，主机控制) */
#define SPI_HOST            SPI2_HOST
#define SPI_CLOCK_SPEED     10000000   /* 10MHz，可根据实际连线质量调整 */

/* --- 摄像头参数配置 --- */
#define CAMERA_FRAME_WIDTH   320      /* QVGA 分辨率宽度 */
#define CAMERA_FRAME_HEIGHT  240      /* QVGA 分辨率高度 */
#define CAMERA_PIXEL_FORMAT  PIXFORMAT_RGB565  /* 像素格式 */
#define CAMERA_FPS_TARGET    15       /* 目标帧率 */

/* SPI发送分块大小 */
#define SPI_SEND_BLOCK_SIZE  2048     /* 每块2KB，较大块传输更高效 */

/* --- FreeRTOS任务优先级 (数值越大优先级越高) --- */
#define TASK_PRIORITY_CAM_CAPTURE  6    /* 图像采集 - 最高优先级 */
#define TASK_PRIORITY_IMG_PROC     5    /* 图像处理 */
#define TASK_PRIORITY_SPI_SEND     6    /* SPI发送 */
#define TASK_PRIORITY_COMM         4    /* 指令处理 */
#define TASK_PRIORITY_HEARTBEAT    2    /* 心跳保活 */

/* --- FreeRTOS任务栈大小 (单位: 字节) --- */
#define TASK_STACK_CAM_CAPTURE     8192 /* 摄像头采集需要较大栈 */
#define TASK_STACK_IMG_PROC        4096
#define TASK_STACK_SPI_SEND        4096
#define TASK_STACK_COMM            2048
#define TASK_STACK_HEARTBEAT       1024

/* --- 其他配置 --- */
#define HEARTBEAT_PERIOD_MS        1000  /* 心跳周期 1秒 */
#define DEBUG_SERIAL_BAUDRATE      115200 /* 调试串口波特率 */


/* ============================================================
 *  二、SPI通信协议定义 (来自 comm_protocol.h)
 *  本协议由摄像头板与主控板共享
 * ============================================================ */

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

#define IMG_FMT_RGB565    0
#define IMG_FMT_GRAYSCALE 1
#define IMG_FMT_JPEG      2

#define RESOLUTION_QVGA    0
#define RESOLUTION_VGA     1
#define RESOLUTION_QVGA_W  320
#define RESOLUTION_QVGA_H  240
#define RESOLUTION_VGA_W   640
#define RESOLUTION_VGA_H   480

#define SPI_MAX_DATA_LEN  4096

/* --- 协议数据结构 --- */
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

/* CRC8 校验 (多项式 0x07, 初始值 0x00) */
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


/* ============================================================
 *  三、类声明
 * ============================================================ */

/* --- OV2640 摄像头驱动 (来自 ov2640_cam.h) --- */
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
    bool        _initialized;
    int         _width;
    int         _height;
    pixformat_t _pixelFormat;
};


/* --- SPI 主机通信 (来自 spi_master.h) --- */
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
    int       _mosiPin;
    int       _misoPin;
    int       _sclkPin;
    int       _csPin;
    bool      _initialized;
    bool      _running;

    uint8_t*  _txBuffer;
    uint8_t*  _rxBuffer;

    QueueHandle_t _rxQueue;

    bool buildAndSendFrame(uint8_t cmd, const uint8_t* data, uint16_t len);
    bool buildFrame(uint8_t cmd, const uint8_t* data, uint16_t len,
                    uint8_t* outBuf, uint16_t* outLen);
    bool transmitReceive(uint8_t* txBuf, uint8_t* rxBuf, uint16_t len);
    bool parseRxData(const uint8_t* data, size_t len);

    static void spiTaskFunc(void* arg);
    void spiTask(void);
};


/* --- 图像预处理 (来自 img_preprocess.h) --- */
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


/* ============================================================
 *  四、类实现
 * ============================================================ */

/* --- OV2640 摄像头驱动实现 (来自 ov2640_cam.cpp) --- */
static const char* TAG_CAM = "OV2640";

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
        ESP_LOGE(TAG_CAM, "Camera init failed: 0x%x", ret);
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

    ESP_LOGI(TAG_CAM, "OV2640 initialized: %dx%d, fmt=RGB565", _width, _height);
    return true;
}

bool OV2640Cam::capture(camera_fb_t** frameBuffer)
{
    if (!_initialized || !frameBuffer) {
        return false;
    }

    *frameBuffer = esp_camera_fb_get();
    if (!*frameBuffer) {
        ESP_LOGW(TAG_CAM, "Camera capture failed");
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
        ESP_LOGW(TAG_CAM, "Resolution %dx%d not supported", width, height);
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
        ESP_LOGI(TAG_CAM, "Pixel format set to %d", format);
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
        ESP_LOGI(TAG_CAM, "Frame size set to %dx%d", _width, _height);
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


/* --- SPI 主机通信实现 (来自 spi_master.cpp) --- */
static const char* TAG_SPI = "SpiMaster";

SpiMaster::SpiMaster()
{
    _mosiPin = -1;
    _misoPin = -1;
    _sclkPin = -1;
    _csPin = -1;
    _initialized = false;
    _running = false;
    _txBuffer = NULL;
    _rxBuffer = NULL;
    _rxQueue = NULL;
}

SpiMaster::~SpiMaster()
{
    stop();
    if (_txBuffer) {
        free(_txBuffer);
        _txBuffer = NULL;
    }
    if (_rxBuffer) {
        free(_rxBuffer);
        _rxBuffer = NULL;
    }
    if (_rxQueue) {
        vQueueDelete(_rxQueue);
        _rxQueue = NULL;
    }
    _initialized = false;
}

bool SpiMaster::init(int mosiPin, int misoPin, int sclkPin, int csPin)
{
    if (_initialized) {
        return false;
    }

    _mosiPin = mosiPin;
    _misoPin = misoPin;
    _sclkPin = sclkPin;
    _csPin = csPin;

    _txBuffer = (uint8_t*)heap_caps_malloc(4096, MALLOC_CAP_DMA);
    _rxBuffer = (uint8_t*)heap_caps_malloc(4096, MALLOC_CAP_DMA);
    if (!_txBuffer || !_rxBuffer) {
        ESP_LOGE(TAG_SPI, "Failed to allocate SPI buffers");
        return false;
    }

    _rxQueue = xQueueCreate(5, sizeof(SpiFrame_t));
    if (!_rxQueue) {
        ESP_LOGE(TAG_SPI, "Failed to create RX queue");
        return false;
    }

    spi_bus_config_t buscfg = {
        .mosi_io_num = (gpio_num_t)mosiPin,
        .miso_io_num = (gpio_num_t)misoPin,
        .sclk_io_num = (gpio_num_t)sclkPin,
        .quadwp_io_num = GPIO_NUM_NC,
        .quadhd_io_num = GPIO_NUM_NC,
        .data4_io_num = GPIO_NUM_NC,
        .data5_io_num = GPIO_NUM_NC,
        .data6_io_num = GPIO_NUM_NC,
        .data7_io_num = GPIO_NUM_NC,
        .max_transfer_sz = 4096,
        .flags = 0,
        .isr_cpu_id = INTR_CPU_ID_1,
        .intr_flags = 0,
    };

    esp_err_t ret = spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_CH_AUTO);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG_SPI, "SPI bus init failed: %s", esp_err_to_name(ret));
        return false;
    }

    spi_device_interface_config_t devcfg = {
        .command_bits = 0,
        .address_bits = 0,
        .dummy_bits = 0,
        .mode = SPI_MODE0,
        .duty_cycle_pos = 0,
        .cs_ena_pretrans = 0,
        .cs_ena_posttrans = 0,
        .clock_speed_hz = SPI_CLOCK_SPEED,
        .input_delay_ns = 0,
        .spics_io_num = (gpio_num_t)csPin,
        .flags = 0,
        .queue_size = 4,
        .pre_cb = NULL,
        .post_cb = NULL,
    };

    spi_device_handle_t handle;
    ret = spi_bus_add_device(SPI2_HOST, &devcfg, &handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG_SPI, "SPI bus add device failed: %s", esp_err_to_name(ret));
        return false;
    }

    _initialized = true;
    ESP_LOGI(TAG_SPI, "SPI master initialized: MOSI=%d MISO=%d SCLK=%d CS=%d",
             mosiPin, misoPin, sclkPin, csPin);
    return true;
}

void SpiMaster::start(void)
{
    if (!_initialized || _running) {
        return;
    }

    _running = true;
    ESP_LOGI(TAG_SPI, "SPI master started");
}

void SpiMaster::stop(void)
{
    _running = false;
    ESP_LOGI(TAG_SPI, "SPI master stopped");
}

bool SpiMaster::sendImageFrame(const uint8_t* image, uint16_t width, uint16_t height,
                                uint8_t format, uint32_t totalSize)
{
    if (!_initialized || !_running || !image) {
        return false;
    }

    uint8_t startData[9];
    startData[0] = (width >> 8) & 0xFF;
    startData[1] = width & 0xFF;
    startData[2] = (height >> 8) & 0xFF;
    startData[3] = height & 0xFF;
    startData[4] = format;
    startData[5] = (totalSize >> 24) & 0xFF;
    startData[6] = (totalSize >> 16) & 0xFF;
    startData[7] = (totalSize >> 8) & 0xFF;
    startData[8] = totalSize & 0xFF;

    if (!buildAndSendFrame(SPI_CMD_IMG_FRAME_START, startData, 9)) {
        return false;
    }

    uint16_t blockSize = SPI_SEND_BLOCK_SIZE;
    uint32_t remaining = totalSize;
    uint16_t blockIndex = 0;
    uint32_t offset = 0;

    while (remaining > 0) {
        uint16_t currentBlockSize = (remaining > blockSize) ? blockSize : remaining;
        uint8_t blockData[2 + SPI_SEND_BLOCK_SIZE];

        blockData[0] = (blockIndex >> 8) & 0xFF;
        blockData[1] = blockIndex & 0xFF;
        memcpy(blockData + 2, image + offset, currentBlockSize);

        if (!buildAndSendFrame(SPI_CMD_IMG_FRAME_DATA, blockData, 2 + currentBlockSize)) {
            ESP_LOGW(TAG_SPI, "Failed to send block %u", blockIndex);
            return false;
        }

        remaining -= currentBlockSize;
        offset += currentBlockSize;
        blockIndex++;
    }

    uint8_t endData[2];
    endData[0] = (blockIndex >> 8) & 0xFF;
    endData[1] = blockIndex & 0xFF;

    if (!buildAndSendFrame(SPI_CMD_IMG_FRAME_END, endData, 2)) {
        return false;
    }

    ESP_LOGD(TAG_SPI, "Image frame sent: %u blocks", blockIndex);
    return true;
}

bool SpiMaster::sendCommand(uint8_t cmd, const uint8_t* data, uint16_t len)
{
    return buildAndSendFrame(cmd, data, len);
}

bool SpiMaster::sendHeartbeat(uint32_t timestamp)
{
    uint8_t hbData[4];
    hbData[0] = (timestamp >> 24) & 0xFF;
    hbData[1] = (timestamp >> 16) & 0xFF;
    hbData[2] = (timestamp >> 8) & 0xFF;
    hbData[3] = timestamp & 0xFF;

    return buildAndSendFrame(SPI_CMD_HEARTBEAT, hbData, 4);
}

bool SpiMaster::buildAndSendFrame(uint8_t cmd, const uint8_t* data, uint16_t len)
{
    uint16_t frameLen;
    if (!buildFrame(cmd, data, len, _txBuffer, &frameLen)) {
        return false;
    }

    memset(_rxBuffer, 0, 4096);
    return transmitReceive(_txBuffer, _rxBuffer, frameLen);
}

bool SpiMaster::buildFrame(uint8_t cmd, const uint8_t* data, uint16_t len,
                            uint8_t* outBuf, uint16_t* outLen)
{
    if (!outBuf || !outLen) return false;
    if (len > SPI_MAX_DATA_LEN) return false;

    uint16_t totalLen = SPI_FRAME_OVERHEAD + len;
    *outLen = totalLen;

    outBuf[0] = 0xAA;
    outBuf[1] = 0x55;
    outBuf[2] = cmd;
    outBuf[3] = (len >> 8) & 0xFF;
    outBuf[4] = len & 0xFF;

    if (data && len > 0) {
        memcpy(outBuf + 5, data, len);
    }

    outBuf[5 + len] = crc8(outBuf + 2, 1 + 2 + len);
    outBuf[6 + len] = 0x55;
    outBuf[7 + len] = 0xAA;

    return true;
}

bool SpiMaster::transmitReceive(uint8_t* txBuf, uint8_t* rxBuf, uint16_t len)
{
    if (!_initialized) return false;

    spi_device_handle_t handle = NULL;
    spi_transaction_t t;
    memset(&t, 0, sizeof(t));
    t.length = len * 8;
    t.tx_buffer = txBuf;
    t.rx_buffer = rxBuf;

    esp_err_t ret = spi_device_transmit(handle, &t);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG_SPI, "SPI transmit failed: %s", esp_err_to_name(ret));
        return false;
    }

    return true;
}

bool SpiMaster::parseRxData(const uint8_t* data, size_t len)
{
    if (!data || len < SPI_FRAME_OVERHEAD) {
        return false;
    }

    if (data[0] != 0xAA || data[1] != 0x55) {
        return false;
    }

    uint8_t cmd = data[2];
    uint16_t dataLen = (data[3] << 8) | data[4];

    if (dataLen > SPI_MAX_DATA_LEN) {
        return false;
    }

    SpiFrame_t frame;
    frame.cmd = cmd;
    frame.dataLen = dataLen;
    memcpy(frame.data, data + 5, dataLen);

    if (_rxQueue) {
        xQueueSend(_rxQueue, &frame, 0);
    }

    return true;
}

void SpiMaster::spiTaskFunc(void* arg)
{
    SpiMaster* self = (SpiMaster*)arg;
    self->spiTask();
}

void SpiMaster::spiTask(void)
{
    ESP_LOGI(TAG_SPI, "SPI master task started");
    while (_running) {
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    ESP_LOGI(TAG_SPI, "SPI master task ended");
    vTaskDelete(NULL);
}


/* --- 图像预处理实现 (来自 img_preprocess.cpp) --- */
static const char* TAG_IMG = "ImgPreprocess";

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
        ESP_LOGE(TAG_IMG, "Failed to allocate scratch buffer: %d bytes", _scratchBufSize);
        return false;
    }

    _initialized = true;
    ESP_LOGI(TAG_IMG, "Image preprocessor initialized: max %dx%d", maxWidth, maxHeight);
    return true;
}

bool ImagePreprocessor::resize(const uint8_t* src, int srcW, int srcH,
                                uint8_t* dst, int dstW, int dstH)
{
    if (!_initialized || !src || !dst || srcW <= 0 || srcH <= 0 || dstW <= 0 || dstH <= 0) {
        return false;
    }

    ESP_LOGD(TAG_IMG, "Resize: %dx%d -> %dx%d (TODO: implement)", srcW, srcH, dstW, dstH);

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

    ESP_LOGD(TAG_IMG, "JPEG to RGB565: len=%lu (TODO: implement)", (unsigned long)jpegLen);
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


/* ============================================================
 *  五、主程序 (来自 main.cpp)
 *  包含全局变量、FreeRTOS 任务、setup() 和 loop()
 * ============================================================ */
static const char* TAG_MAIN = "CamMain";

OV2640Cam          g_camera;
SpiMaster          g_spiMaster;
ImagePreprocessor  g_imgPreproc;

SemaphoreHandle_t  g_frameMutex;
QueueHandle_t      g_frameQueue;

TaskHandle_t       g_taskCamCapture;
TaskHandle_t       g_taskImgProc;
TaskHandle_t       g_taskSpiSend;
TaskHandle_t       g_taskComm;
TaskHandle_t       g_taskHeartbeat;

static uint8_t*    g_capturedFrame = NULL;
static int         g_frameWidth = 0;
static int         g_frameHeight = 0;
static uint32_t    g_frameCount = 0;
static bool        g_systemReady = false;

typedef struct {
    uint8_t* data;
    int width;
    int height;
    uint8_t format;
    uint32_t size;
} FrameInfo_t;

/* --- 摄像头采集任务 --- */
static void taskCameraCapture(void* arg)
{
    ESP_LOGI(TAG_MAIN, "Camera capture task started");

    camera_fb_t* fb = NULL;

    while (1) {
        if (g_camera.capture(&fb) && fb) {
            if (xSemaphoreTake(g_frameMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
                if (g_capturedFrame) {
                    free(g_capturedFrame);
                }
                g_capturedFrame = (uint8_t*)malloc(fb->len);
                if (g_capturedFrame) {
                    memcpy(g_capturedFrame, fb->buf, fb->len);
                    g_frameWidth = fb->width;
                    g_frameHeight = fb->height;
                    g_frameCount++;
                }
                xSemaphoreGive(g_frameMutex);
            }

            FrameInfo_t frameInfo;
            frameInfo.data = g_capturedFrame;
            frameInfo.width = fb->width;
            frameInfo.height = fb->height;
            frameInfo.format = IMG_FMT_RGB565;
            frameInfo.size = fb->len;

            xQueueSend(g_frameQueue, &frameInfo, 0);
            g_camera.returnFrame(fb);
            fb = NULL;
        } else {
            ESP_LOGW(TAG_MAIN, "Camera capture failed");
        }

        vTaskDelay(pdMS_TO_TICKS(1000 / CAMERA_FPS_TARGET));
    }
}

/* --- 图像预处理任务 --- */
static void taskImgPreprocess(void* arg)
{
    ESP_LOGI(TAG_MAIN, "Image preprocess task started");

    FrameInfo_t frameInfo;

    while (1) {
        if (xQueueReceive(g_frameQueue, &frameInfo, portMAX_DELAY) == pdTRUE) {
            ESP_LOGD(TAG_MAIN, "Preprocessing frame: %dx%d",
                     frameInfo.width, frameInfo.height);

            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }
}

/* --- SPI 发送任务 --- */
static void taskSpiSend(void* arg)
{
    ESP_LOGI(TAG_MAIN, "SPI send task started");

    FrameInfo_t frameInfo;

    while (1) {
        if (xQueueReceive(g_frameQueue, &frameInfo, pdMS_TO_TICKS(100)) == pdTRUE) {
            if (frameInfo.data && frameInfo.size > 0) {
                bool ok = g_spiMaster.sendImageFrame(
                    frameInfo.data,
                    frameInfo.width,
                    frameInfo.height,
                    frameInfo.format,
                    frameInfo.size);

                if (!ok) {
                    ESP_LOGW(TAG_MAIN, "Failed to send image frame");
                }
            }
        }
    }
}

/* --- 指令处理任务 --- */
static void taskCommHandler(void* arg)
{
    ESP_LOGI(TAG_MAIN, "Command handler task started");

    SpiFrame_t rxFrame;

    while (1) {
        if (xQueueReceive(g_spiMaster.getRxQueue(), &rxFrame, pdMS_TO_TICKS(100)) == pdTRUE) {
            ESP_LOGI(TAG_MAIN, "Received command: 0x%02X, len=%u",
                     rxFrame.cmd, rxFrame.dataLen);

            switch (rxFrame.cmd) {
                case SPI_CMD_SET_RESOLUTION:
                    if (rxFrame.dataLen >= 1) {
                        ESP_LOGI(TAG_MAIN, "Set resolution: %u", rxFrame.data[0]);
                    }
                    break;
                case SPI_CMD_SET_FPS:
                    if (rxFrame.dataLen >= 1) {
                        ESP_LOGI(TAG_MAIN, "Set FPS: %u", rxFrame.data[0]);
                    }
                    break;
                default:
                    break;
            }
        }
    }
}

/* --- 心跳任务 --- */
static void taskHeartbeatFunc(void* arg)
{
    ESP_LOGI(TAG_MAIN, "Heartbeat task started");

    while (1) {
        uint32_t timestamp = (uint32_t)(esp_log_timestamp() / 1000);
        g_spiMaster.sendHeartbeat(timestamp);
        vTaskDelay(pdMS_TO_TICKS(HEARTBEAT_PERIOD_MS));
    }
}

/* ============================================================
 *  Arduino 入口
 * ============================================================ */
void setup()
{
    Serial.begin(DEBUG_SERIAL_BAUDRATE);
    delay(1000);

    ESP_LOGI(TAG_MAIN, "========================================");
    ESP_LOGI(TAG_MAIN, "  智慧之眼导盲头环 - 摄像头板启动");
    ESP_LOGI(TAG_MAIN, "========================================");

    g_frameMutex = xSemaphoreCreateMutex();
    g_frameQueue = xQueueCreate(2, sizeof(FrameInfo_t));

    ESP_LOGI(TAG_MAIN, "Initializing camera...");
    bool camOk = g_camera.init(
        PIN_CAMERA_PWDN, PIN_CAMERA_RESET, PIN_CAMERA_XCLK,
        PIN_CAMERA_SIOD, PIN_CAMERA_SIOC,
        PIN_CAMERA_VSYNC, PIN_CAMERA_HREF, PIN_CAMERA_PCLK,
        PIN_CAMERA_D0, PIN_CAMERA_D1, PIN_CAMERA_D2, PIN_CAMERA_D3,
        PIN_CAMERA_D4, PIN_CAMERA_D5, PIN_CAMERA_D6, PIN_CAMERA_D7);

    if (camOk) {
        ESP_LOGI(TAG_MAIN, "Camera initialized OK: %dx%d",
                 g_camera.getWidth(), g_camera.getHeight());
    } else {
        ESP_LOGE(TAG_MAIN, "Camera initialization FAILED!");
    }

    ESP_LOGI(TAG_MAIN, "Initializing image preprocessor...");
    g_imgPreproc.init(CAMERA_FRAME_WIDTH, CAMERA_FRAME_HEIGHT);

    ESP_LOGI(TAG_MAIN, "Initializing SPI master...");
    g_spiMaster.init(PIN_SPI_MOSI, PIN_SPI_MISO, PIN_SPI_SCLK, PIN_SPI_CS);
    g_spiMaster.start();

    ESP_LOGI(TAG_MAIN, "Creating tasks...");

    xTaskCreatePinnedToCore(
        taskCameraCapture, "cam_cap",
        8192, NULL,
        TASK_PRIORITY_CAM_CAPTURE, &g_taskCamCapture, 1);

    xTaskCreatePinnedToCore(
        taskImgPreprocess, "img_proc",
        TASK_STACK_IMG_PROC, NULL,
        TASK_PRIORITY_IMG_PROC, &g_taskImgProc, 1);

    xTaskCreatePinnedToCore(
        taskSpiSend, "spi_send",
        TASK_STACK_SPI_SEND, NULL,
        TASK_PRIORITY_SPI_SEND, &g_taskSpiSend, 0);

    xTaskCreatePinnedToCore(
        taskCommHandler, "comm",
        TASK_STACK_COMM, NULL,
        TASK_PRIORITY_COMM, &g_taskComm, 0);

    xTaskCreatePinnedToCore(
        taskHeartbeatFunc, "heartbeat",
        TASK_STACK_HEARTBEAT, NULL,
        TASK_PRIORITY_HEARTBEAT, &g_taskHeartbeat, 0);

    g_systemReady = true;
    ESP_LOGI(TAG_MAIN, "Camera board initialization complete!");
}

void loop()
{
    if (!g_systemReady) {
        delay(100);
        return;
    }

    vTaskDelay(pdMS_TO_TICKS(1000));
}
