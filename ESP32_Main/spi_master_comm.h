/* ============================================================
 * 文件名: spi_master_comm.h
 * 功能描述: SPI 主机通信头文件
 *           主控板作为 SPI Master，接收摄像头板(Slave)传来的图像数据
 *           摄像头板有数据就绪时拉低 DATA_READY 中断引脚，主控响应中断
 *           并发起 SPI 读取，按帧协议重组为完整 JPEG 图像帧
 * 依赖关系: Arduino SPI 库、config.h、protocol.h
 * 接口说明:
 *   SpiMasterComm()        - 构造函数
 *   begin()                - 初始化SPI主机+中断
 *   getLatestFrame()       - 获取最新完整JPEG帧
 *   isFrameReady()         - 查询是否有新帧
 *   sendCommand()          - 向摄像头板发送控制命令
 *   selfTest()             - 通信自检(心跳)
 *
 * 数据流: 摄像头板拉低DATA_READY -> ISR置标志 -> 任务调用readFrame()
 *         -> SPI读取帧 -> 重组到帧缓冲 -> 置帧就绪标志
 * ============================================================ */

#ifndef SPI_MASTER_COMM_H
#define SPI_MASTER_COMM_H

#include <Arduino.h>
#include <SPI.h>
#include "config.h"
#include "protocol.h"

/* 完整图像帧缓冲（供外部获取） */
typedef struct {
    uint32_t frameId;                        // 帧序号
    uint16_t width;                          // 宽
    uint16_t height;                         // 高
    uint8_t  format;                         // 格式
    uint32_t size;                           // 实际JPEG字节数
    uint8_t* data;                           // 数据指针(指向内部缓冲)
    uint32_t timestamp;                      // 接收完成时间戳
} ImageFrame_t;

class SpiMasterComm {
public:
    SpiMasterComm();
    ~SpiMasterComm();

    /* --------------------------------------------------------
     * begin - 初始化 SPI 主机与数据就绪中断
     * 参数: 无
     * 返回: true=成功
     * 说明: 配置 SPI 引脚、时钟、CS，挂接 GPIO 中断
     * -------------------------------------------------------- */
    bool begin();

    /* --------------------------------------------------------
     * readFrameIfReady - 若收到数据就绪中断则读取一帧
     * 参数: 无
     * 返回: true=本次成功读取并重组了一帧, false=无数据或未完成
     * 说明: 应在 SPI 任务中周期调用；内部完成帧起始/数据/结束重组
     * -------------------------------------------------------- */
    bool readFrameIfReady();

    /* --------------------------------------------------------
     * isFrameReady - 查询是否有新的完整帧待取
     * 返回: true=有
     * -------------------------------------------------------- */
    bool isFrameReady() const { return _frameReady; }

    /* --------------------------------------------------------
     * getLatestFrame - 获取最新完整帧（取后清就绪标志）
     * 参数: outFrame - 输出帧信息(数据指针指向内部缓冲，请立即使用)
     * 返回: true=成功, false=暂无完整帧
     * 注意: 取用后帧数据可能被下一帧覆盖，请及时拷贝
     * -------------------------------------------------------- */
    bool getLatestFrame(ImageFrame_t* outFrame);

    /* --------------------------------------------------------
     * sendCommand - 向摄像头板发送 SPI 控制命令
     * 参数: cmd     - 命令字 SpiCommand_t
     *       payload - 载荷指针(可为NULL)
     *       len     - 载荷长度
     * 返回: true=发送成功
     * -------------------------------------------------------- */
    bool sendCommand(uint8_t cmd, const uint8_t* payload, uint16_t len);

    /* --------------------------------------------------------
     * selfTest - SPI通信自检(发送心跳并等待应答)
     * 返回: true=通信正常, false=超时无应答
     * -------------------------------------------------------- */
    bool selfTest();

    /* 中断服务标志(由 ISR 设置，任务检测) */
    static volatile bool _dataReadyFlag;

private:
    /* 通过 SPI 读取指定长度数据到缓冲 */
    size_t spiTransfer(uint8_t* outBuf, size_t len);

    /* 解析一帧 SPI 帧 */
    bool parseSpiFrame(const uint8_t* raw, size_t rawLen, SpiFrame_t* frame);

    /* 处理一帧(按命令分发: 起始/数据/结束) */
    void handleFrame(const SpiFrame_t* frame);

    /* 构造并发送一帧 */
    bool sendFrame(uint8_t cmd, const uint8_t* payload, uint16_t len);

    SPIClass*  _spi;               // SPI 实例(HSPI=SPI3, 避开PSRAM占用的SPI2)
    SemaphoreHandle_t _spiMutex;   // SPI 总线互斥锁(读/写仲裁)
    bool       _initialized;       // 初始化标志
    bool       _frameReady;        // 完整帧就绪标志
    bool       _frameInProg;       // 帧接收进行中标志

    /* 帧重组缓冲 */
    uint8_t    _imgBuf[IMG_MAX_JPEG_SIZE];   // JPEG 帧缓冲
    uint32_t   _imgRecvLen;                   // 已接收字节数
    uint32_t   _imgExpectLen;                 // 期望总字节数
    uint16_t   _imgWidth;                     // 帧宽
    uint16_t   _imgHeight;                    // 帧高
    uint8_t    _imgFormat;                    // 帧格式
    uint16_t   _imgBlockCount;                // 已收块数
    uint32_t   _frameId;                      // 帧序号

    ImageFrame_t _latestFrame;                // 最新帧信息
};

#endif /* SPI_MASTER_COMM_H */
