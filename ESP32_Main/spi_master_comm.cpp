/* ============================================================
 * 文件名: spi_master_comm.cpp
 * 功能描述: SPI 主机通信实现
 *           实现 SPI 主机初始化、数据就绪中断处理、帧读取与重组、
 *           控制命令发送、通信自检
 * 依赖关系: Arduino SPI 库、config.h、protocol.h、spi_master_comm.h
 * 接口说明: 见头文件
 *
 * 工作流程:
 *   1. 摄像头板准备好数据后拉低 DATA_READY 引脚
 *   2. 主控 GPIO 中断触发，置 _dataReadyFlag
 *   3. SPI 任务检测到标志，调用 spiTransfer 读取数据
 *   4. 解析 SPI 帧，按 帧起始/数据/结束 重组完整 JPEG
 *   5. 重组完成置 _frameReady，供 WiFi 任务取用 UDP 发送
 * ============================================================ */

#include "spi_master_comm.h"

#ifdef DEBUG
  #define DBG_PRINT(x)     Serial.print(x)
  #define DBG_PRINTLN(x)   Serial.println(x)
  #define DBG_PRINTF(fmt, ...) Serial.printf(fmt, ##__VA_ARGS__)
#else
  #define DBG_PRINT(x)
  #define DBG_PRINTLN(x)
  #define DBG_PRINTF(fmt, ...)
#endif

/* 静态成员初始化 */
volatile bool SpiMasterComm::_dataReadyFlag = false;

/* 静态 ISR 回调（需为静态函数） */
static void IRAM_ATTR onDataReadyIsr()
{
    SpiMasterComm::_dataReadyFlag = true;
}

/* 构造函数 */
SpiMasterComm::SpiMasterComm()
    : _spi(nullptr), _spiMutex(nullptr), _initialized(false),
      _frameReady(false), _frameInProg(false),
      _imgRecvLen(0), _imgExpectLen(0),
      _imgWidth(0), _imgHeight(0), _imgFormat(0),
      _imgBlockCount(0), _frameId(0)
{
    memset(_imgBuf, 0, sizeof(_imgBuf));
    memset(&_latestFrame, 0, sizeof(_latestFrame));
}

/* 析构函数 */
SpiMasterComm::~SpiMasterComm()
{
    if (_spi) {
        delete _spi;
        _spi = nullptr;
    }
    if (_spiMutex) {
        vSemaphoreDelete(_spiMutex);
        _spiMutex = nullptr;
    }
    _initialized = false;
}

/* begin - 初始化 SPI 主机与中断 */
bool SpiMasterComm::begin()
{
    if (_initialized) {
        DBG_PRINTLN("[SPI] already initialized");
        return true;
    }

    /* 创建 SPI 总线互斥锁 (防止读/写并发冲突) */
    _spiMutex = xSemaphoreCreateMutex();
    if (!_spiMutex) {
        DBG_PRINTLN("[SPI] mutex create FAIL");
        return false;
    }

    /* 创建 SPI 实例并初始化引脚
     * 注意: 必须用 HSPI(SPI3_HOST), 不可用 FSPI(SPI2_HOST)!
     *       N16R8的OPI PSRAM独占SPI2, 若用FSPI会破坏PSRAM导致全系统崩溃 */
    _spi = new SPIClass(HSPI);
    _spi->begin(PIN_SPI_SCK, PIN_SPI_MISO, PIN_SPI_MOSI, PIN_SPI_CS);

    /* CS 引脚配置为输出，默认拉高(空闲) */
    pinMode(PIN_SPI_CS, OUTPUT);
    digitalWrite(PIN_SPI_CS, HIGH);

    /* 数据就绪中断引脚: 输入上拉，下降沿触发 */
    pinMode(PIN_SPI_DATA_READY, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(PIN_SPI_DATA_READY),
                    onDataReadyIsr, FALLING);

    _initialized = true;
    DBG_PRINTF("[SPI] master init OK, SCK=%d MISO=%d MOSI=%d CS=%d DRDY=%d\n",
               PIN_SPI_SCK, PIN_SPI_MISO, PIN_SPI_MOSI, PIN_SPI_CS,
               PIN_SPI_DATA_READY);
    return true;
}

