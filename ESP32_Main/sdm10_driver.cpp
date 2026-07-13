/* ============================================================
 * 文件名: sdm10_driver.cpp
 * 功能描述: SDM10 激光测距传感器驱动实现
 *           实现 UART1 初始化、帧读取与解析、自检逻辑
 * 依赖关系: Arduino Core、config.h、protocol.h、sdm10_driver.h
 * 接口说明: 见头文件
 *
 * 协议来源: SDM10/SDM10j 官方数据手册 (siman.asia)
 *
 * UART 配置: 460800bps, 8N1, TTL 5V
 * 帧格式(4字节): [0x5C][DIST_L][DIST_H][XOR_CHK]
 *   - 0x5C:    固定帧头
 *   - DIST_L/H: 距离值, 16位小端, 单位毫米(mm)
 *   - XOR_CHK:  XOR校验 (详见 parseFrame)
 * 输出模式: 上电后自动连续输出 50Hz (无需主动查询)
 * 无效值:   距离 = 65535 (0xFFFF) 表示测量失败
 * ============================================================ */

#include "sdm10_driver.h"

#ifdef DEBUG
  #define DBG_PRINT(x)     Serial.print(x)
  #define DBG_PRINTLN(x)   Serial.println(x)
  #define DBG_PRINTF(fmt, ...) Serial.printf(fmt, ##__VA_ARGS__)
#else
  #define DBG_PRINT(x)
  #define DBG_PRINTLN(x)
  #define DBG_PRINTF(fmt, ...)
#endif

/* 帧格式常量 (来自SDM10数据手册) */
#define SDM10_FRAME_HEADER      0x5C    // 固定帧头
#define SDM10_FRAME_LEN         4       // 总帧长: 帧头1 + 距离2 + 校验1
#define SDM10_DIST_INVALID      65535   // 无效距离标识 (0xFFFF)

/* 构造函数 */
SDM10Driver::SDM10Driver()
    : _online(false), _initialized(false), _lastError(0), _lastReadMs(0)
{
}

/* 析构函数 */
SDM10Driver::~SDM10Driver()
{
    _initialized = false;
}

/* begin - 初始化 UART1 与传感器 */
bool SDM10Driver::begin()
{
    if (_initialized) {
        DBG_PRINTLN("[SDM10] already initialized");
        return true;
    }

    /* UART1: 460800bps 8N1 (SDM10出厂默认) */
    SDM10_UART.begin(SDM10_BAUDRATE, SERIAL_8N1, PIN_SDM10_TX, PIN_SDM10_RX);
    delay(50);  // 等待传感器上电稳定

    _initialized = true;
    _online = false;
    _lastError = 0;

    DBG_PRINTF("[SDM10] UART1 initialized, baud=%lu\n", (unsigned long)SDM10_BAUDRATE);
    return true;
}

/* readDistance - 读取一次测距结果 (非阻塞) */
bool SDM10Driver::readDistance(float& outDistance)
{
    if (!_initialized) {
        _lastError = 1;
        return false;
    }

    outDistance = SDM10_INVALID_DISTANCE;

    /* 缓冲区数据不足一帧时直接返回 */
    if (SDM10_UART.available() < SDM10_FRAME_LEN) {
        if (_online && (millis() - _lastReadMs > 2000)) {
            _online = false;
            _lastError = 3;
            DBG_PRINTLN("[SDM10] offline: no data > 2s");
        }
        return false;
    }

    /* 读取缓冲区 */
    uint8_t buf[64];
    size_t len = SDM10_UART.available();
    if (len > sizeof(buf)) len = sizeof(buf);
    SDM10_UART.readBytes(buf, len);

    /* 解析帧 */
    if (parseFrame(buf, len, outDistance)) {
        _online = true;
        _lastReadMs = millis();
        _lastError = 0;
#ifdef DEBUG
        DBG_PRINTF("[SDM10] dist=%.2f m (%u mm)\n", outDistance,
                   (unsigned)(outDistance * 1000.0f));
#endif
        return true;
    }

    _lastError = 2;
    return false;
}

