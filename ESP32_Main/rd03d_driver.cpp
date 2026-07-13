/* ============================================================
 * 文件名: rd03d_driver.cpp
 * 功能描述: RD-03D 毫米波雷达驱动实现
 *           实现 UART 初始化、多目标帧读取与解析、自检逻辑
 * 依赖关系: Arduino Core、config.h、protocol.h、rd03d_driver.h
 * 接口说明: 见头文件
 *
 * 协议来源: Ai-Thinker RD-03D 多目标轨迹跟踪用户手册 V1.0.1
 *
 * 关键参数 (来自官方数据手册):
 *   - 芯片:    S5KM312CL, 24GHz FMCW
 *   - 波特率:  256000bps 8N1 (出厂默认)
 *   - 检测:    仅运动目标 (FMCW多普勒效应)
 *   - 距离:    0.5m ~ 8m
 *   - 角度:    ±60° 水平, ±30° 俯仰
 *   - 目标数:  最多3个
 *   - 数据刷新率: 10Hz
 *   - 距离分辨率: 0.75m (单个距离门36cm, 共23门)
 *
 * 坐标系 (数据手册 Fig 5-1):
 *   X: 横向偏移 (mm, 正=右, 负=左)
 *   Y: 纵向距离 (mm, 正前方)
 *   Speed: 径向速度 (cm/s, 正=远离, 负=靠近)
 *
 * 数据帧格式 (数据手册 §5, 表5-1/5-2):
 *   帧头(4B): AA FF 03 00
 *   目标1(8B): X(2B LE) + Y(2B LE) + Speed(2B LE) + PixelDist(2B LE)
 *   目标2(8B): 同上
 *   目标3(8B): 同上
 *   帧尾(2B): 55 CC
 *   总长: 30 字节
 *
 *   坐标/速度编码 (非标准! 见数据手册表5-2):
 *     signed int16, MSB=1→正值, MSB=0→负值
 *     绝对值 = raw & 0x7FFF (低15位)
 *     非标准二进制补码!
 *
 * 命令帧格式 (用于配置雷达参数):
 *   帧头: FD FC FB FA
 *   帧尾: 04 03 02 01
 *
 * 注意: ⚠️ RD-03D 仅检测运动目标 (FMCW多普勒原理).
 *   静止障碍物不会出现在雷达输出中, 需依赖激光测距.
 * ============================================================ */

#include "rd03d_driver.h"
#include <math.h>

#ifdef DEBUG
  #define DBG_PRINT(x)     Serial.print(x)
  #define DBG_PRINTLN(x)   Serial.println(x)
  #define DBG_PRINTF(fmt, ...) Serial.printf(fmt, ##__VA_ARGS__)
#else
  #define DBG_PRINT(x)
  #define DBG_PRINTLN(x)
  #define DBG_PRINTF(fmt, ...)
#endif

/* ============================================================
 * RD-03D 帧格式常量 (来自用户手册 V1.0.1 §5)
 *
 * 数据输出帧 (雷达自动连续输出, 10Hz):
 *   帧头: 4字节 AA FF 03 00
 *   数据区: 目标1(8B) + 目标2(8B) + 目标3(8B) = 24字节
 *     每目标: X坐标(2B LE, signed mag) + Y坐标(2B LE, signed mag)
 *            + 速度(2B LE, signed mag, cm/s) + 像素距离(2B LE, uint, mm)
 *   帧尾: 2字节 55 CC
 *   总帧长: 4 + 24 + 2 = 30 字节
 *   空目标槽: 连续8字节 0x00
 *
 * 命令帧 (配置雷达参数):
 *   帧头: FD FC FB FA
 *   数据长度: 2B LE
 *   命令字: 2B LE
 *   命令值: NB
 *   帧尾: 04 03 02 01
 * ============================================================ */

/* 数据帧 */
#define RD03D_DATA_HDR0         0xAA
#define RD03D_DATA_HDR1         0xFF
#define RD03D_DATA_HDR2         0x03
#define RD03D_DATA_HDR3         0x00
#define RD03D_DATA_FTR0         0x55
#define RD03D_DATA_FTR1         0xCC
#define RD03D_FRAME_LEN         30      // 完整数据帧长度 (4+24+2)
#define RD03D_TARGET_SLOT_LEN   8       // 每目标槽长度 (X+Y+Speed+Pixel)
#define RD03D_TARGET_SLOTS      3       // 目标槽数量

