/* ============================================================
 * 文件名: alarm_manager.cpp
 * 功能描述: 蜂鸣器 + RGB 状态指示管理实现
 *           实现 LEDC PWM 驱动蜂鸣器、WS2812 驱动RGB、
 *           状态-颜色映射、闪烁效果、自检流程
 * 依赖关系: Arduino Core(LEDC/neopixel)、config.h、alarm_manager.h
 * 接口说明: 见头文件
 *
 * 说明:
 *   - 蜂鸣器使用 ESP32 LEDC 硬件 PWM，频率可调
 *   - WS2812 使用 ESP32 Arduino Core 内置 neopixelWrite()
 *     (需 ESP32 Arduino Core 2.0.4+)
 * ============================================================ */

#include "alarm_manager.h"

#ifdef DEBUG
  #define DBG_PRINT(x)     Serial.print(x)
  #define DBG_PRINTLN(x)   Serial.println(x)
  #define DBG_PRINTF(fmt, ...) Serial.printf(fmt, ##__VA_ARGS__)
#else
  #define DBG_PRINT(x)
  #define DBG_PRINTLN(x)
  #define DBG_PRINTF(fmt, ...)
#endif

/* 构造函数 */
AlarmManager::AlarmManager()
    : _initialized(false), _curState(SYS_STATE_POWER_ON),
      _dangerAlarm(false), _lastToggleMs(0), _toggleOn(false)
{
}

/* 析构函数 */
AlarmManager::~AlarmManager()
{
    setBuzzerOff();
    setRgbColor(0, 0, 0);   // 熄灯
    _initialized = false;
}

/* begin - 初始化 */
bool AlarmManager::begin()
{
    if (_initialized) return true;

    /* 蜂鸣器引脚: LEDC PWM 配置(ESP32 Arduino Core 3.x 引脚式API)
     * ledcAttach(引脚, 频率, 分辨率位数) */
    ledcAttach(PIN_BUZZER, BUZZER_LEDC_FREQ_HZ, BUZZER_LEDC_TIMER_BITS);
    ledcWrite(PIN_BUZZER, 0);   // 默认关闭(占空比0)

    /* RGB 引脚: WS2812 数据线，初始化为熄灭 */
    pinMode(PIN_RGB_LED, OUTPUT);
    setRgbColor(0, 0, 0);

    _initialized = true;
    DBG_PRINTLN("[ALARM] init OK");
    return true;
}

/* setBuzzerOn - 开启蜂鸣器 */
void AlarmManager::setBuzzerOn(uint32_t freqHz)
{
    if (!_initialized) return;
    /* 重新设置频率并给50%占空比发声 */
    ledcWriteTone(PIN_BUZZER, freqHz);
}

/* setBuzzerOff - 关闭蜂鸣器 */
void AlarmManager::setBuzzerOff()
{
    if (!_initialized) return;
    ledcWriteTone(PIN_BUZZER, 0);   // 频率0 = 静音
}

/* beep - 单次短响(阻塞) */
void AlarmManager::beep(uint32_t freqHz, uint32_t durationMs)
{
    if (!_initialized) return;
    setBuzzerOn(freqHz);
    delay(durationMs);
    setBuzzerOff();
}

/* setDangerAlarm - 设置危险报警 */
void AlarmManager::setDangerAlarm(bool on)
{
    _dangerAlarm = on;
    if (on) {
        _curState = SYS_STATE_DANGER;
    } else {
        /* 恢复到正常(若原为危险) */
        if (_curState == SYS_STATE_DANGER) {
            _curState = SYS_STATE_NORMAL;
        }
        setBuzzerOff();
    }
}

/* setRgbColor - 直接设置RGB颜色 */
void AlarmManager::setRgbColor(uint8_t r, uint8_t g, uint8_t b)
{
    /* ESP32 Arduino Core 内置 WS2812 驱动(GRBRGB格式按板载LED)
     * neopixelWrite(pin, r, g, b) 适用于单颗WS2812 */
    neopixelWrite(PIN_RGB_LED, r, g, b);
}

/* setSystemState - 设置系统状态 */
void AlarmManager::setSystemState(uint8_t state)
{
    if (state == _curState) return;
    _curState = state;
    _toggleOn = true;
    _lastToggleMs = millis();

    /* 非危险状态时关闭蜂鸣器 */
    if (state != SYS_STATE_DANGER) {
        setBuzzerOff();
        _dangerAlarm = false;
    }

    DBG_PRINTF("[ALARM] state=%u\n", state);
}

/* update - 周期调用驱动闪烁等效果 */
void AlarmManager::update()
{
    if (!_initialized) return;

    uint32_t now = millis();
    uint8_t r, g, b;

    switch (_curState) {
    case SYS_STATE_POWER_ON:
        /* 上电: 蓝色常亮 */
        setRgbColor(0, 0, 255);
        break;

    case SYS_STATE_INIT:
        /* 初始化中: 蓝色闪烁(500ms周期) */
        if (now - _lastToggleMs >= 250) {
            _toggleOn = !_toggleOn;
            _lastToggleMs = now;
        }
        setRgbColor(0, 0, _toggleOn ? 255 : 0);
        break;

    case SYS_STATE_NORMAL:
    case SYS_STATE_WIFI_CONNECTED:
        /* 正常/WiFi连接: 绿色常亮 */
        setRgbColor(0, 255, 0);
        break;

    case SYS_STATE_WARNING:
        /* 警告: 黄色常亮 */
        setRgbColor(255, 200, 0);
        break;

    case SYS_STATE_DANGER:
        /* 危险: 红色闪烁 + 蜂鸣器间歇响(200ms周期) */
        if (now - _lastToggleMs >= 100) {
            _toggleOn = !_toggleOn;
            _lastToggleMs = now;
            if (_toggleOn) {
                setBuzzerOn(BUZZER_LEDC_FREQ_HZ);
            } else {
                setBuzzerOff();
            }
        }
        setRgbColor(_toggleOn ? 255 : 0, 0, 0);
        break;

    case SYS_STATE_FAULT:
        /* 故障: 红色常亮 */
        setRgbColor(255, 0, 0);
        break;

    default:
        setRgbColor(0, 0, 0);
        break;
    }
}

/* selfTest - 自检: 蜂鸣器短响 + RGB三色循环 */
bool AlarmManager::selfTest()
{
    if (!_initialized) return false;

    DBG_PRINTLN("[ALARM] self-test start");

    /* RGB 三色循环 */
    setRgbColor(0, 0, 255);   delay(150);   // 蓝
    setRgbColor(0, 255, 0);   delay(150);   // 绿
    setRgbColor(255, 0, 0);   delay(150);   // 红
    setRgbColor(0, 0, 0);     delay(50);

    /* 蜂鸣器短响自检 */
    beep(2000, 100);

    DBG_PRINTLN("[ALARM] self-test done");
    return true;
}
