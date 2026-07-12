/* ============================================================
 * 文件名: rd03d_driver.cpp
 * 功能描述: RD-03D 毫米波雷达驱动实现
 *           实现 UART 初始化、多目标帧读取与解析、自检逻辑
 * 依赖关系: Arduino Core、config.h、protocol.h、rd03d_driver.h
 * 接口说明: 见头文件
 *
 * 重要说明:
 *   RD-03D 实际串口帧格式请以厂家《RD-03D 协议手册》为准。
 *   本文件 parseFrame() 采用一种常见的毫米波雷达帧格式作为
 *   框架示例（帧头 0xAA 0xFF 0x03 0x00 + 目标数 + 各目标
 *   距离/速度/角度 + CRC16）。若实际手册不同，只需修改
 *   parseFrame() 内的解析逻辑，其余流程保持不变。
 * ============================================================ */

#include "rd03d_driver.h"

#ifdef DEBUG
  #define DBG_PRINT(x)     Serial.print(x)
  #define DBG_PRINTLN(x)   Serial.println(x)
  #define DBG_PRINTF(fmt, ...) Serial.printf(fmt, ##__VA_ARGS__)
#else
  #define DBG_PRINT(x)
  #define DBG_PRINTLN(x)
  #define DBG_PRINTF(fmt, ...)
#endif

/* RD-03D 示例帧格式常量（按常见毫米波雷达协议假设，需以手册为准） */
#define RD03D_HEADER0           0xAA
#define RD03D_HEADER1           0xFF
#define RD03D_HEADER2           0x03
#define RD03D_HEADER3           0x00
#define RD03D_HEADER_LEN        4
#define RD03D_FRAME_MIN_LEN     8     // 帧头4 + 目标数1 + 至少1目标(距离2+速度1) + CRC2

/* 单个目标在帧中的字节数: 距离2B(cm,小端) + 速度1B(有符号,0.1m/s) + 角度1B(有符号,°) */
#define RD03D_TARGET_DATA_LEN   4

/* 构造时未初始化 */
RD03DDriver::RD03DDriver()
    : _uart(nullptr), _online(false), _initialized(false), _lastError(0), _lastReadMs(0)
{
}

/* begin - 初始化雷达 UART */
bool RD03DDriver::begin(HardwareSerial& uart, int txPin, int rxPin, uint32_t baudrate)
{
    if (_initialized) {
        DBG_PRINTLN("[RD03D] already initialized");
        return true;
    }

    _uart = &uart;
    _uart->begin(baudrate, SERIAL_8N1, txPin, rxPin);
    delay(50);  // 等待雷达上电稳定

    _initialized = true;
    _online = false;
    _lastError = 0;

    DBG_PRINTF("[RD03D] initialized, tx=%d rx=%d baud=%lu\n",
               txPin, rxPin, (unsigned long)baudrate);
    return true;
}

/* readTargets - 读取一帧多目标数据 */
bool RD03DDriver::readTargets(RadarTarget_t* targets, uint8_t* count)
{
    if (!_initialized || !targets || !count) {
        _lastError = 1;
        return false;
    }

    *count = 0;

    /* 非阻塞: 数据不足一帧则返回 */
    if (_uart->available() < RD03D_FRAME_MIN_LEN) {
        if (_online && (millis() - _lastReadMs > 2000)) {
            _online = false;
            _lastError = 3;   // 通信超时
            DBG_PRINTLN("[RD03D] offline: no data > 2s");
        }
        return false;
    }

    /* 读取缓冲区 */
    uint8_t buf[256];
    size_t len = _uart->available();
    if (len > sizeof(buf)) len = sizeof(buf);
    _uart->readBytes(buf, len);

    /* 解析帧 */
    if (parseFrame(buf, len, targets, count)) {
        _online = true;
        _lastReadMs = millis();
        _lastError = 0;
#ifdef DEBUG
        DBG_PRINTF("[RD03D] targets=%u\n", *count);
#endif
        return true;
    }

    _lastError = 2;
    return false;
}

/* parseFrame - 解析一帧雷达多目标数据
 * 示例帧:
 *   [0xAA][0xFF][0x03][0x00][N][d_lo][d_hi][v][a] ... (N个目标) [crc_lo][crc_hi]
 *   N: 目标数
 *   d: 距离 cm, 16位小端
 *   v: 径向速度, 有符号, 单位0.1m/s（靠近为正）
 *   a: 角度, 有符号, 单位°（正前0，右正左负）
 *   crc: CRC16-IBM, 小端
 * 注意: 若实际RD-03D协议不同，请在此处按手册修改 */