/* parseFrame - 解析一帧测距数据
 *
 * SDM10 数据手册 V2.0 帧格式 (4字节):
 *   [0x5C][DIST_L][DIST_H][CHK_SUM]
 *
 *   Byte 0:     帧头 0x5C (固定)
 *   Byte 1-2:   距离值, 16位小端, 单位 mm (范围 0~65535)
 *               65535 = 测量失败/超量程
 *   Byte 3:     校验和
 *               计算方法: ~(DIST_L + DIST_H)
 *               (从Byte1到Byte2做和, 然后按位取反)
 *
 * 示例: 5C 02 11 EC
 *   距离: 0x1102 = 4354mm = 4.354m
 *   校验: ~(0x02 + 0x11) = ~0x13 = 0xEC ✓
 *
 * 来源: SDM10 数据手册 V2.0 §5.2, §6 (siman.asia)
 */
bool SDM10Driver::parseFrame(const uint8_t* data, size_t len, float& outDistance)
{
    if (!data || len < SDM10_FRAME_LEN) {
        return false;
    }

    /* 查找帧头 0x5C */
    size_t offset = 0;
    if (!findFrameHeader(data, len, &offset)) {
        return false;
    }

    /* 剩余长度不足一帧 */
    if (offset + SDM10_FRAME_LEN > len) {
        return false;
    }

    const uint8_t* frame = data + offset;

    /* 校验和: ~(DIST_L + DIST_H) — 见 SDM10 数据手册 V2.0 §6 */
    uint8_t expectedChk = ~(frame[1] + frame[2]);
    uint8_t actualChk   = frame[3];
    if (expectedChk != actualChk) {
        DBG_PRINTF("[SDM10] chk fail: expect=0x%02X actual=0x%02X (DIST_L=0x%02X DIST_H=0x%02X)\n",
                   expectedChk, actualChk, frame[1], frame[2]);
        return false;
    }

    /* 解析距离: 16位小端, 单位 mm */
    uint16_t distMm = (uint16_t)frame[1] | ((uint16_t)frame[2] << 8);

    /* 无效值判断 (0 = 未就绪, 65535 = 测量失败) */
    if (distMm == 0 || distMm == SDM10_DIST_INVALID) {
        return false;
    }

    /* 超量程判断 */
    uint16_t maxMm = (uint16_t)(SDM10_MAX_RANGE_CM * 10);
    if (distMm > maxMm) {
        DBG_PRINTF("[SDM10] out of range: %u mm > %u mm\n", distMm, maxMm);
        return false;
    }

    outDistance = distMm / 1000.0f;   // mm -> m
    return true;
}

/* findFrameHeader - 查找帧头 0x5C */
bool SDM10Driver::findFrameHeader(const uint8_t* data, size_t len, size_t* outOffset)
{
    if (!data || len < 1 || !outOffset) {
        return false;
    }
    for (size_t i = 0; i < len; i++) {
        if (data[i] == SDM10_FRAME_HEADER) {
            *outOffset = i;
            return true;
        }
    }
    return false;
}

/* selfTest - 传感器自检 */
bool SDM10Driver::selfTest()
{
    if (!_initialized) {
        return false;
    }

    DBG_PRINTLN("[SDM10] self-test start...");
    uint32_t startMs = millis();
    float dist;

    while (millis() - startMs < SENSOR_SELFTEST_TIMEOUT_MS) {
        if (readDistance(dist) && dist > 0) {
            DBG_PRINTF("[SDM10] self-test OK, dist=%.2f m\n", dist);
            _online = true;
            return true;
        }
        delay(10);
    }

    DBG_PRINTLN("[SDM10] self-test FAIL: timeout");
    _online = false;
    _lastError = 4;
    return false;
}
