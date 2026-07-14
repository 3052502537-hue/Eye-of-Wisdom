/* ============================================================
 *  文件名: spi_slave_comm.cpp
 *  功能描述:
 *    导盲头环项目 - SPI 从机通信实现文件
 *    实现 SpiSlaveComm 类的所有方法，包括:
 *    - SPI 从机模式初始化 (使用 ESP-IDF spi_slave 驱动)
 *    - 协议帧构建与解析 (帧头+命令+长度+数据+CRC+帧尾)
 *    - 图像数据分块发送 (大数据自动分块，每块独立握手)
 *    - 数据就绪信号控制 (GPIO 通知主控板)
 *    - 接收主控板下发的指令并解析
 *
 *  通信协议帧格式(与主控板 protocol.h 一致):
 *    ┌────────┬──────┬──────────┬────────┬──────┬────────┐
 *    │ 帧头   │ 命令 │ 数据长度  │  数据  │ CRC  │ 帧尾   │
 *    │ 2字节  │ 1字节│ 2字节    │ N字节  │ 1字节│ 2字节  │
 *    │ 0xAA55 │      │ 小端序   │        │      │ 0x55AA │
 *    └────────┴──────┴──────────┴────────┴──────┴────────┘
 *
 *  图像发送流程 (3步协议, 与主控板 spi_master_comm.cpp handleFrame() 配合):
 *    1. START帧(0x01): 发送元数据 (宽/高/格式/总大小)
 *    2. DATA帧(0x02): 分块发送 (blockIndex+blockSize+数据)
 *    3. END帧(0x03):   发送 totalBlocks
 *    每步: 构帧 → 拉低DATA_READY → 等待主控SPI读取 → 拉高DATA_READY
 *
 *  依赖关系:
 *    - spi_slave_comm.h (本文件头文件)
 *    - config.h (引脚和参数宏定义)
 *    - driver/spi_slave.h (ESP-IDF SPI 从机驱动)
 *    - driver/gpio.h (GPIO 控制)
 *    - Arduino.h (Serial 调试输出)
 *    - string.h (memcpy 等内存操作)
 *
 *  接口说明:
 *    本文件实现 spi_slave_comm.h 中声明的所有方法
 *    外部通过 SpiSlaveComm 类对象调用
 * ============================================================ */

#include "spi_slave_comm.h"
#include <string.h>
#include <stdlib.h>

/* ============================================================
 *  构造函数: 初始化成员变量
 * ============================================================ */
SpiSlaveComm::SpiSlaveComm()
{
    _initialized  = false;
    _txBuffer     = NULL;
    _rxBuffer     = NULL;
    _txFrameCount = 0;
    _rxFrameCount = 0;
    _rxQueue      = NULL;
    _txMutex      = NULL;
}

/* ============================================================
 *  析构函数: 释放所有资源
 * ============================================================ */
SpiSlaveComm::~SpiSlaveComm()
{
    if (_txBuffer) {
        free(_txBuffer);                   /* 释放发送缓冲区 */
        _txBuffer = NULL;
    }
    if (_rxBuffer) {
        free(_rxBuffer);                   /* 释放接收缓冲区 */
        _rxBuffer = NULL;
    }
    if (_rxQueue) {
        vQueueDelete(_rxQueue);            /* 删除接收队列 */
        _rxQueue = NULL;
    }
    if (_txMutex) {
        vSemaphoreDelete(_txMutex);        /* 删除互斥锁 */
        _txMutex = NULL;
    }
    _initialized = false;
}

/* ============================================================
 *  初始化 SPI 从机
 *  配置 SPI 总线、从机接口、DMA、数据就绪引脚
 * ============================================================ */