bool RD03DDriver::parseFrame(const uint8_t* data, size_t len,
                             RadarTarget_t* targets, uint8_t* count)
{
    if (!data || len < RD03D_FRAME_MIN_LEN || !targets || !count) {
        return false;
    }

    /* 查找帧头 0xAA 0xFF 0x03 0x00 */
    size_t offset = 0;
    if (!findFrameHeader(data, len, &offset)) {
        return false;
    }

    const uint8_t* frame = data + offset;

    /* 目标数 */
    uint8_t n = frame[RD03D_HEADER_LEN];
    if (n == 0 || n > RADAR_MAX_TARGETS) {
        return false;
    }

    /* 校验帧总长度: 帧头 + 目标数(1) + N*目标数据 + CRC(2) */
    size_t expectLen = RD03D_HEADER_LEN + 1 + (size_t)n * RD03D_TARGET_DATA_LEN + 2;
    if (offset + expectLen > len) {
        DBG_PRINTF("[RD03D] frame too short: need=%u have=%u\n",
                   (unsigned)(offset + expectLen), (unsigned)len);
        return false;
    }

    /* CRC16 校验（示例: CRC16-IBM, 多项式0x8005）
     * 覆盖范围: 帧头 到 目标数据结束（不含CRC本身） */
    size_t crcDataLen = RD03D_HEADER_LEN + 1 + (size_t)n * RD03D_TARGET_DATA_LEN;
    uint16_t expectedCrc = 0;
    for (size_t i = 0; i < crcDataLen; i++) {
        expectedCrc ^= (uint16_t)frame[i] << 8;
        for (uint8_t b = 0; b < 8; b++) {
            if (expectedCrc & 0x8000) {
                expectedCrc = (expectedCrc << 1) ^ 0x8005;
            } else {
                expectedCrc <<= 1;
            }
        }
    }
    uint16_t actualCrc = (uint16_t)frame[crcDataLen] | ((uint16_t)frame[crcDataLen + 1] << 8);
    if (expectedCrc != actualCrc) {
        DBG_PRINTF("[RD03D] CRC mismatch: expect=0x%04X actual=0x%04X\n",
                   expectedCrc, actualCrc);
        return false;
    }

    /* 逐目标解析 */
    const uint8_t* p = frame + RD03D_HEADER_LEN + 1;
    for (uint8_t i = 0; i < n; i++) {
        uint16_t distCm = (uint16_t)p[0] | ((uint16_t)p[1] << 8);
        int8_t   vRaw   = (int8_t)p[2];     // 有符号速度, 0.1m/s
        int8_t   aRaw   = (int8_t)p[3];     // 有符号角度, °

        targets[i].distance = distCm / 100.0f;        // cm -> m
        targets[i].speed    = vRaw / 10.0f;           // 0.1m/s -> m/s
        targets[i].angle    = (float)aRaw;            // °

        p += RD03D_TARGET_DATA_LEN;
    }

    *count = n;
    return true;
}

/* findFrameHeader - 查找 4 字节帧头 */
bool RD03DDriver::findFrameHeader(const uint8_t* data, size_t len, size_t* outOffset)
{
    if (!data || len < RD03D_HEADER_LEN || !outOffset) {
        return false;
    }
    for (size_t i = 0; i + RD03D_HEADER_LEN <= len; i++) {
        if (data[i] == RD03D_HEADER0 && data[i + 1] == RD03D_HEADER1 &&
            data[i + 2] == RD03D_HEADER2 && data[i + 3] == RD03D_HEADER3) {
            *outOffset = i;
            return true;
        }
    }
    return false;
}

/* selfTest - 传感器自检 */
bool RD03DDriver::selfTest()
{
    if (!_initialized) {
        return false;
    }

    DBG_PRINTLN("[RD03D] self-test start...");
    uint32_t startMs = millis();
    RadarTarget_t targets[RADAR_MAX_TARGETS];
    uint8_t count;

    /* 在超时时间内尝试读到有效帧（即使无目标也算通信正常） */
    while (millis() - startMs < SENSOR_SELFTEST_TIMEOUT_MS) {
        if (readTargets(targets, &count)) {
            DBG_PRINTF("[RD03D] self-test OK, targets=%u\n", count);
            _online = true;
            return true;
        }
        delay(10);
    }

    DBG_PRINTLN("[RD03D] self-test FAIL: timeout");
    _online = false;
    _lastError = 4;
    return false;
}