/* readFrameIfReady - 响应中断并读取重组一帧 */
bool SpiMasterComm::readFrameIfReady()
{
    if (!_initialized) return false;

    /* 检查数据就绪标志 */
    if (!_dataReadyFlag) {
        return false;
    }
    _dataReadyFlag = false;   // 清标志

    /* 获取 SPI 总线锁 (防止与 sendCommand 并发) */
    if (xSemaphoreTake(_spiMutex, pdMS_TO_TICKS(10)) != pdTRUE) {
        return false;  // 总线忙, 下次再试
    }

    /* 读取一帧原始数据 (static 避免 4KB 栈分配导致溢出) */
    static uint8_t rawBuf[SPI_FRAME_OVERHEAD + SPI_MAX_DATA_LEN];
    size_t rawLen = spiTransfer(rawBuf, sizeof(rawBuf));

    xSemaphoreGive(_spiMutex);  // 释放总线

    if (rawLen == 0) {
        return false;
    }

    /* 解析 SPI 帧 */
    SpiFrame_t frame;
    if (!parseSpiFrame(rawBuf, rawLen, &frame)) {
        DBG_PRINTLN("[SPI] frame parse fail");
        return false;
    }

    /* 按命令分发处理 */
    handleFrame(&frame);
    return true;
}

/* spiTransfer - 拉低CS并通过SPI读取数据 */
size_t SpiMasterComm::spiTransfer(uint8_t* outBuf, size_t len)
{
    if (!_spi || !outBuf || len == 0) return 0;

    /* 限制单次读取长度 */
    if (len > SPI_DMA_BUFFER_SIZE) len = SPI_DMA_BUFFER_SIZE;

    SPISettings settings(SPI_CLOCK_SPEED, MSBFIRST, SPI_MODE0);
    _spi->beginTransaction(settings);
    digitalWrite(PIN_SPI_CS, LOW);   // 选中从机

    /* 全双工: 发送哑元0x00，读取从机返回数据 */
    memset(outBuf, 0x00, len);
    _spi->transferBytes(outBuf, outBuf, len);

    digitalWrite(PIN_SPI_CS, HIGH);  // 释放从机
    _spi->endTransaction();

    return len;
}

/* parseSpiFrame - 解析 SPI 帧
 * 帧结构: |AA 55|cmd|lenLo lenHi|data...|crc|55 AA| */
bool SpiMasterComm::parseSpiFrame(const uint8_t* raw, size_t rawLen, SpiFrame_t* frame)
{
    if (!raw || !frame || rawLen < SPI_FRAME_OVERHEAD) {
        return false;
    }

    /* 校验帧头 */
    if (raw[0] != SPI_FRAME_HEADER0 || raw[1] != SPI_FRAME_HEADER1) {
        return false;
    }

    /* 提取命令与长度 */
    frame->cmd = raw[2];
    frame->dataLen = (uint16_t)raw[3] | ((uint16_t)raw[4] << 8);

    /* 长度合法性检查 */
    if (frame->dataLen > SPI_MAX_DATA_LEN) {
        return false;
    }

    /* 校验总长度 */
    size_t expectTotal = SPI_FRAME_HEADER_LEN + SPI_FRAME_CMD_LEN +
                         SPI_FRAME_LEN_LEN + frame->dataLen +
                         SPI_FRAME_CRC_LEN + SPI_FRAME_TAIL_LEN;
    if (expectTotal > rawLen) {
        return false;
    }

    /* 提取数据 */
    if (frame->dataLen > 0) {
        memcpy(frame->data, raw + SPI_FRAME_HEADER_LEN + SPI_FRAME_CMD_LEN + SPI_FRAME_LEN_LEN,
               frame->dataLen);
    }

    /* CRC 校验: 覆盖 cmd+len+data */
    size_t crcDataLen = SPI_FRAME_CMD_LEN + SPI_FRAME_LEN_LEN + frame->dataLen;
    uint8_t expectedCrc = crc8(raw + SPI_FRAME_HEADER_LEN, crcDataLen);
    uint8_t actualCrc = raw[SPI_FRAME_HEADER_LEN + crcDataLen];
    if (expectedCrc != actualCrc) {
        DBG_PRINTF("[SPI] CRC fail: expect=0x%02X actual=0x%02X\n",
                   expectedCrc, actualCrc);
        return false;
    }

    /* 校验帧尾 */
    size_t tailOffset = SPI_FRAME_HEADER_LEN + crcDataLen + SPI_FRAME_CRC_LEN;
    if (raw[tailOffset] != SPI_FRAME_TAIL0 || raw[tailOffset + 1] != SPI_FRAME_TAIL1) {
        return false;
    }

    return true;
}

