/* ============================================================
 * 文件名: sdm10_driver.cpp
 * 功能描述: SDM10 激光测距传感器驱动实现
 *           实现 UART1 初始化、帧读取与解析、自检逻辑
 * 依赖关系: Arduino Core、config.h、protocol.h、sdm10_driver.h
 * 接口说明: 见头文件
 *
 * 重要说明:
 *   SDM10 实际串口帧格式请以厂家《SDM10 协议手册》为准。
 *   本文件中 parseFrame() 采用一种常见的激光测距帧格式作为
 *   框架示例（帧头 0xAA 0x55 + 距离(2B,cm,小端) + 累加和校验）。
 *   若实际手册不同，只需修改 parseFrame() 内的解析逻辑，
 *   其余流程保持不变。
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

/* SDM10 示例帧格式常量（按常见激光测距协议假设，需以手册为准） */
#define SDM10_FRAME_HEADER0     0xAA
#define SDM10_FRAME_HEADER1     0x55
#define SDM10_FRAME_MIN_LEN     6     // 帧头2 + 距离2 + 校验1 + (保留1)
#define SDM10_QUERY_CMD_LEN     4     // 查询命令长度

/* 构造函数 */
SDM10Driver::SDM10Driver()
    : _online(false), _initialized(false), _lastError(0), _lastReadMs(0)
{
}

/* 析构函数 */
SDM10Driver::~SDM10Driver()
{
    /* Arduino Serial 无需显式释放 */
    _initialized = false;
}

/* begin - 初始化 UART1 与传感器 */
bool SDM10Driver::begin()
{
    if (_initialized) {
        DBG_PRINTLN("[SDM10] already initialized");
        return true;
    }

    /* 配置 UART1: ESP32 Arduino 支持任意 GPIO 映射到 UART */
    SDM10_UART.begin(SDM10_BAUDRATE, SERIAL_8N1, PIN_SDM10_TX, PIN_SDM10_RX);
    delay(50);  // 等待传感器上电稳定

    _initialized = true;
    _online = false;
    _lastError = 0;

    DBG_PRINTLN("[SDM10] UART1 initialized, baud=115200");
    return true;
}

/* readDistance - 读取一次测距结果 */
bool SDM10Driver::readDistance(float& outDistance)
{
    if (!_initialized) {
        _lastError = 1;   // 未初始化
        return false;
    }

    outDistance = SDM10_INVALID_DISTANCE;

    /* 若缓冲区不足一帧，直接返回（非阻塞） */
    if (SDM10_UART.available() < SDM10_FRAME_MIN_LEN) {
        /* 超过2秒未读到数据则置为离线 */
        if (_online && (millis() - _lastReadMs > 2000)) {
            _online = false;
            _lastError = 3;   // 通信超时
            DBG_PRINTLN("[SDM10] offline: no data > 2s");
        }
        return false;
    }

    /* 读取缓冲区数据 */
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
        DBG_PRINTF("[SDM10] dist=%.2f m\n", outDistance);
#endif
        return true;
    }

    _lastError = 2;   // 帧解析失败
    return false;
}

/* parseFrame - 解析一帧测距数据
 * 示例帧: [0xAA][0x55][distLo][distHi][reserved][checksum]
 *   dist: 距离值，单位cm，16位小端
 *   checksum: 前5字节累加和的低8位
 * 注意: 若实际SDM10协议不同，请在此处按手册修改 */
bool SDM10Driver::parseFrame(const uint8_t* data, size_t len, float& outDistance)
{
    if (!data || len < SDM10_FRAME_MIN_LEN) {
        return false;
    }

    /* 查找帧头 */
    size_t offset = 0;
    if (!findFrameHeader(data, len, &offset)) {
        return false;
    }

    /* 剩余长度不足 */
    if (offset + SDM10_FRAME_MIN_LEN > len) {
        return false;
    }

    const uint8_t* frame = data + offset;

    /* 校验和: 累加和（示例） */
    uint8_t expectedSum = sumChecksum(frame, SDM10_FRAME_MIN_LEN - 1);
    uint8_t actualSum = frame[SDM10_FRAME_MIN_LEN - 1];
    if (expectedSum != actualSum) {
        DBG_PRINTF("[SDM10] checksum mismatch: expect=0x%02X actual=0x%02X\n",
                   expectedSum, actualSum);
        return false;
    }

    /* 解析距离: 16位小端，单位cm -> 转换为m */
    uint16_t distCm = (uint16_t)frame[2] | ((uint16_t)frame[3] << 8);

    /* 超量程或无效值判断 */
    if (distCm == 0 || distCm > SDM10_MAX_RANGE_CM) {
        return false;
    }

    outDistance = distCm / 100.0f;   // cm -> m
    return true;
}

/* findFrameHeader - 查找帧头 0xAA 0x55 */
bool SDM10Driver::findFrameHeader(const uint8_t* data, size_t len, size_t* outOffset)
{
    if (!data || len < 2 || !outOffset) {
        return false;
    }
    for (size_t i = 0; i + 1 < len; i++) {
        if (data[i] == SDM10_FRAME_HEADER0 && data[i + 1] == SDM10_FRAME_HEADER1) {
            *outOffset = i;
            return true;
        }
    }
    return false;
}

/* sendQueryCmd - 发送查询命令（若传感器为被动查询模式）
 * 当前采用连续输出模式，无需主动查询，此函数预留扩展 */
bool SDM10Driver::sendQueryCmd()
{
    if (!_initialized) return false;
    /* 预留: 实际查询命令字节以手册为准 */
    uint8_t cmd[SDM10_QUERY_CMD_LEN] = {0xAA, 0x55, 0x01, 0x00};
    cmd[3] = sumChecksum(cmd, 3);
    SDM10_UART.write(cmd, SDM10_QUERY_CMD_LEN);
    return true;
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

    /* 在超时时间内尝试读取有效距离 */
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
    _lastError = 4;   // 自检超时
    return false;
}