bool SpiSlaveComm::init(void)
{
    if (_initialized) {
        DBG_PRINTLN("[SPI] 已初始化，跳过");
        return true;
    }

    DBG_PRINTLN("[SPI] 开始初始化 SPI 从机...");

    /* --- 分配 DMA 对齐的发送/接收缓冲区 --- */
    /* 使用 heap_caps_malloc 分配 DMA 兼容内存 */
    _txBuffer = (uint8_t*)heap_caps_malloc(SPI_MAX_TRANSFER, MALLOC_CAP_DMA);
    _rxBuffer = (uint8_t*)heap_caps_malloc(SPI_MAX_TRANSFER, MALLOC_CAP_DMA);
    if (!_txBuffer || !_rxBuffer) {
        DBG_PRINTLN("[SPI] 缓冲区分配失败");
        return false;
    }
    memset(_txBuffer, 0, SPI_MAX_TRANSFER);
    memset(_rxBuffer, 0, SPI_MAX_TRANSFER);

    /* --- 创建接收队列 --- */
    _rxQueue = xQueueCreate(MAX_RX_QUEUE_LENGTH, sizeof(RxFrame_t));
    if (!_rxQueue) {
        DBG_PRINTLN("[SPI] 接收队列创建失败");
        return false;
    }

    /* --- 创建发送互斥锁 --- */
    _txMutex = xSemaphoreCreateMutex();
    if (!_txMutex) {
        DBG_PRINTLN("[SPI] 互斥锁创建失败");
        return false;
    }

    /* --- 配置 SPI 总线 --- */
    spi_bus_config_t busCfg = {
        .mosi_io_num     = PIN_SPI_MOSI,   /* MOSI 引脚 */
        .miso_io_num     = PIN_SPI_MISO,   /* MISO 引脚 */
        .sclk_io_num     = PIN_SPI_SCLK,   /* SCLK 引脚 */
        .quadwp_io_num   = GPIO_NUM_NC,    /* 不使用 QIO */
        .quadhd_io_num   = GPIO_NUM_NC,
        .data4_io_num    = GPIO_NUM_NC,
        .data5_io_num    = GPIO_NUM_NC,
        .data6_io_num    = GPIO_NUM_NC,
        .data7_io_num    = GPIO_NUM_NC,
        .max_transfer_sz = SPI_MAX_TRANSFER, /* 单次最大传输量 */
        .flags           = 0,
        .isr_cpu_id      = INTR_CPU_ID_1,  /* 中断在 CPU1 */
        .intr_flags      = 0,
    };

    /* --- 配置 SPI 从机接口 --- */
    spi_slave_interface_config_t slaveCfg = {
        .spics_io_num   = PIN_SPI_CS,      /* CS 引脚 */
        .flags          = 0,
        .queue_size     = 3,               /* 事务队列深度 */
        .mode           = SPI_MODE,        /* SPI Mode0 */
        .post_setup_cb  = NULL,            /* CS 拉低回调 (可选) */
        .post_trans_cb  = _onTransComplete,/* 传输完成回调 */
    };

    /* --- 初始化 SPI 从机 --- */
    /* 使用 DMA 通道 1 提高传输效率 */
    esp_err_t ret = spi_slave_initialize(SPI_HOST_DEVICE, &busCfg, &slaveCfg, DMA_CHANNEL_1);
    if (ret != ESP_OK) {
        DBG_PRINTF("[SPI] spi_slave_initialize 失败: %s\n", esp_err_to_name(ret));
        return false;
    }

    /* --- 初始化数据就绪引脚 --- */
    pinMode(PIN_DATA_READY, OUTPUT);
    digitalWrite(PIN_DATA_READY, DATA_READY_IDLE);  /* 默认高电平(空闲) */

    _initialized = true;
    DBG_PRINTF("[SPI] 初始化成功: MOSI=%d MISO=%d SCLK=%d CS=%d READY=%d\n",
               PIN_SPI_MOSI, PIN_SPI_MISO, PIN_SPI_SCLK, PIN_SPI_CS, PIN_DATA_READY);
    return true;
}

/* ============================================================
 *  发送一帧图像数据给主控板(3步协议: START→DATA→END)
 *  与主控板 spi_master_comm.cpp handleFrame() 配合:
 *    第1步: 发送 SPI_CMD_IMG_FRAME_START (0x01) + 元数据
 *    第2步: 发送 SPI_CMD_IMG_FRAME_DATA  (0x02) + blockIndex+blockSize+数据
 *    第3步: 发送 SPI_CMD_IMG_FRAME_END   (0x03) + totalBlocks
 * ============================================================ */
