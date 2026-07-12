/* ============================================================
 *  智慧之眼导盲头环 - 摄像头板配置文件
 *  版本: v1.0
 *  更新日期: 2026-07-10
 *
 *  说明: 本文件包含所有引脚定义和可调参数
 *       修改硬件接线后只需修改此文件
 *
 *  引脚分配原则:
 *   - 摄像头 DVP 引脚与 SPI 引脚完全分开，无复用
 *   - 摄像头数据总线 (D0-D7) 尽量连续
 *   - SPI 使用 ESP32-S3 FSPIQ 封装常用引脚
 * ============================================================ */

#ifndef CONFIG_H
#define CONFIG_H

#include <stdint.h>

/* ============================================================
 *  一、OV2640 摄像头引脚配置 (DVP接口)
 *  共 16 个引脚，全部独立，不与SPI复用
 * ============================================================ */

#define PIN_CAMERA_PWDN    2          /* 掉电控制 (高电平掉电) */
#define PIN_CAMERA_RESET   3          /* 复位引脚 (低电平复位) */
#define PIN_CAMERA_XCLK    4          /* 主时钟输出 (24MHz) */
#define PIN_CAMERA_SIOD    5          /* SCCB数据 (I2C SDA) */
#define PIN_CAMERA_SIOC    6          /* SCCB时钟 (I2C SCL) */
#define PIN_CAMERA_VSYNC   7          /* 帧同步输入 */
#define PIN_CAMERA_HREF    8          /* 行同步输入 */
#define PIN_CAMERA_PCLK    9          /* 像素时钟输入 */
#define PIN_CAMERA_D7      10         /* 数据位7 (MSB) */
#define PIN_CAMERA_D6      11         /* 数据位6 */
#define PIN_CAMERA_D5      12         /* 数据位5 */
#define PIN_CAMERA_D4      13         /* 数据位4 */
#define PIN_CAMERA_D3      14         /* 数据位3 */
#define PIN_CAMERA_D2      15         /* 数据位2 */
#define PIN_CAMERA_D1      16         /* 数据位1 */
#define PIN_CAMERA_D0      17         /* 数据位0 (LSB) */

/* ============================================================
 *  二、SPI主机通信（连接主控板）
 *  使用 SPI2_HOST，引脚全部独立，不与摄像头复用
 * ============================================================ */

#define PIN_SPI_MOSI        35         /* 主机输出 → 从机输入 */
#define PIN_SPI_MISO        36         /* 从机输出 → 主机输入 */
#define PIN_SPI_SCLK        37         /* SPI时钟 (主机输出) */
#define PIN_SPI_CS          38         /* 片选 (低电平有效，主机控制) */
#define SPI_HOST            SPI2_HOST
#define SPI_CLOCK_SPEED     10000000   /* 10MHz，可根据实际连线质量调整 */

/* ============================================================
 *  三、摄像头参数配置
 * ============================================================ */

#define CAMERA_FRAME_WIDTH   320      /* QVGA 分辨率宽度 */
#define CAMERA_FRAME_HEIGHT  240      /* QVGA 分辨率高度 */
#define CAMERA_PIXEL_FORMAT  PIXFORMAT_RGB565  /* 像素格式 */
#define CAMERA_FPS_TARGET    15       /* 目标帧率 */

/* SPI发送分块大小 */
#define SPI_SEND_BLOCK_SIZE  2048     /* 每块2KB，较大块传输更高效 */

/* ============================================================
 *  四、FreeRTOS任务优先级
 *  数值越大优先级越高
 * ============================================================ */

#define TASK_PRIORITY_CAM_CAPTURE  6    /* 图像采集 - 最高优先级 */
#define TASK_PRIORITY_IMG_PROC     5    /* 图像处理 */
#define TASK_PRIORITY_SPI_SEND     6    /* SPI发送 */
#define TASK_PRIORITY_COMM         4    /* 指令处理 */
#define TASK_PRIORITY_HEARTBEAT    2    /* 心跳保活 */

/* ============================================================
 *  五、FreeRTOS任务栈大小（单位: 字节）
 * ============================================================ */

#define TASK_STACK_CAM_CAPTURE     8192 /* 摄像头采集需要较大栈 */
#define TASK_STACK_IMG_PROC        4096
#define TASK_STACK_SPI_SEND        4096
#define TASK_STACK_COMM            2048
#define TASK_STACK_HEARTBEAT       1024

/* ============================================================
 *  六、其他配置
 * ============================================================ */

#define HEARTBEAT_PERIOD_MS        1000  /* 心跳周期 1秒 */
#define DEBUG_SERIAL_BAUDRATE      115200 /* 调试串口波特率 */

#endif /* CONFIG_H */
