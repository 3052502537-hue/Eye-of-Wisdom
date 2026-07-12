#include "comm/spi_slave.h"
#include "config.h"
#include "esp_log.h"
#include "driver/spi_slave.h"
#include "driver/gpio.h"
#include <string.h>
#include <stdlib.h>

static const char* TAG = "SpiSlave";

SpiSlave::SpiSlave()
{
    _mosiPin = -1;
    _misoPin = -1;
    _sclkPin = -1;
    _csPin = -1;
    _initialized = false;
    _running = false;

    _imageBuffer = NULL;
    _imageWidth = 0;
    _imageHeight = 0;
    _imageFormat = 0;
    _imageReady = false;

    _imgTotalSize = 0;
    _imgTotalBlocks = 0;
    _imgReceivedBlocks = 0;

    _frameQueue = NULL;
    _imageCallback = NULL;
    _imageCallbackArg = NULL;
}

SpiSlave::~SpiSlave()
{
    stop();
    if (_imageBuffer) {
        free(_imageBuffer);
        _imageBuffer = NULL;
    }
    if (_frameQueue) {
        vQueueDelete(_frameQueue);
        _frameQueue = NULL;
    }
    _initialized = false;
}

bool SpiSlave::init(int mosiPin, int misoPin, int sclkPin, int csPin)
{
    if (_initialized) {
        return false;
    }

    _mosiPin = mosiPin;
    _misoPin = misoPin;
    _sclkPin = sclkPin;
    _csPin = csPin;

    _imageBuffer = (uint8_t*)heap_caps_malloc(SPI_SLAVE_MAX_IMG_BUF_SIZE, MALLOC_CAP_SPIRAM);
    if (!_imageBuffer) {
        ESP_LOGE(TAG, "Failed to allocate image buffer");
        return false;
    }
    memset(_imageBuffer, 0, SPI_SLAVE_MAX_IMG_BUF_SIZE);

    _frameQueue = xQueueCreate(5, sizeof(SpiFrame_t));
    if (!_frameQueue) {
        ESP_LOGE(TAG, "Failed to create frame queue");
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

    spi_slave_interface_config_t slvcfg = {
        .spics_io_num = (gpio_num_t)csPin,
        .flags = 0,
        .queue_size = 4,
        .mode = SPI_MODE0,
        .post_setup_cb = NULL,
        .post_trans_cb = NULL,
    };

    esp_err_t ret = spi_slave_initialize(SPI2_HOST, &buscfg, &slvcfg, SPI_DMA_CH_AUTO);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SPI slave init failed: %s", esp_err_to_name(ret));
        return false;
    }

    _initialized = true;
    ESP_LOGI(TAG, "SPI slave initialized: MOSI=%d MISO=%d SCLK=%d CS=%d",
             mosiPin, misoPin, sclkPin, csPin);
    return true;
}

void SpiSlave::start(void)
{
    if (!_initialized || _running) {
        return;
    }

    _running = true;

    xTaskCreatePinnedToCore(
        spiTaskFunc,
        "spi_slave_task",
        TASK_STACK_SPI_RECV,
        this,
        TASK_PRIORITY_SPI_RECV,
        NULL,
        1);

    ESP_LOGI(TAG, "SPI slave started");
}

void SpiSlave::stop(void)
{
    _running = false;
    ESP_LOGI(TAG, "SPI slave stopped");
}

bool SpiSlave::sendCommand(uint8_t cmd, const uint8_t* data, uint16_t len)
{
    if (!_initialized || !_running) {
        return false;
    }

    ESP_LOGD(TAG, "Send command: 0x%02X, len=%u (TODO: implement)", cmd, len);
    return true;
}

bool SpiSlave::waitForImage(uint32_t timeoutMs)
{
    if (!_initialized) {
        return false;
    }

    uint32_t start = xTaskGetTickCount();
    while (!_imageReady) {
        if ((xTaskGetTickCount() - start) > pdMS_TO_TICKS(timeoutMs)) {
            return false;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    return true;
}

void SpiSlave::registerImageCallback(void (*callback)(void* arg), void* arg)
{
    _imageCallback = callback;
    _imageCallbackArg = arg;
}

bool SpiSlave::parseFrame(const uint8_t* data, size_t len)
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
        ESP_LOGW(TAG, "Data length too large: %u", dataLen);
        return false;
    }

    if (len < SPI_FRAME_HEADER_LEN + SPI_FRAME_CMD_LEN + SPI_FRAME_LEN_LEN + dataLen + SPI_FRAME_CRC_LEN + SPI_FRAME_TAIL_LEN) {
        return false;
    }

    uint8_t crc = data[5 + dataLen];
    uint8_t calcCrc = crc8(data + 2, 1 + 2 + dataLen);
    if (crc != calcCrc) {
        ESP_LOGW(TAG, "CRC mismatch: 0x%02X vs 0x%02X", crc, calcCrc);
        return false;
    }

    const uint8_t* payload = data + 5;

    switch (cmd) {
        case SPI_CMD_IMG_FRAME_START:
            return handleFrameStart(payload, dataLen);
        case SPI_CMD_IMG_FRAME_DATA:
            return handleFrameData(payload, dataLen);
        case SPI_CMD_IMG_FRAME_END:
            return handleFrameEnd(payload, dataLen);
        case SPI_CMD_ACK:
            return handleAck(payload, dataLen);
        case SPI_CMD_HEARTBEAT:
            return handleHeartbeat(payload, dataLen);
        default:
            ESP_LOGW(TAG, "Unknown command: 0x%02X", cmd);
            return false;
    }
}

