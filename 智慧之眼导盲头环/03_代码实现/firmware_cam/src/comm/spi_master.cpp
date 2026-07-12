#include "comm/spi_master.h"
#include "config.h"
#include "esp_log.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include <string.h>
#include <stdlib.h>

static const char* TAG = "SpiMaster";

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
        ESP_LOGE(TAG, "Failed to allocate SPI buffers");
        return false;
    }

    _rxQueue = xQueueCreate(5, sizeof(SpiFrame_t));
    if (!_rxQueue) {
        ESP_LOGE(TAG, "Failed to create RX queue");
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
        ESP_LOGE(TAG, "SPI bus init failed: %s", esp_err_to_name(ret));
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
        ESP_LOGE(TAG, "SPI bus add device failed: %s", esp_err_to_name(ret));
        return false;
    }

    _initialized = true;
    ESP_LOGI(TAG, "SPI master initialized: MOSI=%d MISO=%d SCLK=%d CS=%d",
             mosiPin, misoPin, sclkPin, csPin);
    return true;
}

void SpiMaster::start(void)
{
    if (!_initialized || _running) {
        return;
    }

    _running = true;
    ESP_LOGI(TAG, "SPI master started");
}

void SpiMaster::stop(void)
{
    _running = false;
    ESP_LOGI(TAG, "SPI master stopped");
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
            ESP_LOGW(TAG, "Failed to send block %u", blockIndex);
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

    ESP_LOGD(TAG, "Image frame sent: %u blocks", blockIndex);
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
        ESP_LOGW(TAG, "SPI transmit failed: %s", esp_err_to_name(ret));
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
    ESP_LOGI(TAG, "SPI master task started");
    while (_running) {
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    ESP_LOGI(TAG, "SPI master task ended");
    vTaskDelete(NULL);
}
