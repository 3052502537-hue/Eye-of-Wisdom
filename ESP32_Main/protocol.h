/* ============================================================
 * 文件名: protocol.h
 * 功能描述: 导盲头环系统通信协议定义
 *           定义 SPI帧格式、JSON数据格式、UDP图像包格式、
 *           命令枚举、工作模式枚举、数据结构等
 * 依赖关系: 无外部依赖，被 spi_master_comm / wifi_manager /
 *           task_manager 等模块引用
 * 接口说明: 提供枚举、结构体、宏及CRC校验工具函数
 *
 * 通信总览:
 *   1. 主控 <-> 摄像头板: SPI帧协议（自定义帧头/尾+CRC8）
 *   2. 主控 -> 手机App:   传感器数据 JSON over TCP
 *   3. 主控 -> 手机App:   图像数据 二进制JPEG over UDP
 *   4. 手机App -> 主控:   控制命令 JSON over TCP
 * ============================================================ */

#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <stdint.h>
#include <stddef.h>

/* ============================================================
 * 一、SPI 帧格式定义（主控<->摄像头板）
 *
 * 帧结构: | 帧头(2B) | 命令(1B) | 数据长度(2B) | 数据(NB) | CRC8(1B) | 帧尾(2B) |
 *   帧头:   0xAA 0x55
 *   帧尾:   0x55 0xAA
 *   数据长度: 小端16位，N最大=SPI_MAX_DATA_LEN
 *   CRC8:   多项式0x07，初值0x00，对 [命令+长度+数据] 计算
 * ============================================================ */
#define SPI_FRAME_HEADER0          0xAA
#define SPI_FRAME_HEADER1          0x55
#define SPI_FRAME_TAIL0            0x55
#define SPI_FRAME_TAIL1            0xAA

#define SPI_FRAME_HEADER_LEN       2
#define SPI_FRAME_TAIL_LEN         2
#define SPI_FRAME_CMD_LEN          1
#define SPI_FRAME_LEN_LEN          2
#define SPI_FRAME_CRC_LEN          1
/* 帧总开销 = 帧头+命令+长度+CRC+帧尾 */
#define SPI_FRAME_OVERHEAD         (SPI_FRAME_HEADER_LEN + SPI_FRAME_CMD_LEN + \
                                   SPI_FRAME_LEN_LEN + SPI_FRAME_CRC_LEN + SPI_FRAME_TAIL_LEN)
#define SPI_MAX_DATA_LEN           4096   // 单帧数据最大长度

/* SPI 命令枚举 */
typedef enum {
    SPI_CMD_IMG_FRAME_START   = 0x01,  // 图像帧起始（含分辨率/格式/总大小）
    SPI_CMD_IMG_FRAME_DATA    = 0x02,  // 图像帧数据块
    SPI_CMD_IMG_FRAME_END     = 0x03,  // 图像帧结束（含总块数校验）
    SPI_CMD_SET_RESOLUTION    = 0x10,  // 设置分辨率（主控->摄像头）
    SPI_CMD_SET_FPS           = 0x11,  // 设置帧率（主控->摄像头）
    SPI_CMD_SET_QUALITY       = 0x12,  // 设置JPEG质量（主控->摄像头）
    SPI_CMD_ACK               = 0x20,  // 应答
    SPI_CMD_NACK              = 0x21,  // 否定应答
    SPI_CMD_HEARTBEAT         = 0x30,  // 心跳
} SpiCommand_t;

/* 图像格式枚举 */
typedef enum {
    IMG_FMT_RGB565    = 0,
    IMG_FMT_GRAYSCALE = 1,
    IMG_FMT_JPEG      = 2,
} ImageFormat_t;

/* 分辨率枚举 */
typedef enum {
    RESOLUTION_QVGA = 0,   // 320x240
    RESOLUTION_VGA  = 1,   // 640x480
} Resolution_t;

#define RESOLUTION_QVGA_W     320
#define RESOLUTION_QVGA_H     240
#define RESOLUTION_VGA_W      640
#define RESOLUTION_VGA_H      480

/* SPI帧结构（解析后） */
typedef struct {
    uint8_t  cmd;                              // 命令字
    uint16_t dataLen;                          // 数据长度
    uint8_t  data[SPI_MAX_DATA_LEN];           // 数据载荷
} SpiFrame_t;

/* 图像帧起始载荷 */
typedef struct {
    uint16_t width;                            // 图像宽
    uint16_t height;                           // 图像高
    uint8_t  format;                           // 图像格式 ImageFormat_t
    uint32_t totalSize;                        // 整帧字节数
} ImgFrameStart_t;

/* 图像帧数据块载荷 */
typedef struct {
    uint16_t blockIndex;                       // 当前块序号(从0开始)
    uint16_t blockSize;                        // 当前块字节数
    /* 后接 blockSize 字节数据 */
} ImgFrameData_t;

/* 图像帧结束载荷 */
typedef struct {
    uint16_t totalBlocks;                      // 总块数
} ImgFrameEnd_t;

/* ============================================================
 * 二、UDP 图像包格式（主控->手机App）
 *
 * 由于单帧JPEG可能大于MTU，需分片发送。每包格式:
 *   | 帧ID(4B) | 分片序号(2B) | 总分片数(2B) | 数据长度(2B) | JPEG数据(NB) |
 *   帧ID:     每帧图像递增，用于接收端重组
 *   分片序号: 从0开始
 *   总分片数: 该帧共分多少片
 *   所有多字节字段: 小端
 * ============================================================ */