bool SpiSlave::handleFrameStart(const uint8_t* data, uint16_t len)
{
    if (len < 9) return false;

    _imageWidth = (data[0] << 8) | data[1];
    _imageHeight = (data[2] << 8) | data[3];
    _imageFormat = data[4];
    _imgTotalSize = (data[5] << 24) | (data[6] << 16) | (data[7] << 8) | data[8];
    _imgReceivedBlocks = 0;
    _imageReady = false;

    ESP_LOGI(TAG, "Image frame start: %dx%d, fmt=%d, size=%lu",
             _imageWidth, _imageHeight, _imageFormat, (unsigned long)_imgTotalSize);

    return true;
}

bool SpiSlave::handleFrameData(const uint8_t* data, uint16_t len)
{
    if (len < 4) return false;

    uint16_t blockIndex = (data[0] << 8) | data[1];
    uint16_t blockSize = len - 2;

    uint32_t offset = blockIndex * SPI_SEND_BLOCK_SIZE;
    if (offset + blockSize > SPI_SLAVE_MAX_IMG_BUF_SIZE) {
        ESP_LOGW(TAG, "Block %u exceeds buffer", blockIndex);
        return false;
    }

    memcpy(_imageBuffer + offset, data + 2, blockSize);
    _imgReceivedBlocks++;

    ESP_LOGD(TAG, "Received block %u, size=%u", blockIndex, blockSize);
    return true;
}

bool SpiSlave::handleFrameEnd(const uint8_t* data, uint16_t len)
{
    if (len < 2) return false;

    _imgTotalBlocks = (data[0] << 8) | data[1];

    ESP_LOGI(TAG, "Image frame end: total blocks=%u, received=%u",
             _imgTotalBlocks, _imgReceivedBlocks);

    if (_imgReceivedBlocks == _imgTotalBlocks) {
        _imageReady = true;
        if (_imageCallback) {
            _imageCallback(_imageCallbackArg);
        }
        return true;
    } else {
        ESP_LOGW(TAG, "Block mismatch: expected %u, got %u",
                 _imgTotalBlocks, _imgReceivedBlocks);
        return false;
    }
}

bool SpiSlave::handleCommand(const uint8_t* data, uint16_t len)
{
    ESP_LOGD(TAG, "Received command, len=%u", len);
    return true;
}

bool SpiSlave::handleAck(const uint8_t* data, uint16_t len)
{
    if (len < 2) return false;
    ESP_LOGD(TAG, "ACK: cmd=0x%02X, status=%u", data[0], data[1]);
    return true;
}

bool SpiSlave::handleHeartbeat(const uint8_t* data, uint16_t len)
{
    if (len < 4) return false;
    uint32_t timestamp = (data[0] << 24) | (data[1] << 16) | (data[2] << 8) | data[3];
    ESP_LOGD(TAG, "Heartbeat: timestamp=%lu", (unsigned long)timestamp);
    return true;
}

bool SpiSlave::sendAck(uint8_t originalCmd, uint8_t status)
{
    uint8_t ackData[2] = {originalCmd, status};
    ESP_LOGD(TAG, "Send ACK: cmd=0x%02X, status=%u (TODO)", originalCmd, status);
    return true;
}

bool SpiSlave::buildFrame(uint8_t cmd, const uint8_t* data, uint16_t len,
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

void SpiSlave::spiTaskFunc(void* arg)
{
    SpiSlave* self = (SpiSlave*)arg;
    self->spiTask();
}

void SpiSlave::spiTask(void)
{
    ESP_LOGI(TAG, "SPI slave task started");

    WORD_ALIGNED_ATTR uint8_t recvBuf[4096];
    WORD_ALIGNED_ATTR uint8_t sendBuf[4096];

    while (_running) {
        spi_slave_transaction_t t;
        memset(&t, 0, sizeof(t));
        t.length = 4096 * 8;
        t.tx_buffer = sendBuf;
        t.rx_buffer = recvBuf;

        esp_err_t ret = spi_slave_transmit(SPI2_HOST, &t, portMAX_DELAY);
        if (ret == ESP_OK) {
            int recvLen = t.trans_len / 8;
            if (recvLen > 0) {
                parseFrame(recvBuf, recvLen);
            }
        } else {
            ESP_LOGW(TAG, "SPI transaction failed: %s", esp_err_to_name(ret));
        }
    }

    ESP_LOGI(TAG, "SPI slave task ended");
    vTaskDelete(NULL);
}