/* handleFrame - 按命令分发处理帧 */
void SpiMasterComm::handleFrame(const SpiFrame_t* frame)
{
    if (!frame) return;

    switch (frame->cmd) {
    case SPI_CMD_IMG_FRAME_START: {
        /* 图像帧起始: 解析分辨率/格式/总大小 */
        if (frame->dataLen >= sizeof(ImgFrameStart_t)) {
            ImgFrameStart_t start;
            memcpy(&start, frame->data, sizeof(ImgFrameStart_t));
            _imgWidth      = start.width;
            _imgHeight     = start.height;
            _imgFormat     = start.format;
            _imgExpectLen  = start.totalSize;
            _imgRecvLen    = 0;
            _imgBlockCount = 0;
            _frameInProg   = true;
            _frameReady    = false;
            DBG_PRINTF("[SPI] frame start %dx%d fmt=%u size=%lu\n",
                       _imgWidth, _imgHeight, _imgFormat, (unsigned long)_imgExpectLen);
        }
        break;
    }

    case SPI_CMD_IMG_FRAME_DATA: {
        /* 图像帧数据块: 解析块序号+块大小+数据 */
        if (!_frameInProg || frame->dataLen < sizeof(ImgFrameData_t)) break;
        ImgFrameData_t blk;
        memcpy(&blk, frame->data, sizeof(ImgFrameData_t));
        const uint8_t* payload = frame->data + sizeof(ImgFrameData_t);
        size_t payloadLen = frame->dataLen - sizeof(ImgFrameData_t);

        /* 拷贝到帧缓冲(越界保护) */
        if (_imgRecvLen + payloadLen <= sizeof(_imgBuf)) {
            memcpy(_imgBuf + _imgRecvLen, payload, payloadLen);
            _imgRecvLen += payloadLen;
            _imgBlockCount++;
        } else {
            DBG_PRINTLN("[SPI] img buf overflow, drop frame");
            _frameInProg = false;
        }
        break;
    }

    case SPI_CMD_IMG_FRAME_END: {
        /* 图像帧结束: 校验块数，置就绪标志 */
        if (!_frameInProg) break;
        _frameInProg = false;
        _frameId++;

        _latestFrame.frameId   = _frameId;
        _latestFrame.width     = _imgWidth;
        _latestFrame.height    = _imgHeight;
        _latestFrame.format    = _imgFormat;
        _latestFrame.size      = _imgRecvLen;
        _latestFrame.data      = _imgBuf;
        _latestFrame.timestamp = millis();
        _frameReady = true;

        DBG_PRINTF("[SPI] frame end id=%lu size=%lu blocks=%u\n",
                   (unsigned long)_frameId, (unsigned long)_imgRecvLen, _imgBlockCount);
        break;
    }

    case SPI_CMD_HEARTBEAT:
        DBG_PRINTLN("[SPI] heartbeat");
        break;

    default:
        DBG_PRINTF("[SPI] unknown cmd=0x%02X\n", frame->cmd);
        break;
    }
}

