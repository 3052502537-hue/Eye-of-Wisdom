#include "drivers/status_led.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char* TAG = "StatusLED";

StatusLED::StatusLED() : _pin(-1), _initialized(false), _state(false)
{
}

StatusLED::~StatusLED()
{
    if (_initialized) {
        off();
        _initialized = false;
    }
}

bool StatusLED::init(int pin)
{
    if (_initialized) {
        return false;
    }

    _pin = pin;

    gpio_config_t ioConfig = {
        .pin_bit_mask = 1ULL << pin,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    esp_err_t ret = gpio_config(&ioConfig);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "GPIO config failed: %s", esp_err_to_name(ret));
        return false;
    }

    off();
    _initialized = true;
    ESP_LOGI(TAG, "StatusLED initialized on pin %d", pin);
    return true;
}

void StatusLED::on(void)
{
    if (!_initialized) return;
    gpio_set_level((gpio_num_t)_pin, 1);
    _state = true;
}

void StatusLED::off(void)
{
    if (!_initialized) return;
    gpio_set_level((gpio_num_t)_pin, 0);
    _state = false;
}

void StatusLED::toggle(void)
{
    if (!_initialized) return;
    if (_state) {
        off();
    } else {
        on();
    }
}

void StatusLED::blink(uint32_t onMs, uint32_t offMs, uint8_t count)
{
    if (!_initialized) return;

    for (uint8_t i = 0; i < count; i++) {
        on();
        vTaskDelay(pdMS_TO_TICKS(onMs));
        off();
        vTaskDelay(pdMS_TO_TICKS(offMs));
    }
}

void StatusLED::setPattern(uint8_t pattern)
{
    if (!_initialized) return;
    ESP_LOGD(TAG, "Set pattern: %u (TODO: implement)", pattern);
}
