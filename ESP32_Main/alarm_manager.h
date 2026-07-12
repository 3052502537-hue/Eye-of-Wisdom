/* ============================================================
 * 文件名: alarm_manager.h
 * 功能描述: 蜂鸣器 + RGB 状态指示管理头文件
 *           蜂鸣器: GPIO PWM 控制，仅极危险情况和设备自检时响
 *           RGB LED: 板载 WS2812(GPIO48)，蓝色=电源，绿色=Wifi连接，
 *                    红色=故障，并根据危险等级切换颜色
 * 依赖关系: Arduino Core(LEDC PWM)、config.h
 * 接口说明:
 *   AlarmManager()        - 构造函数
 *   begin()               - 初始化蜂鸣器与RGB
 *   setBuzzerOn()         - 开启蜂鸣器(指定频率)
 *   setBuzzerOff()        - 关闭蜂鸣器
 *   beep()                - 单次短响
 *   setSystemState()      - 设置系统状态(RGB随之变化)
 *   update()              - 周期调用(用于闪烁等动态效果)
 *   selfTest()            - 自检(蜂鸣器短响+RGB三色循环)
 *
 * 状态对应RGB颜色:
 *   POWER_ON       -> 蓝
 *   INIT           -> 蓝闪
 *   NORMAL         -> 绿
 *   WARNING        -> 黄
 *   DANGER         -> 红闪
 *   FAULT          -> 红
 *   WIFI_CONNECTED -> 绿(常亮)
 * ============================================================ */

#ifndef ALARM_MANAGER_H
#define ALARM_MANAGER_H

#include <Arduino.h>
#include "config.h"

class AlarmManager {
public:
    AlarmManager();
    ~AlarmManager();

    /* --------------------------------------------------------
     * begin - 初始化蜂鸣器PWM与RGB LED
     * 参数: 无
     * 返回: true=成功
     * -------------------------------------------------------- */
    bool begin();

    /* --------------------------------------------------------
     * setBuzzerOn - 开启蜂鸣器
     * 参数: freqHz - 频率(Hz)
     * 返回: 无
     * -------------------------------------------------------- */
    void setBuzzerOn(uint32_t freqHz = BUZZER_LEDC_FREQ_HZ);

    /* --------------------------------------------------------
     * setBuzzerOff - 关闭蜂鸣器
     * -------------------------------------------------------- */
    void setBuzzerOff();

    /* --------------------------------------------------------
     * beep - 单次短响
     * 参数: freqHz    - 频率
     *       durationMs - 持续时间(ms)
     * 返回: 无
     * 说明: 阻塞式短响，用于自检
     * -------------------------------------------------------- */
    void beep(uint32_t freqHz = 2000, uint32_t durationMs = 100);

    /* --------------------------------------------------------
     * setDangerAlarm - 设置危险报警(连续蜂鸣)
     * 参数: on - true=开启危险报警(红闪+蜂鸣), false=关闭
     * -------------------------------------------------------- */
    void setDangerAlarm(bool on);

    /* --------------------------------------------------------
     * setSystemState - 设置系统状态(RGB随之变化)
     * 参数: state - 系统状态(见 config.h SYS_STATE_*)
     * -------------------------------------------------------- */
    void setSystemState(uint8_t state);

    /* --------------------------------------------------------
     * update - 周期调用，驱动闪烁等动态效果
     * 参数: 无
     * 返回: 无
     * 说明: 应在主循环或决策任务中周期调用(约10Hz)
     * -------------------------------------------------------- */
    void update();

    /* --------------------------------------------------------
     * selfTest - 自检: 蜂鸣器短响 + RGB三色循环
     * 参数: 无
     * 返回: true=自检完成
     * -------------------------------------------------------- */
    bool selfTest();

    /* 直接设置RGB颜色(0-255) */
    void setRgbColor(uint8_t r, uint8_t g, uint8_t b);

private:
    bool    _initialized;
    uint8_t _curState;          // 当前系统状态
    bool    _dangerAlarm;       // 危险报警标志
    uint32_t _lastToggleMs;     // 上次闪烁切换时间
    bool    _toggleOn;          // 闪烁当前相位
};

#endif /* ALARM_MANAGER_H */