/* 命令帧 */
#define RD03D_CMD_HEADER0       0xFD
#define RD03D_CMD_HEADER1       0xFC
#define RD03D_CMD_HEADER2       0xFB
#define RD03D_CMD_HEADER3       0xFA
#define RD03D_CMD_TAIL0         0x04
#define RD03D_CMD_TAIL1         0x03
#define RD03D_CMD_TAIL2         0x02
#define RD03D_CMD_TAIL3         0x01

/* 构造时未初始化 */
RD03DDriver::RD03DDriver()
    : _uart(nullptr), _online(false), _initialized(false),
      _lastError(0), _lastReadMs(0)
{
}

/* begin - 初始化雷达 UART (256000bps 8N1) */
bool RD03DDriver::begin(HardwareSerial& uart, int txPin, int rxPin, uint32_t baudrate)
{
    if (_initialized) {
        DBG_PRINTLN("[RD03D] already initialized");
        return true;
    }

    _uart = &uart;
    _uart->begin(baudrate, SERIAL_8N1, txPin, rxPin);
    delay(100);  // 等待雷达上电稳定 (RD-03D 需较长时间)

    _initialized = true;
    _online = false;
    _lastError = 0;

    DBG_PRINTF("[RD03D] initialized, tx=%d rx=%d baud=%lu\n",
               txPin, rxPin, (unsigned long)baudrate);
    return true;
}

/* readTargets - 读取一帧多目标数据 (非阻塞)
 *
 * RD-03D 仅检测运动目标 (FMCW多普勒效应), 静止物体不输出.
 * 如果前方有静止障碍物, 需依赖激光测距.
 */
