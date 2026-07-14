/* ============================================================
 * 文件名: sdm10_driver.h
 * 功能描述: SDM10 激光测距传感器驱动头文件
 *           UART接口, 量程10m(标准版), 精度±5cm(<5m)/1%(≥5m), 5V供电
 *           上电自动连续输出(50Hz), 无需主动查询
 * 依赖关系: Arduino Serial1(UART1)、config.h、protocol.h
 * 接口说明:
 *   SDM10Driver()        - 构造函数
 *   begin()              - 初始化UART(460800bps 8N1)
 *   readDistance()       - 读取一次距离(m)
 *   selfTest()           - 传感器自检
 *   isOnline()           - 查询在线状态
 *
 * 协议: [0x5C][DIST_L][DIST_H][CHK_SUM] 4字节, 距离16位LE(mm)
 *       校验: ~(DIST_L + DIST_H)
 * 来源: SDM10 数据手册 V2.0 (siman.asia)
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

    bool    _online;        // 在线状态
    bool    _initialized;   // 初始化标志
    uint8_t _lastError;     // 最近错误码
    uint32_t _lastReadMs;   // 上次成功读取时间戳

    /* 滑动窗口滤波 (5样本) */
    static const uint8_t FILTER_WINDOW = 5;
    float   _filterBuf[5];  // 环形缓冲
    uint8_t _filterIdx;     // 当前写入位置
    uint8_t _filterCount;   // 已填充样本数
    float   _filterSum;     // 累加和(避免每次重算)
};

#endif /* SDM10_DRIVER_H */
