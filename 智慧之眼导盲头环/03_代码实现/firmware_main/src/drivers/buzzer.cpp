#include "drivers/buzzer.h"
#include "config.h"
#include "esp_log.h"
#include "driver/ledc.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char* TAG = "Buzzer";

Buzzer::Buzzer()
    : _pin(-1), _ledcChannel(0), _ledcTimer(0),
      _initialized(false), _beeping(false), _volume(128)
{
}

Buzzer::~Buzzer()
{
    if (_initialized) {
        ledc_stop((ledc_mode_t)LEDC_LOW_SPEED_MODE, (ledc_channel_t)_ledcChannel, 0);
        _initialized = false;
    }
}

bool Buzzer::init(int pin, int ledcChannel, int ledcTimer)
{
    if (_initialized) {
        return false;
    }

    _pin = pin;
    _ledcChannel = ledcChannel;
    _ledcTimer = ledcTimer;

    ledc_timer_config_t timerConfig = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .duty_resolution = LEDC_TIMER_10_BIT,
        .timer_num = (ledc_timer_t)ledcTimer,
        .freq_hz = 1000,
        .clk_cfg = LEDC_AUTO_CLK,
    };

    esp_err_t ret = ledc_timer_config(&timerConfig);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "LEDC timer config failed: %s", esp_err_to_name(ret));
        return false;
    }

    ledc_channel_config_t channelConfig = {
        .gpio_num = (gpio_num_t)pin,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = (ledc_channel_t)ledcChannel,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = (ledc_timer_t)ledcTimer,
        .duty = 0,
        .hpoint = 0,
    };

    ret = ledc_channel_config(&channelConfig);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "LEDC channel config failed: %s", esp_err_to_name(ret));
        return false;
    }

    _initialized = true;
    ESP_LOGI(TAG, "Buzzer initialized on pin %d, channel %d", pin, ledcChannel);
    return true;
}

void Buzzer::beep(uint32_t frequency, uint32_t durationMs)
{
    if (!_initialized) {
        return;
    }

    startTone(frequency);
    vTaskDelay(pdMS_TO_TICKS(durationMs));
    stopTone();
}

void Buzzer::startTone(uint32_t frequency)
{
    if (!_initialized || frequency == 0) {
        return;
    }

    ledc_set_freq(LEDC_LOW_SPEED_MODE, (ledc_timer_t)_ledcTimer, frequency);

    uint32_t duty = (512 * _volume) / 255;
    ledc_set_duty(LEDC_LOW_SPEED_MODE, (ledc_channel_t)_ledcChannel, duty);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, (ledc_channel_t)_ledcChannel);

    _beeping = true;
    ESP_LOGD(TAG, "Start tone: %lu Hz", (unsigned long)frequency);
}

void Buzzer::stopTone(void)
{
    if (!_initialized) {
        return;
    }

    ledc_set_duty(LEDC_LOW_SPEED_MODE, (ledc_channel_t)_ledcChannel, 0);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, (ledc_channel_t)_ledcChannel);

    _beeping = false;
    ESP_LOGD(TAG, "Stop tone");
}

void Buzzer::setVolume(uint8_t volume)
{
    _volume = volume;
    ESP_LOGD(TAG, "Volume set to %u", volume);
}

void Buzzer::playPattern(const uint16_t* pattern, uint8_t count)
{
    if (!_initialized || !pattern || count == 0) {
        return;
    }

    for (uint8_t i = 0; i < count; i++) {
        if (pattern[i * 2] > 0) {
            startTone(pattern[i * 2]);
        }
        vTaskDelay(pdMS_TO_TICKS(pattern[i * 2 + 1]));
        if (pattern[i * 2] > 0) {
            stopTone();
        }
    }
}
