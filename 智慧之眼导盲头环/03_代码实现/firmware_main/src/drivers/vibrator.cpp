#include "drivers/vibrator.h"
#include "config.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char* TAG = "Vibrator";

Vibrator::Vibrator()
    : _frontPin(-1), _rearPin(-1), _leftPin(-1), _rightPin(-1),
      _initialized(false), _intensity(100)
{
}

Vibrator::~Vibrator()
{
    stopAll();
    _initialized = false;
}

bool Vibrator::init(int frontPin, int rearPin, int leftPin, int rightPin)
{
    if (_initialized) {
        return false;
    }

    _frontPin = frontPin;
    _rearPin = rearPin;
    _leftPin = leftPin;
    _rightPin = rightPin;

    gpio_config_t ioConfig = {
        .pin_bit_mask = (1ULL << frontPin) | (1ULL << rearPin) |
                        (1ULL << leftPin) | (1ULL << rightPin),
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

    stopAll();

    _initialized = true;
    ESP_LOGI(TAG, "Vibrator initialized: front=%d rear=%d left=%d right=%d",
             frontPin, rearPin, leftPin, rightPin);
    return true;
}

void Vibrator::vibrate(WarnDirection_t direction, uint32_t durationMs)
{
    if (!_initialized) {
        return;
    }

    switch (direction) {
        case WARN_DIR_FRONT:
            vibrateFront(durationMs);
            break;
        case WARN_DIR_FRONT_LEFT:
            setPin(_frontPin, true);
            setPin(_leftPin, true);
            vTaskDelay(pdMS_TO_TICKS(durationMs));
            setPin(_frontPin, false);
            setPin(_leftPin, false);
            break;
        case WARN_DIR_FRONT_RIGHT:
            setPin(_frontPin, true);
            setPin(_rightPin, true);
            vTaskDelay(pdMS_TO_TICKS(durationMs));
            setPin(_frontPin, false);
            setPin(_rightPin, false);
            break;
        case WARN_DIR_LEFT:
            vibrateLeft(durationMs);
            break;
        case WARN_DIR_RIGHT:
            vibrateRight(durationMs);
            break;
        case WARN_DIR_REAR:
            vibrateRear(durationMs);
            break;
        case WARN_DIR_ALL:
            vibrateAll(durationMs);
            break;
        default:
            break;
    }
}

void Vibrator::vibrateFront(uint32_t durationMs)
{
    if (!_initialized) return;
    setPin(_frontPin, true);
    vTaskDelay(pdMS_TO_TICKS(durationMs));
    setPin(_frontPin, false);
}

void Vibrator::vibrateRear(uint32_t durationMs)
{
    if (!_initialized) return;
    setPin(_rearPin, true);
    vTaskDelay(pdMS_TO_TICKS(durationMs));
    setPin(_rearPin, false);
}

void Vibrator::vibrateLeft(uint32_t durationMs)
{
    if (!_initialized) return;
    setPin(_leftPin, true);
    vTaskDelay(pdMS_TO_TICKS(durationMs));
    setPin(_leftPin, false);
}

void Vibrator::vibrateRight(uint32_t durationMs)
{
    if (!_initialized) return;
    setPin(_rightPin, true);
    vTaskDelay(pdMS_TO_TICKS(durationMs));
    setPin(_rightPin, false);
}

void Vibrator::vibrateAll(uint32_t durationMs)
{
    if (!_initialized) return;
    setPin(_frontPin, true);
    setPin(_rearPin, true);
    setPin(_leftPin, true);
    setPin(_rightPin, true);
    vTaskDelay(pdMS_TO_TICKS(durationMs));
    stopAll();
}

void Vibrator::stopAll(void)
{
    if (!_initialized) return;
    setPin(_frontPin, false);
    setPin(_rearPin, false);
    setPin(_leftPin, false);
    setPin(_rightPin, false);
}

void Vibrator::setIntensity(uint8_t intensity)
{
    _intensity = intensity;
    ESP_LOGD(TAG, "Intensity set to %u", intensity);
}

void Vibrator::setPin(int pin, bool state)
{
    if (pin < 0) return;
    gpio_set_level((gpio_num_t)pin, state ? 1 : 0);
}