bool RD03DDriver::readTargets(RadarTarget_t* targets, uint8_t* count)
{
    if (!_initialized || !targets || !count) {
        _lastError = 1;
        return false;
    }

    *count = 0;

    /* 非阻塞: 数据不足一帧则返回 */
    if (_uart->available() < RD03D_FRAME_LEN) {
        if (_online && (millis() - _lastReadMs > 2000)) {
            _online = false;
            _lastError = 3;
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
        for (uint8_t i = 0; i < *count; i++) {
            DBG_PRINTF("[RD03D] T%u: d=%.2fm v=%.1fm/s a=%.1f°\n",
                       i, targets[i].distance, targets[i].speed, targets[i].angle);
        }
#endif
        return true;
    }

    _lastError = 2;
    return false;
}

/* ============================================================
 * parseFrame - 解析 RD-03D 数据帧
 *
 * 帧格式 (用户手册 V1.0.1 §5, 已验证):
 *
 *   Offset  Size  Field
 *   ------  ----  -----
 *   0       4     帧头 AA FF 03 00
 *   4       8     目标1: X(2B)+Y(2B)+Speed(2B)+Pixel(2B)
 *   12      8     目标2: 同上
 *   20      8     目标3: 同上
 *   28      2     帧尾 55 CC
 *   总长: 30 字节
 *
 * 坐标/速度编码 (非标准! 符号-幅度+反相符号位):
 *   - 将2字节按LE解析为uint16
 *   - MSB(bit15)=1 → 正, MSB(bit15)=0 → 负
 *   - 绝对值 = raw & 0x7FFF (低15位)
 *   - 这不是标准二进制补码! 直接用 (int16_t)raw 会得到错误结果
 *
 * 空目标检测:
 *   - 目标槽X和Y均为0 → 该槽为空(无目标)
 *   - PixelDist为0也可辅助判断
 *
 * 距离/角度计算:
 *   距离(m) = sqrt(X² + Y²) / 1000.0
 *   角度(°) = atan2(X, Y) * 180.0 / PI  (右正左负)
 *
 * 速度转换:
 *   原始单位: cm/s (数据手册确认)
 *   输出单位: m/s  (RadarTarget_t.speed)
 *   符号转换: 原始正=远离→输出负(靠近为正)
 *
 * 示例帧 (来自数据手册):
 *   AA FF 03 00 0E 03 B1 86 10 00 68 01
 *   00 00 00 00 00 00 00 00
 *   00 00 00 00 00 00 00 00 55 CC
 *
 *   目标1: X=782→-782mm, Y=34481→+1713mm,
 *          Speed=16→-16cm/s, Pixel=360mm → 距离≈1.88m, 角度≈-24.5°
 *   目标2,3: 空
 * ============================================================ */

/* 解码 RD-03D 特有的符号-幅度编码 (MSB=1→正, MSB=0→负) */
static inline int16_t rd03d_decode_s16(uint16_t raw)
{
    int16_t absVal = (int16_t)(raw & 0x7FFF);
    return (raw & 0x8000) ? absVal : -absVal;
}

bool RD03DDriver::parseFrame(const uint8_t* data, size_t len,
                             RadarTarget_t* targets, uint8_t* count)
{
    if (!data || len < RD03D_FRAME_LEN || !targets || !count) {
        return false;
    }

    /* 搜索数据帧头 AA FF 03 00 */
    size_t offset = 0;
    if (!findFrameHeader(data, len, &offset)) {
        return false;
    }

    /* 确保从帧头开始有完整的一帧 */
    if (offset + RD03D_FRAME_LEN > len) {
        return false;
    }

    const uint8_t* frame = data + offset;

    /* 验证帧尾 55 CC */
    if (frame[28] != RD03D_DATA_FTR0 || frame[29] != RD03D_DATA_FTR1) {
        DBG_PRINTF("[RD03D] footer mismatch: got 0x%02X 0x%02X, expected 0x55 0xCC\n",
                   frame[28], frame[29]);
        return false;
    }

    /* 解析3个目标槽 */
    uint8_t targetCount = 0;

    for (uint8_t slot = 0; slot < RD03D_TARGET_SLOTS; slot++) {
        const uint8_t* t = frame + 4 + (slot * RD03D_TARGET_SLOT_LEN);

        /* 读取各字段 (2B LE) */
        uint16_t rawX     = (uint16_t)t[0] | ((uint16_t)t[1] << 8);
        uint16_t rawY     = (uint16_t)t[2] | ((uint16_t)t[3] << 8);
        uint16_t rawSpeed = (uint16_t)t[4] | ((uint16_t)t[5] << 8);
        uint16_t rawPixel = (uint16_t)t[6] | ((uint16_t)t[7] << 8);

        /* 空槽检测: X和Y均为0表示无目标 */
        if (rawX == 0 && rawY == 0) {
            continue;
        }

        /* 解码 RD-03D 特有编码 → 标准有符号整数 */
        int16_t x_mm = rd03d_decode_s16(rawX);
        int16_t y_mm = rd03d_decode_s16(rawY);
        int16_t spd_cm_s = rd03d_decode_s16(rawSpeed);
        (void)rawPixel;  // 像素距离暂不使用, 保留供后续滤波使用

        /* 有效性过滤 */
        /* Y必须 > 500mm (最小检测距离0.5m) 且 < 8000mm (最大8m) */
        if (y_mm < 500 || y_mm > 8000) {
            continue;
        }
        /* X必须在 ±60° 范围内 (|X| < Y * tan60° ≈ Y * 1.73) */
        if (abs(x_mm) > abs(y_mm) * 2) {
            continue;
        }

        /* 计算距离和角度 */
        float distM = sqrtf((float)(x_mm * x_mm + y_mm * y_mm)) / 1000.0f;
        float angleDeg = atan2f((float)x_mm, (float)y_mm) * 180.0f / (float)M_PI;

        /* 速度: cm/s → m/s, 取反(原始正=远离, 输出正=靠近) */
        float speedMs = -(float)spd_cm_s / 100.0f;

        targets[targetCount].distance = distM;
        targets[targetCount].speed    = speedMs;
        targets[targetCount].angle    = angleDeg;
        targetCount++;

        if (targetCount >= RADAR_MAX_TARGETS) break;
    }

    if (targetCount == 0) {
        return false;  // 帧格式正确但无有效目标(所有槽为空)
    }

    *count = targetCount;
    return true;
}

/* findFrameHeader - 搜索数据帧头 AA FF 03 00
 *
 * 注意: 命令帧头为 FD FC FB FA, 与数据帧头不同.
 *       这里只搜索数据帧头.
 *       如果在数据流中搜到命令帧头，忽略之(命令帧走单独的处理路径).
 */
bool RD03DDriver::findFrameHeader(const uint8_t* data, size_t len, size_t* outOffset)
{
    if (!data || len < 4 || !outOffset) {
        return false;
    }

    for (size_t i = 0; i + 4 <= len; i++) {
        if (data[i] == RD03D_DATA_HDR0 && data[i+1] == RD03D_DATA_HDR1 &&
            data[i+2] == RD03D_DATA_HDR2 && data[i+3] == RD03D_DATA_HDR3) {
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
