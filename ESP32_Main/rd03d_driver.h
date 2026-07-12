/* ============================================================
 * 文件名: rd03d_driver.h
 * 功能描述: RD-03D 毫米波雷达驱动头文件
 *           RD-03D 为 24GHz 毫米波雷达，UART接口，支持测距/测角/测速
 *           多目标检测。本驱动用于前方/后方两路雷达（实例化两个对象）
 * 依赖关系: Arduino Serial(UART)、config.h、protocol.h
 * 接口说明:
 *   RD03DDriver()        - 构造函数
 *   begin()              - 初始化指定UART
 *   readTargets()        - 读取一帧多目标数据
 *   selfTest()           - 传感器自检
 *   isOnline()           - 查询在线状态
 *
 * 注意: RD-03D 具体帧格式以厂家协议手册为准，parseFrame() 预留
 *       按手册填充实际解析逻辑
 * ============================================================ */

#ifndef RD03D_DRIVER_H
#define RD03D_DRIVER_H

#include <Arduino.h>
#include "config.h"
#include "protocol.h"

class RD03DDriver {
public:
    /* --------------------------------------------------------
     * begin - 初始化 RD-03D 雷达
     * 参数: uart     - HardwareSerial 引用(Serial0/Serial1/Serial2)
     *       txPin    - ESP32 接收引脚(接雷达TX)
     *       rxPin    - ESP32 发送引脚(接雷达RX)
     *       baudrate - 波特率
     * 返回: true=成功, false=失败
     * 说明: 支持前方/后方两路雷达分别用不同UART实例化
     * -------------------------------------------------------- */
    bool begin(HardwareSerial& uart, int txPin, int rxPin, uint32_t baudrate = RADAR_BAUDRATE);

    /* --------------------------------------------------------
     * readTargets - 读取一帧多目标数据
     * 参数: targets - 输出目标数组(调用方分配，容量 RADAR_MAX_TARGETS)
     *       count   - 输出目标数量
     * 返回: true=读到有效帧, false=无数据或校验失败
     * -------------------------------------------------------- */
    bool readTargets(RadarTarget_t* targets, uint8_t* count);

    /* --------------------------------------------------------
     * selfTest - 传感器自检
     * 返回: true=自检通过, false=失败
     * -------------------------------------------------------- */
    bool selfTest();

    /* 查询在线状态 */
    bool isOnline() const { return _online; }

    /* 获取最近错误码 */
    uint8_t getLastError() const { return _lastError; }

private:
    /* 解析一帧雷达数据 */
    bool parseFrame(const uint8_t* data, size_t len,
                    RadarTarget_t* targets, uint8_t* count);

    /* 查找帧头 */
    bool findFrameHeader(const uint8_t* data, size_t len, size_t* outOffset);

    HardwareSerial* _uart;       // UART 引用
    bool    _online;             // 在线状态
    bool    _initialized;        // 初始化标志
    uint8_t _lastError;          // 最近错误码
    uint32_t _lastReadMs;        // 上次成功读取时间戳
};

#endif /* RD03D_DRIVER_H */