/* getLatestFrame - 获取最新完整帧 */
bool SpiMasterComm::getLatestFrame(ImageFrame_t* outFrame)
{
    if (!outFrame || !_frameReady) return false;
    *outFrame = _latestFrame;
    _frameReady = false;   // 取用后清标志
    return true;
}

/* sendFrame - 构造并发送一帧 SPI 帧 */
bool SpiMasterComm::sendFrame(uint8_t cmd, const uint8_t* payload, uint16_t len)
{
    if (!_spi || !_spiMutex || len > SPI_MAX_DATA_LEN) return false;

    /* 获取 SPI 总线锁 (防止与 readFrameIfReady 并发) */
    if (xSemaphoreTake(_spiMutex, pdMS_TO_TICKS(50)) != pdTRUE) {
        DBG_PRINTLN("[SPI] sendFrame: bus busy");
        return false;
    }

    /* 构造帧缓冲 (static 避免 4KB 栈分配) */
    static uint8_t buf[SPI_FRAME_OVERHEAD + SPI_MAX_DATA_LEN];
    size_t idx = 0;

    /* 帧头 */
    buf[idx++] = SPI_FRAME_HEADER0;
    buf[idx++] = SPI_FRAME_HEADER1;

    /* 命令 */
    buf[idx++] = cmd;

    /* 长度(小端) */
    buf[idx++] = (uint8_t)(len & 0xFF);
    buf[idx++] = (uint8_t)((len >> 8) & 0xFF);

    /* 数据 */
    if (len > 0 && payload) {
        memcpy(buf + idx, payload, len);
        idx += len;
    }

    /* CRC8 (覆盖 cmd+len+data) */
    size_t crcDataLen = SPI_FRAME_CMD_LEN + SPI_FRAME_LEN_LEN + len;
    buf[idx++] = crc8(buf + SPI_FRAME_HEADER_LEN, crcDataLen);

    /* 帧尾 */
    buf[idx++] = SPI_FRAME_TAIL0;
    buf[idx++] = SPI_FRAME_TAIL1;

    /* 发送 */
    SPISettings settings(SPI_CLOCK_SPEED, MSBFIRST, SPI_MODE0);
    _spi->beginTransaction(settings);
    digitalWrite(PIN_SPI_CS, LOW);
    _spi->transferBytes(buf, nullptr, idx);
    digitalWrite(PIN_SPI_CS, HIGH);
    _spi->endTransaction();

    xSemaphoreGive(_spiMutex);  // 释放总线

    return true;
}

/* sendCommand - 向摄像头板发送控制命令(对外封装) */
bool SpiMasterComm::sendCommand(uint8_t cmd, const uint8_t* payload, uint16_t len)
{
    return sendFrame(cmd, payload, len);
}

/* selfTest - 通信自检: 发送心跳并等待应答 */
bool SpiMasterComm::selfTest()
{
    if (!_initialized) return false;

    DBG_PRINTLN("[SPI] self-test: send heartbeat...");
    /* 发送心跳命令 */
    uint32_t ts = millis();
    if (!sendFrame(SPI_CMD_HEARTBEAT, (uint8_t*)&ts, sizeof(ts))) {
        return false;
    }

    /* 等待摄像头板应答(通过数据就绪中断) */
    uint32_t startMs = millis();
    while (millis() - startMs < SENSOR_SELFTEST_TIMEOUT_MS) {
        if (_dataReadyFlag) {
            _dataReadyFlag = false;
            uint8_t buf[64];
            size_t n = spiTransfer(buf, sizeof(buf));
            SpiFrame_t frame;
            if (n > 0 && parseSpiFrame(buf, n, &frame)) {
                DBG_PRINTLN("[SPI] self-test OK: got response");
                return true;
            }
        }
        delay(5);
    }

    DBG_PRINTLN("[SPI] self-test FAIL: no response");
    return false;
}
