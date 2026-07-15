/* ============================================================
 * 文件名: protocol.h
 * 功能描述: 导盲头环系统通信协议定义 v3.0
 *           v3.0: 彻底移除雷达数据结构、SPI帧协议、UDP图像包格式
 *                 新增 camera_ip 字段告知手机摄像板地址
 *           定义 JSON 数据格式、命令枚举、工作模式枚举、传感器数据结构
 * 依赖关系: 无外部依赖，被 wifi_manager / task_manager 等模块引用
 * 接口说明: 提供枚举、结构体、宏及CRC校验工具函数
 *
 * 通信总览 (v3.0):
 *   1. 主控 -> 手机App:   传感器数据 JSON over TCP (port 8888)
 *                         含 camera_ip 告知手机摄像板地址
 *   2. 手机App -> 主控:   控制命令 JSON over TCP
 *   3. 摄像板 -> 手机App: MJPEG 视频流 over HTTP (/video)
 *   4. 摄像板 -> 手机App: 单帧 JPEG over HTTP (/capture)
 * ============================================================ */

#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <stdint.h>
#include <stddef.h>

/* ============================================================
 * 一、JSON 通信协议（主控<->手机App，TCP port 8888）
 *
 * 1.1 传感器数据上报（主控->App）:
 * {
 *   "type":"sensor",
 *   "ts":123456,                         // 时间戳(ms)
 *   "laser_front":1.23,                  // 前向激光距离(m)，数值型，-1无效
 *   "ultrasonic":2.10,                   // HC-SR04 超声波距离(m)，-1无效
 *   "camera_ip":"192.168.4.10",          // 摄像板ESP32的IP地址(v3.0新增)
 *   "battery":85,                        // 电池电量百分比(暂未接入，固定-1)
 *   "mode":2,                            // 工作模式 1传感器/2自动/3风险
 *   "level":0                            // 危险等级 0安全/1注意/2危险
 * }
 *
 * 1.2 控制命令（App->主控）:
 * {"cmd":"set_mode","mode":1}
 * {"cmd":"set_warn","d_att":3.0,"d_war":2.0,"d_dan":1.0}
 * {"cmd":"set_buzzer","on":1}
 * {"cmd":"reboot"}
 * {"cmd":"query"}
 * {"cmd":"calibrate"}
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
    CMD_SET_BUZZER     = 2,   // 设置蜂鸣器开关
    CMD_REBOOT         = 3,   // 重启
    CMD_CALIBRATE      = 4,   // 校准传感器
    CMD_QUERY_STATUS   = 5,   // 查询状态
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
 * 二、传感器数据结构
 * ============================================================ */

/* 激光测距数据 */
typedef struct {
    float distance;                           // 距离(m)
    uint8_t valid;                            // 数据有效性 0无效/1有效
} LaserData_t;

/* 完整传感器数据帧（供任务间共享） */
typedef struct {
    LaserData_t  laser;                       // 前向激光
    float        ultrasonicDist;              // HC-SR04 超声波距离(m), -1无效
    uint32_t     timestamp;                   // 时间戳(ms)
    DangerLevel_t level;                      // 危险等级
} SensorFrame_t;

/* ============================================================
 * 三、CRC8 校验工具函数
 *    多项式: 0x07，初值: 0x00
 *    摄像板 HTTP 通信中用于数据校验
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
