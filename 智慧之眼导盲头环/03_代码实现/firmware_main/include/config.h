/* ============================================================
 *  智慧之眼导盲头环 - 主控板配置文件
 *  版本: v1.0
 *  更新日期: 2026-07-10
 *
 *  说明: 本文件包含所有引脚定义和可调参数
 *       修改硬件接线后只需修改此文件
 * ============================================================ */

#ifndef CONFIG_H
#define CONFIG_H

#include <stdint.h>

/* ============================================================
 *  一、毫米波雷达引脚配置
 * ============================================================ */

/* 前雷达 - 前方障碍物检测 */
#define PIN_RADAR_FRONT_TX      17      /* 雷达TX → ESP32 RX */
#define PIN_RADAR_FRONT_RX      18      /* ESP32 TX → 雷达RX */
#define RADAR_FRONT_UART        UART_NUM_1

/* 后雷达 - 后方接近预警 */
#define PIN_RADAR_REAR_TX       19      /* 雷达TX → ESP32 RX */
#define PIN_RADAR_REAR_RX       20      /* ESP32 TX → 雷达RX */
#define RADAR_REAR_UART         UART_NUM_2

/* 雷达公共参数 */
#define RADAR_BAUDRATE          115200  /* RD03D 默认波特率 */

/* ============================================================
 *  二、蜂鸣器 + 振动马达 + 状态LED引脚
 * ============================================================ */

/* 蜂鸣器 - PWM控制不同频率 */
#define PIN_BUZZER              47      /* PWM输出引脚 */
#define BUZZER_LEDC_CHANNEL     0
#define BUZZER_LEDC_TIMER       LEDC_TIMER_0

/* 振动马达 - 四路方向指示
 *  引脚选择: 避开 SPI (35,36,37,38)、雷达 (17,18,19,20)、蜂鸣器 (47)、LED (48)
 */
#define PIN_VIB_FRONT           42      /* 前方振动 */
#define PIN_VIB_REAR            43      /* 后方振动 */
#define PIN_VIB_LEFT            44      /* 左侧振动 */
#define PIN_VIB_RIGHT           45      /* 右侧振动 */

/* 状态LED */
#define PIN_STATUS_LED          48      /* 板载LED或外接LED */

/* ============================================================
 *  三、SPI从机通信（连接摄像头板）
 *  注意: 引脚需与摄像头板的SPI主机引脚一一对应连接
 *       摄像头板  →  主控板
 *       MOSI(35) → MOSI(35)
 *       MISO(36) ← MISO(36)
 *       SCLK(37) → SCLK(37)
 *       CS(38)   → CS(38)
 *       GND      → GND  (必须共地!)
 * ============================================================ */

#define PIN_SPI_MOSI            35      /* 摄像头板MOSI → 主控板MOSI */
#define PIN_SPI_MISO            36      /* 主控板MISO → 摄像头板MISO */
#define PIN_SPI_SCLK            37      /* 摄像头板SCLK → 主控板SCLK */
#define PIN_SPI_CS              38      /* 摄像头板CS → 主控板CS */
#define SPI_HOST                SPI2_HOST
#define SPI_CLOCK_SPEED         10000000 /* 10MHz，可根据实际连线质量调整 */

/* ============================================================
 *  四、预警距离阈值配置
 *  单位: 米
 * ============================================================ */

#define WARN_DISTANCE_ATTENTION  3.0f   /* 大于此值: 安全 */
#define WARN_DISTANCE_WARNING    2.0f   /* 小于此值: 警告 */
#define WARN_DISTANCE_DANGER     1.0f   /* 小于此值: 危险 */

#define WARN_DEBOUNCE_FRAMES     3      /* 连续N帧确认才触发预警 */

/* ============================================================
 *  五、蜂鸣器预警参数
 * ============================================================ */

