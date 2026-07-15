/* ============================================================
 * 文件名: hc_sr04_driver.h
 * 功能描述: HC-SR04 超声波测距传感器驱动头文件
 *           替代原来的 Rd-03D 毫米波雷达，用于前向障碍物检测
 *           量程 2cm-400cm，精度 ±3mm
 * 依赖关系: Arduino GPIO 函数、config.h
 * 接口说明:
 *   begin(trigPin, echoPin)  - 初始化 Trig/Echo 引脚
 *   readDistance()            - 返回距离(米)，超时/无效返回 -1.0f
 *   isOnline()                - 查询传感器是否在线(最近一次读取成功)
 * ============================================================ */

#ifndef HC_SR04_DRIVER_H
#define HC_SR04_DRIVER_H

#include <Arduino.h>

class HCSR04Driver {
public:
    HCSR04Driver();

    /* --------------------------------------------------------
     * begin - 初始化 Trig/Echo 引脚
     * 参数: trigPin - Trig 输出引脚号
     *       echoPin - Echo 输入引脚号
     * 返回: true=成功
     * -------------------------------------------------------- */
    bool begin(uint8_t trigPin, uint8_t echoPin);

    /* --------------------------------------------------------
     * readDistance - 读取距离（阻塞式，最多等待 HCSR04_TIMEOUT_US）
     * 返回: 距离(米)，超时/无效返回 -1.0f
     * 原理: Trig发10μs高脉冲 → Echo返回高电平脉宽 → 距离=脉宽*声速/2
     * -------------------------------------------------------- */
    float readDistance();

    /* --------------------------------------------------------
     * isOnline - 传感器是否在线
     * 返回: true=最近一次读取成功
     * -------------------------------------------------------- */
    bool isOnline() const { return _online; }

private:
    uint8_t _trigPin;
    uint8_t _echoPin;
    bool    _online;
};

#endif /* HC_SR04_DRIVER_H */
