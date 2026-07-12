/* ============================================================
 * 文件名: sdm10_driver.h
 * 功能描述: SDM10 激光测距传感器驱动头文件
 *           SDM10 为 UART 接口激光测距模块，量程22m，精度±1cm，5V供电
 *           支持单次/连续测距，本驱动采用被动查询方式读取距离
 * 依赖关系: Arduino Serial1(UART1)、config.h、protocol.h
 * 接口说明:
 *   SDM10Driver()        - 构造函数
 *   begin()              - 初始化UART及传感器
 *   readDistance()       - 读取一次距离(m)
 *   selfTest()           - 传感器自检
 *   isOnline()           - 查询在线状态
 *
 * 注意: SDM10 具体帧格式需以厂家协议手册为准，本驱动提供框架
 *       并预留 parseFrame() 供按手册填充实际解析逻辑
 * ============================================================ */

#ifndef SDM10_DRIVER_H
#define SDM10_DRIVER_H

#include <Arduino.h>
#include "config.h"
#include "protocol.h"

class SDM10Driver {
public:
    /* 构造函数 */
    SDM10Driver();

    /* 析构函数 */
    ~SDM10Driver();

    /* --------------------------------------------------------
     * begin - 初始化 SDM10 激光测距模块
     * 参数: 无（引脚与波特率由 config.h 指定）
     * 返回: true=初始化成功, false=失败
     * 说明: 配置 UART1 引脚与波特率，并尝试使能连续输出模式
     * -------------------------------------------------------- */
    bool begin();

    /* --------------------------------------------------------
     * readDistance - 读取一次测距结果
     * 参数: outDistance - 输出距离(m)，失败时为 SDM10_INVALID_DISTANCE
     * 返回: true=读到有效数据, false=无数据或校验失败
     * 说明: 从 UART 读取一帧并解析，非阻塞（带超时）
     * -------------------------------------------------------- */
    bool readDistance(float& outDistance);

    /* --------------------------------------------------------
     * selfTest - 传感器自检
     * 参数: 无
     * 返回: true=自检通过(收到有效距离), false=自检失败
     * 说明: 上电启动时调用，在 SENSOR_SELFTEST_TIMEOUT_MS 内
     *       等待并验证能否读到有效距离
     * -------------------------------------------------------- */
    bool selfTest();

    /* --------------------------------------------------------
     * isOnline - 查询传感器在线状态
     * 参数: 无
     * 返回: true=在线, false=离线
     * -------------------------------------------------------- */
    bool isOnline() const { return _online; }

    /* --------------------------------------------------------
     * getLastError - 获取最近一次错误码
     * 返回: 0=无错误, 非0=错误码
     * -------------------------------------------------------- */
    uint8_t getLastError() const { return _lastError; }

private:
    /* --------------------------------------------------------
     * parseFrame - 解析一帧测距数据
     * 参数: data - 帧数据缓冲区指针
     *       len  - 数据长度
     *       outDistance - 输出解析得到的距离(m)
     * 返回: true=解析成功, false=帧格式错误
     * 说明: 需按 SDM10 实际协议手册实现具体字节解析
     * -------------------------------------------------------- */
    bool parseFrame(const uint8_t* data, size_t len, float& outDistance);

    /* --------------------------------------------------------
     * findFrameHeader - 在缓冲区中查找帧头位置
     * 参数: data - 缓冲区, len - 长度
     *       outOffset - 输出帧头偏移
     * 返回: true=找到, false=未找到
     * -------------------------------------------------------- */
    bool findFrameHeader(const uint8_t* data, size_t len, size_t* outOffset);

    /* 发送查询命令（若传感器为被动查询模式） */
    bool sendQueryCmd();

    bool    _online;        // 在线状态
    bool    _initialized;   // 初始化标志
    uint8_t _lastError;     // 最近错误码
    uint32_t _lastReadMs;   // 上次成功读取时间戳
};

#endif /* SDM10_DRIVER_H */