#define BUZZER_FREQ_ATTENTION    1000   /* 注意: 1kHz */
#define BUZZER_FREQ_WARNING      1500   /* 警告: 1.5kHz */
#define BUZZER_FREQ_DANGER       2000   /* 危险: 2kHz */

#define BUZZER_INTERVAL_ATTENTION  500  /* 间断间隔(ms) */
#define BUZZER_INTERVAL_WARNING    250
#define BUZZER_INTERVAL_DANGER     100  /* 危险时几乎连续 */

/* ============================================================
 *  六、振动马达预警参数
 * ============================================================ */

#define VIB_DURATION_ATTENTION   100    /* 单次振动时长(ms) */
#define VIB_DURATION_WARNING     150
#define VIB_DURATION_DANGER      200

/* ============================================================
 *  七、视觉检测配置
 * ============================================================ */

#define VISION_FRAME_WIDTH       320    /* QVGA 宽度 */
#define VISION_FRAME_HEIGHT      240    /* QVGA 高度 */
#define VISION_FPS_TARGET        15     /* 目标帧率 */

/* 盲道检测 */
#define LANE_ROI_TOP_RATIO       0.4f   /* ROI从图像40%高度处开始（地面部分） */
#define LANE_DETECT_CONFIDENCE   0.5f   /* 最低置信度 */

/* 红绿灯检测 */
#define TL_ROI_TOP_RATIO         0.0f   /* ROI从顶部开始 */
#define TL_ROI_BOTTOM_RATIO      0.4f   /* ROI到40%高度处（天空部分） */
#define TL_DETECT_CONFIDENCE     0.6f   /* 最低置信度 */

/* 斑马线检测 */
#define CW_ROI_TOP_RATIO         0.3f   /* ROI从30%高度处开始 */
#define CW_ROI_BOTTOM_RATIO      0.9f   /* ROI到90%高度处 */
#define CW_MIN_STRIPES           5      /* 最少条纹数 */
#define CW_DETECT_CONFIDENCE     0.5f   /* 最低置信度 */

/* ============================================================
 *  八、FreeRTOS任务优先级
 *  数值越大优先级越高（ESP-IDF约定）
 * ============================================================ */

#define TASK_PRIORITY_RADAR_FRONT   5
#define TASK_PRIORITY_RADAR_REAR    5
#define TASK_PRIORITY_SPI_RECV      6
#define TASK_PRIORITY_VISION        4
#define TASK_PRIORITY_WARNING       6
#define TASK_PRIORITY_FEEDBACK      5
#define TASK_PRIORITY_STATE_MGR     3
#define TASK_PRIORITY_DEBUG         2
#define TASK_PRIORITY_HEARTBEAT     1

/* ============================================================
 *  九、FreeRTOS任务栈大小（单位: 字节）
 * ============================================================ */

#define TASK_STACK_RADAR_FRONT   2048
#define TASK_STACK_RADAR_REAR    2048
#define TASK_STACK_SPI_RECV      4096
#define TASK_STACK_VISION        16384  /* 视觉处理需要较大栈 */
#define TASK_STACK_WARNING       4096
#define TASK_STACK_FEEDBACK      2048
#define TASK_STACK_STATE_MGR     2048
#define TASK_STACK_DEBUG         4096
#define TASK_STACK_HEARTBEAT     1024

/* ============================================================
 *  十、任务周期与队列配置
 * ============================================================ */

#define WARNING_UPDATE_PERIOD_MS   100  /* 预警决策周期 */
#define STATE_UPDATE_PERIOD_MS     500  /* 状态管理周期 */
#define HEARTBEAT_PERIOD_MS        1000 /* 心跳周期 */

#define DEBUG_QUEUE_SIZE           20
#define WARNING_QUEUE_SIZE         10

/* ============================================================
 *  十一、调试串口配置
 * ============================================================ */

#define DEBUG_SERIAL_BAUDRATE      115200

#endif /* CONFIG_H */