#define UDP_IMG_HEADER_LEN         10         // 帧ID+分片序号+总分片数+数据长度
#define UDP_IMG_MAX_PAYLOAD        (UDP_PACKET_MAX_SIZE - UDP_IMG_HEADER_LEN)  // 单包最大JPEG载荷

typedef struct {
    uint32_t frameId;                          // 帧ID
    uint16_t sliceIndex;                       // 分片序号
    uint16_t sliceTotal;                       // 总分片数
    uint16_t dataLen;                          // 本包JPEG数据长度
} __attribute__((packed)) UdpImgHeader_t;

/* ============================================================
 * 三、JSON 通信协议（主控<->手机App，TCP）
 *
 * 3.1 传感器数据上报（主控->App）:
 * {
 *   "type":"sensor",
 *   "ts":123456,                         // 时间戳(ms)
 *   "laser_front":1.23,                  // 前向激光距离(m)，数值型
 *   "radar_front":{                      // 前向雷达目标(最近目标)
 *     "dist":1.5,"speed":0.8,"angle":15.0
 *   },
 *   "radar_back":{                       // 后向雷达目标(最近目标)
 *     "dist":2.0,"speed":-0.5,"angle":-30.0
 *   },
 *   "battery":85,                        // 电池电量百分比
 *   "mode":2,                            // 工作模式 1传感器/2自动/3风险
 *   "level":0,                           // 危险等级 0安全/1注意/2危险
 *   "img":1                              // 是否有新图像帧(0/1)
 * }
 *
 * 3.2 控制命令（App->主控）:
 * {"cmd":"set_mode","mode":1}
 * {"cmd":"set_warn","d_att":3.0,"d_war":2.0,"d_dan":1.0}
 * {"cmd":"set_img","res":0,"fps":15,"q":10}
 * {"cmd":"reboot"}
 * ============================================================ */

/* TCP消息类型 */
typedef enum {
    MSG_TYPE_SENSOR   = 0,   // 传感器数据
    MSG_TYPE_STATUS   = 1,   // 系统状态
    MSG_TYPE_ACK      = 2,   // 应答
    MSG_TYPE_ERROR    = 3,   // 错误
} MessageType_t;

/* App下发命令枚举 */
typedef enum {
    CMD_SET_MODE       = 0,   // 设置工作模式
    CMD_SET_WARN       = 1,   // 设置预警阈值
    CMD_SET_IMG        = 2,   // 设置图像参数
    CMD_SET_BUZZER     = 3,   // 设置蜂鸣器开关
    CMD_REBOOT         = 4,   // 重启
    CMD_CALIBRATE      = 5,   // 校准传感器
    CMD_QUERY_STATUS   = 6,   // 查询状态
} AppCommand_t;

/* 工作模式枚举(与Android APP AppConfig.java 一致) */
typedef enum {
    MODE_SENSOR_ONLY  = 1,   // 传感器模式（仅传感器，低能耗）
    MODE_AUTO         = 2,   // 自动模式（全传感器+视觉融合）
    MODE_RISK_ONLY    = 3,   // 风险播报模式（仅播报风险等级）
    MODE_DEBUG        = 4,   // 调试模式（全量数据上报）
} WorkMode_t;

/* 危险等级枚举(与Android APP AppConfig.java 一致) */
typedef enum {
    LEVEL_SAFE        = 0,   // 安全
    LEVEL_CAUTION     = 1,   // 注意
    LEVEL_DANGER      = 2,   // 危险（触发蜂鸣器）
} DangerLevel_t;

/* ============================================================
 * 四、传感器数据结构
 * ============================================================ */

/* 单个雷达目标（距离m / 速度m/s / 角度°） */
typedef struct {
    float distance;                           // 目标距离(m)
    float speed;                              // 目标径向速度(m/s，靠近为正)
    float angle;                              // 目标角度(°，正中为0，右正左负)
} RadarTarget_t;

/* 激光测距数据 */
typedef struct {
    float distance;                           // 距离(m)
    uint8_t valid;                            // 数据有效性 0无效/1有效
} LaserData_t;

/* 一帧雷达数据 */
typedef struct {
    RadarTarget_t targets[RADAR_MAX_TARGETS]; // 目标数组
    uint8_t count;                            // 目标数量
    uint8_t valid;                            // 数据有效性
} RadarData_t;

/* 完整传感器数据帧（供任务间共享） */
typedef struct {
    LaserData_t  laser;                       // 前向激光
    RadarData_t  radarFront;                  // 前向雷达
    RadarData_t  radarRear;                   // 后向雷达
    uint32_t     timestamp;                   // 时间戳(ms)
    DangerLevel_t level;                      // 危险等级
} SensorFrame_t;

/* ============================================================
 * 五、CRC8 校验工具函数
 *    多项式: 0x07，初值: 0x00
 * ============================================================ */
static inline uint8_t crc8(const uint8_t* data, size_t len)
{
    uint8_t crc = 0x00;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (uint8_t j = 0; j < 8; j++) {
            if (crc & 0x80) {
                crc = (uint8_t)((crc << 1) ^ 0x07);
            } else {
                crc <<= 1;
            }
        }
    }
    return crc;
}

/* 简单累加和校验（部分传感器协议使用） */
static inline uint8_t sumChecksum(const uint8_t* data, size_t len)
{
    uint8_t sum = 0;
    for (size_t i = 0; i < len; i++) {
        sum += data[i];
    }
    return sum;
}

#endif /* PROTOCOL_H */
