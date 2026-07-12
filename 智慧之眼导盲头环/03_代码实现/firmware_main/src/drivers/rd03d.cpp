#include "drivers/rd03d.h"
#include "config.h"
#include <string.h>
#include "esp_log.h"

static const char* TAG = "RD03D";

RD03D::RD03D() : _uartNum(UART_NUM_0), _initialized(false)
{
}

RD03D::~RD03D()
{
    if (_initialized) {
        uart_driver_delete(_uartNum);
        _initialized = false;
    }
}

bool RD03D::init(uart_port_t uartNum, int txPin, int rxPin, uint32_t baudrate)
{
    if (_initialized) {
        ESP_LOGW(TAG, "Already initialized");
        return false;
    }

    _uartNum = uartNum;

    uart_config_t uartConfig = {
        .baud_rate = (int)baudrate,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    esp_err_t ret = uart_driver_install(_uartNum, 1024 * 4, 0, 0, NULL, 0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "UART driver install failed: %s", esp_err_to_name(ret));
        return false;
    }

    ret = uart_param_config(_uartNum, &uartConfig);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "UART param config failed: %s", esp_err_to_name(ret));
        return false;
    }

    ret = uart_set_pin(_uartNum, txPin, rxPin, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "UART set pin failed: %s", esp_err_to_name(ret));
        return false;
    }

    _initialized = true;
    ESP_LOGI(TAG, "RD03D initialized on UART%d, baud=%lu", uartNum, (unsigned long)baudrate);
    return true;
}

bool RD03D::readTargets(RadarTarget_t* targets, uint8_t* count)
{
    if (!_initialized || !targets || !count) {
        return false;
    }

    uint8_t buf[512];
    size_t readLen = readAvailable(buf, sizeof(buf), 100);

    if (readLen == 0) {
        *count = 0;
        return true;
    }

    return parseFrame(buf, readLen, targets, count);
}

bool RD03D::parseFrame(const uint8_t* data, size_t len, RadarTarget_t* targets, uint8_t* count)
{
    if (!data || len < 8 || !targets || !count) {
        return false;
    }

    *count = 0;

    size_t offset = 0;
    if (!findFrameHeader(data, len, &offset)) {
        return false;
    }

    ESP_LOGD(TAG, "Frame found at offset %u, data[0]=0x%02X data[1]=0x%02X",
             (unsigned)offset, data[offset], data[offset + 1]);

    return true;
}

bool RD03D::sendCommand(uint8_t cmd, const uint8_t* payload, uint8_t payloadLen)
{
    if (!_initialized) {
        return false;
    }

    return true;
}

size_t RD03D::readAvailable(uint8_t* buf, size_t bufSize, uint32_t timeoutMs)
{
    if (!_initialized || !buf || bufSize == 0) {
        return 0;
    }

    int len = uart_read_bytes(_uartNum, buf, bufSize, timeoutMs / portTICK_PERIOD_MS);
    return (len > 0) ? (size_t)len : 0;
}

bool RD03D::findFrameHeader(const uint8_t* data, size_t len, size_t* offset)
{
    if (!data || len < 2 || !offset) {
        return false;
    }

    for (size_t i = 0; i < len - 1; i++) {
        if (data[i] == 0xAA && data[i + 1] == 0x55) {
            *offset = i;
            return true;
        }
    }
    return false;
}

uint8_t RD03D::calculateChecksum(const uint8_t* data, size_t len)
{
    uint8_t sum = 0;
    for (size_t i = 0; i < len; i++) {
        sum += data[i];
    }
    return sum;
}

bool RD03D::setSensitivity(uint8_t level)
{
    if (!_initialized) {
        return false;
    }

    ESP_LOGI(TAG, "Set sensitivity level: %u (TODO: implement)", level);
    return true;
}

bool RD03D::setMaxDistance(float distance)
{
    if (!_initialized) {
        return false;
    }

    ESP_LOGI(TAG, "Set max distance: %.2f (TODO: implement)", distance);
    return true;
}

bool RD03D::setBaudrate(uint32_t baudrate)
{
    if (!_initialized) {
        return false;
    }

    ESP_LOGI(TAG, "Set baudrate: %lu (TODO: implement)", (unsigned long)baudrate);
    return true;
}

void RD03D::reset(void)
{
    ESP_LOGI(TAG, "Reset RD03D (TODO: implement)");
}
