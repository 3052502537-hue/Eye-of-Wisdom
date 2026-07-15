/* ============================================================
 * 文件名: hc_sr04_driver.cpp
 * 功能描述: HC-SR04 超声波测距传感器驱动实现
 *           使用 Trig 引脚发送 10μs 启动脉冲，Echo 引脚测量回波时间
 *           声速 ≈ 343m/s (20°C)
 *           距离 = (高电平时间μs * 0.0343) / 2 = 高电平时间 / 58 (cm)
 * 依赖关系: Arduino GPIO 函数、config.h
 * 接口说明: 见头文件
 * ============================================================ */

#include "hc_sr04_driver.h"
#include "config.h"

/* 构造函数 */
HCSR04Driver::HCSR04Driver()
    : _trigPin(0), _echoPin(0), _online(false)
{
}

/* begin - 初始化引脚 */
bool HCSR04Driver::begin(uint8_t trigPin, uint8_t echoPin)
{
    _trigPin = trigPin;
    _echoPin = echoPin;

    pinMode(_trigPin, OUTPUT);
    pinMode(_echoPin, INPUT);

    /* 确保 Trig 初始为低电平 */
    digitalWrite(_trigPin, LOW);

#ifdef DEBUG
    Serial.printf("[HC-SR04] init: Trig=GPIO%d, Echo=GPIO%d\n", _trigPin, _echoPin);
#endif

    return true;
}

/* readDistance - 读取距离(米) */
float HCSR04Driver::readDistance()
{
    /* 1. 发送 10μs 高脉冲到 Trig */
    digitalWrite(_trigPin, LOW);
    delayMicroseconds(2);
    digitalWrite(_trigPin, HIGH);
    delayMicroseconds(10);
    digitalWrite(_trigPin, LOW);

    /* 2. 测量 Echo 引脚高电平脉冲宽度(μs) */
    unsigned long pulseUs = pulseIn(_echoPin, HIGH, HCSR04_TIMEOUT_US);

    /* 3. 判断有效性 */
    if (pulseUs == 0) {
        /* 超时: 无回波 (超出量程或传感器故障) */
        _online = false;
        return -1.0f;
    }

    _online = true;

    /* 4. 计算距离: distance_m = (pulse_us * 0.0343) / 200.0
     *    声速 343m/s = 0.0343 cm/μs
     *    除以 200 = 除以 2 (往返) / 100 (cm→m) */
    float distanceCm = (float)pulseUs * 0.0343f / 2.0f;
    float distanceM  = distanceCm / 100.0f;

    /* 5. 量程校验 (2cm - 400cm) */
    if (distanceCm < 2.0f || distanceCm > (float)HCSR04_MAX_RANGE_CM) {
        _online = false;
        return -1.0f;
    }

    return distanceM;
}