bool SpiSlaveComm::sendFrame(const uint8_t* imageData, uint32_t dataSize,
                              uint16_t width, uint16_t height, uint8_t format)
{
    if (!_initialized || !imageData || dataSize == 0) {
        return false;
    }

    /* 获取互斥锁，防止并发发送 */
    if (xSemaphoreTake(_txMutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        DBG_PRINTLN("[SPI] 获取互斥锁超时");
        return false;
    }

    DBG_PRINTF("[SPI] 发送图像: %ux%u, %u字节\n", width, height, (unsigned)dataSize);

    /* --- 第1步: 发送元数据帧 (CMD=0x01 START) --- */
    /* 元数据: width(2B LE) + height(2B LE) + format(1B) + totalSize(4B LE) = 9字节 */
    uint8_t metaData[9];
    metaData[0] = width & 0xFF;                /* 宽度低字节(LE) */
    metaData[1] = (width >> 8) & 0xFF;         /* 宽度高字节 */
    metaData[2] = height & 0xFF;               /* 高度低字节(LE) */
    metaData[3] = (height >> 8) & 0xFF;        /* 高度高字节 */
    metaData[4] = format;                      /* 图像格式 */
    metaData[5] = dataSize & 0xFF;             /* 总大小(LE) */
    metaData[6] = (dataSize >> 8) & 0xFF;
    metaData[7] = (dataSize >> 16) & 0xFF;
    metaData[8] = (dataSize >> 24) & 0xFF;

    uint32_t frameLen = 0;
    if (!_buildFrame(SPI_CMD_IMG_FRAME_START, metaData, sizeof(metaData),
                     _txBuffer, &frameLen)) {
        xSemaphoreGive(_txMutex);
        return false;
    }

    signalDataReady();
    bool ok = _transact(_txBuffer, _rxBuffer, frameLen);
    clearDataReady();

    if (!ok) {
        DBG_PRINTLN("[SPI] 元数据帧发送失败");
        xSemaphoreGive(_txMutex);
        return false;
    }
    _parseRxData(_rxBuffer, frameLen);

    /* --- 第2步: 分块发送图像数据 (CMD=0x02 DATA) --- */
    /* 每块数据区 = maxChunk - 块头(blockIndex 2B + blockSize 2B = 4B) */
    uint32_t maxChunkData = SPI_MAX_TRANSFER - SPI_FRAME_OVERHEAD - 4;
    uint32_t offset = 0;
    uint16_t chunkIndex = 0;

    /* 计算总分块数 */
    uint16_t totalChunks = (uint16_t)((dataSize + maxChunkData - 1) / maxChunkData);
    if (totalChunks == 0) totalChunks = 1;

    while (offset < dataSize) {
        uint32_t remaining = dataSize - offset;
        uint32_t chunkSize = (remaining > maxChunkData) ? maxChunkData : remaining;

        /* 构造块数据: blockIndex(2B LE) + blockSize(2B LE) + 图像数据 */
        uint8_t chunkBuf[SPI_MAX_TRANSFER];
        chunkBuf[0] = chunkIndex & 0xFF;
        chunkBuf[1] = (chunkIndex >> 8) & 0xFF;
        chunkBuf[2] = chunkSize & 0xFF;
        chunkBuf[3] = (chunkSize >> 8) & 0xFF;
        memcpy(chunkBuf + 4, imageData + offset, chunkSize);

        if (!_buildFrame(SPI_CMD_IMG_FRAME_DATA, chunkBuf, 4 + chunkSize,
                         _txBuffer, &frameLen)) {
            DBG_PRINTF("[SPI] 数据块 %u 构帧失败\n", chunkIndex);
            xSemaphoreGive(_txMutex);
            return false;
        }

        signalDataReady();
        ok = _transact(_txBuffer, _rxBuffer, frameLen);
        clearDataReady();

        if (!ok) {
            DBG_PRINTF("[SPI] 数据块 %u 发送失败\n", chunkIndex);
            xSemaphoreGive(_txMutex);
            return false;
        }
        _parseRxData(_rxBuffer, frameLen);

        offset += chunkSize;
        chunkIndex++;

#ifdef DEBUG
        DBG_PRINTF("[SPI] 已发送块 %u/%u: %u字节 (进度 %u/%u)\n",
                   chunkIndex, totalChunks, (unsigned)chunkSize,
                   (unsigned)offset, (unsigned)dataSize);
#endif
    }

    /* --- 第3步: 发送结束帧 (CMD=0x03 END) --- */
    /* 结束载荷: totalBlocks(2B LE) */
    uint8_t endData[2];
    endData[0] = totalChunks & 0xFF;
    endData[1] = (totalChunks >> 8) & 0xFF;

    if (!_buildFrame(SPI_CMD_IMG_FRAME_END, endData, 2, _txBuffer, &frameLen)) {
        DBG_PRINTLN("[SPI] 结束帧构建失败");
        xSemaphoreGive(_txMutex);
        return false;
    }

    signalDataReady();
    ok = _transact(_txBuffer, _rxBuffer, frameLen);
    clearDataReady();

    if (!ok) {
        DBG_PRINTLN("[SPI] 结束帧发送失败");
        xSemaphoreGive(_txMutex);
        return false;
    }
    _parseRxData(_rxBuffer, frameLen);

    _txFrameCount++;
    DBG_PRINTF("[SPI] 图像发送完成: %u块, 总计%u字节\n",
               totalChunks, (unsigned)dataSize);

    xSemaphoreGive(_txMutex);
    return true;
}

/* ============================================================
 *  发送心跳信号
 * ============================================================ */
bool SpiSlaveComm::sendHeartbeat(uint32_t timestamp)
{
    if (!_initialized) return false;

    if (xSemaphoreTake(_txMutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return false;
    }

    /* 心跳数据: 4字节时间戳(小端序, 与项目其他多字节字段一致) */
    uint8_t hbData[4];
    hbData[0] = timestamp & 0xFF;
    hbData[1] = (timestamp >> 8) & 0xFF;
    hbData[2] = (timestamp >> 16) & 0xFF;
    hbData[3] = (timestamp >> 24) & 0xFF;

    uint32_t frameLen = 0;
    bool ok = _buildFrame(SPI_CMD_HEARTBEAT, hbData, 4, _txBuffer, &frameLen);
    if (ok) {
        signalDataReady();
        ok = _transact(_txBuffer, _rxBuffer, frameLen);
        clearDataReady();

        if (ok) {
            _parseRxData(_rxBuffer, frameLen);
        }
    }

    xSemaphoreGive(_txMutex);
    return ok;
}

/* ============================================================
 *  激活数据就绪信号(LOW=有数据, 与主控FALLING边沿检测配合)
 *  通知主控板: 数据已准备好，可以发起 SPI 读取
 * ============================================================ */
void SpiSlaveComm::signalDataReady(void)
{
    digitalWrite(PIN_DATA_READY, DATA_READY_ACTIVE);  /* 拉低 → 主控检测下降沿 */
}

/* ============================================================
 *  清除数据就绪信号(HIGH=空闲)
 *  通知主控板: 数据已被读取，等待下一帧
 * ============================================================ */
void SpiSlaveComm::clearDataReady(void)
{
    digitalWrite(PIN_DATA_READY, DATA_READY_IDLE);    /* 拉高 → 空闲 */
}

/* ============================================================
 *  内部: 构建协议帧(与主控板 protocol.h 一致)
 *  帧格式: [帧头0xAA55][命令1B][长度2B LE][数据NB][CRC8][帧尾0x55AA]
 *  帧开销 = 2+1+2+1+2 = 8字节
 * ============================================================ */
bool SpiSlaveComm::_buildFrame(uint8_t cmd, const uint8_t* data, uint32_t dataLen,
                                uint8_t* outBuf, uint32_t* outLen)
{
    if (!outBuf || !outLen) return false;

    /* 检查数据长度是否超过最大值 */
    uint32_t totalLen = SPI_FRAME_OVERHEAD + dataLen;
    if (totalLen > SPI_MAX_TRANSFER) {
        DBG_PRINTF("[SPI] 帧大小 %u 超过最大传输量 %u\n",
                   (unsigned)totalLen, SPI_MAX_TRANSFER);
        return false;
    }

    /* 写入帧头 */
    outBuf[0] = 0xAA;
    outBuf[1] = 0x55;

    /* 写入命令码 */
    outBuf[2] = cmd;

    /* 写入数据长度 (2字节小端序) */
    outBuf[3] = dataLen & 0xFF;
    outBuf[4] = (dataLen >> 8) & 0xFF;

    /* 写入数据 (从偏移5开始) */
    if (data && dataLen > 0) {
        memcpy(outBuf + 5, data, dataLen);
    }

    /* 计算 CRC8 (覆盖: 命令1B + 长度2B + 数据NB) */
    uint8_t crc = _crc8(outBuf + 2, 1 + 2 + dataLen);
    outBuf[5 + dataLen] = crc;

    /* 写入帧尾 */
    outBuf[6 + dataLen] = 0x55;
    outBuf[7 + dataLen] = 0xAA;

    *outLen = totalLen;
    return true;
}

/* ============================================================
 *  内部: 执行 SPI 从机传输
 *  将数据加入从机发送队列，等待主控板读取
 * ============================================================ */
bool SpiSlaveComm::_transact(uint8_t* txBuf, uint8_t* rxBuf, uint32_t len)
{
    if (!_initialized || !txBuf || !rxBuf || len == 0) {
        return false;
    }

    /* 清空接收缓冲区 */
    memset(rxBuf, 0, SPI_MAX_TRANSFER);

    /* 准备 SPI 从机事务 */
    spi_slave_transaction_t trans;
    memset(&trans, 0, sizeof(trans));
    trans.length    = len * 8;             /* 传输长度 (位) */
    trans.tx_buffer = txBuf;               /* 发送数据 */
    trans.rx_buffer = rxBuf;               /* 接收数据 */

    /* 将事务加入队列 (此时数据已准备好，等主控发起读取) */
    esp_err_t ret = spi_slave_queue_trans(SPI_HOST_DEVICE, &trans,
                                          pdMS_TO_TICKS(SPI_TRANS_TIMEOUT_MS));
    if (ret != ESP_OK) {
        DBG_PRINTF("[SPI] 事务入队失败: %s\n", esp_err_to_name(ret));
        return false;
    }

    /* 等待主控读取完成 (阻塞直到传输完成或超时) */
    spi_slave_transaction_t* result = NULL;
    ret = spi_slave_get_trans_result(SPI_HOST_DEVICE, &result,
                                     pdMS_TO_TICKS(SPI_TRANS_TIMEOUT_MS));
    if (ret != ESP_OK) {
        DBG_PRINTF("[SPI] 传输超时或失败: %s\n", esp_err_to_name(ret));
        return false;
    }

    return true;
}

/* ============================================================
 *  内部: 解析接收到的数据
 *  校验帧头帧尾和CRC，提取命令和数据放入接收队列
 * ============================================================ */
bool SpiSlaveComm::_parseRxData(const uint8_t* data, size_t len)
{
    if (!data || len < SPI_FRAME_OVERHEAD) {
        return false;
    }

    /* 校验帧头 */
    if (data[0] != 0xAA || data[1] != 0x55) {
        return false;
    }

    /* 提取命令码 */
    uint8_t cmd = data[2];

    /* 提取数据长度 (2字节小端序, 与主控一致) */
    uint32_t dataLen = (uint32_t)data[3] | ((uint32_t)data[4] << 8);

    /* 检查数据长度有效性 */
    if (dataLen > SPI_MAX_TRANSFER) {
        return false;
    }

    /* 校验总长度(帧开销=8字节) */
    if (len < SPI_FRAME_OVERHEAD + dataLen) {
        return false;
    }

    /* 校验 CRC8 (覆盖: 命令1B + 长度2B + 数据NB) */
    uint8_t expectedCrc = _crc8(data + 2, 1 + 2 + dataLen);
    uint8_t actualCrc = data[5 + dataLen];
    if (expectedCrc != actualCrc) {
        DBG_PRINTF("[SPI] CRC校验失败: 期望0x%02X 实际0x%02X\n", expectedCrc, actualCrc);
        return false;
    }

    /* 校验帧尾 */
    if (data[6 + dataLen] != 0x55 || data[7 + dataLen] != 0xAA) {
        return false;
    }

    /* 如果有数据内容，放入接收队列 */
    if (dataLen > 0) {
        RxFrame_t rxFrame;
        rxFrame.cmd = cmd;
        rxFrame.dataLen = (dataLen > SPI_MAX_TRANSFER) ? SPI_MAX_TRANSFER : dataLen;
        memcpy(rxFrame.data, data + 5, rxFrame.dataLen);

        /* 非阻塞方式放入队列 */
        xQueueSend(_rxQueue, &rxFrame, 0);
        _rxFrameCount++;

#ifdef DEBUG
        DBG_PRINTF("[SPI] 收到指令: cmd=0x%02X, len=%u\n", cmd, (unsigned)dataLen);
#endif
    }

    return true;
}

/* ============================================================
 *  内部: CRC8 校验计算
 *  多项式: 0x07，初始值: 0x00
 * ============================================================ */
uint8_t SpiSlaveComm::_crc8(const uint8_t* data, size_t len)
{
    uint8_t crc = 0x00;

    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];                 /* 异或当前字节 */
        for (uint8_t j = 0; j < 8; j++) {
            if (crc & 0x80) {
                crc = (crc << 1) ^ 0x07; /* 最高位为1，移位并异或多项式 */
            } else {
                crc <<= 1;               /* 最高位为0，仅移位 */
            }
        }
    }

    return crc;
}

/* ============================================================
 *  SPI 传输完成回调函数 (ISR 中调用)
 *  此函数在 SPI 传输完成后由中断服务程序调用
 *  注意: 不能在 ISR 中执行耗时操作或调用阻塞 API
 * ============================================================ */
void IRAM_ATTR SpiSlaveComm::_onTransComplete(spi_slave_transaction_t* trans)
{
    /* ISR 回调，当前为空。
     * 如需在传输完成时执行操作，可在此处使用 FreeRTOS 信号量通知。
     * 示例:
     *   static SemaphoreHandle_t s_transDoneSem = NULL;
     *   if (s_transDoneSem) xSemaphoreGiveFromISR(s_transDoneSem, NULL);
     */
    (void)trans;     /* 避免未使用参数警告 */
}
