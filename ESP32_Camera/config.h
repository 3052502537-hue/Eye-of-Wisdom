/* ============================================================
 *  文件名: config.h
 *  功能描述:
 *    导盲头环项目 - 摄像头板(ESP32-S3-WROOM) 全局配置文件 v2.0
 *    v2.0: 删除 SPI 从机引脚和协议，改为 WiFi STA + HTTP MJPEG 直传手机
 *    包含 OV2640 DVP 引脚分配、WiFi STA 配置、HTTP 服务器配置、
 *    图像参数、FreeRTOS 任务配置等所有可调参数。
 *    修改硬件接线后只需修改本文件即可。
 *
 *  依赖关系:
 *    无外部依赖，仅依赖标准stdint.h
 *    被所有其他文件引用
 *
 *  接口说明:
 *    提供宏定义形式的引脚号和参数，供各模块直接引用
 *
 *  系统架构 (v2.0):
 *    ┌──────────────────┐   WiFi STA         ┌──────────────┐
 *    │  ESP32_Camera     │ ◄────────────────► │  ESP32_Main   │
 *    │  OV2640 + HTTP    │   连接主控AP        │  WiFi AP      │
 *    └────────┬─────────┘                    └──────────────┘
 *             │ HTTP :80                                  │
 *             │ /video (MJPEG流)                          │
 *             │ /capture (单帧)                           │
 *             │ /status  (状态)                           │
 *    ┌────────┴─────────┐                                │
 *    │  手机 App         │ ◄──────────────────────────────┘
 *    │  CameraHttpClient │   TCP:8888 JSON
 *    └──────────────────┘
 *
 *  硬件说明:
 *    - 摄像头板: ESP32-S3-WROOM (带 PSRAM)
 *    - 摄像头:   OV2640, 200万像素, DVP 接口
 *    - 电源:     18650 锂电池, 5V/3.3V 双路
 * ============================================================ */

#ifndef CONFIG_H
#define CONFIG_H

#include <stdint.h>

/* ============================================================
 *  一、调试配置
 *    通过 #define DEBUG 开启调试输出
 * ============================================================ */

#define DEBUG                  /* 调试总开关，取消注释则开启调试输出 */

#define DEBUG_SERIAL_BAUDRATE    115200   /* 调试串口波特率 */

#ifdef DEBUG
  #define DBG_PRINT(x)           Serial.print(x)
  #define DBG_PRINTLN(x)         Serial.println(x)
  #define DBG_PRINTF(fmt, ...)   Serial.printf(fmt, ##__VA_ARGS__)
#else
  #define DBG_PRINT(x)
  #define DBG_PRINTLN(x)
  #define DBG_PRINTF(fmt, ...)
#endif

/* ============================================================
 *  二、OV2640 摄像头引脚配置 (DVP接口, 共16个引脚)
 *    所有引脚独立使用，无SPI冲突
 *
 *  注意: GPIO0/GPIO3/GPIO45/GPIO46 为 ESP32-S3 启动配置引脚，
 *        摄像头引脚已避开这些引脚。
 * ============================================================ */

#define PIN_CAMERA_PWDN    -1         /* 掉电控制 (高电平掉电)，-1 表示未连接 */
#define PIN_CAMERA_RESET   -1         /* 复位引脚 (低电平复位)，-1 表示未连接 */
#define PIN_CAMERA_XCLK    15         /* 主时钟输出，OV2640 通常用 20MHz */
#define PIN_CAMERA_SIOD    4          /* SCCB 数据线 (I2C SDA) */
#define PIN_CAMERA_SIOC    5          /* SCCB 时钟线 (I2C SCL) */
#define PIN_CAMERA_VSYNC   6          /* 帧同步信号输入 */
#define PIN_CAMERA_HREF    7          /* 行同步信号输入 */
#define PIN_CAMERA_PCLK    13         /* 像素时钟输入 */
#define PIN_CAMERA_D7      16         /* 数据位7 (MSB) */
#define PIN_CAMERA_D6      17         /* 数据位6 */
#define PIN_CAMERA_D5      18         /* 数据位5 */
#define PIN_CAMERA_D4      12         /* 数据位4 */
#define PIN_CAMERA_D3      10         /* 数据位3 */
#define PIN_CAMERA_D2      8          /* 数据位2 */
#define PIN_CAMERA_D1      9          /* 数据位1 */
#define PIN_CAMERA_D0      11         /* 数据位0 (LSB) */

/* ============================================================
 *  三、WiFi STA 配置 (v2.0: 摄像板连接主控AP)
 *    摄像板作为 WiFi 客户端连接到主控ESP32的AP热点
 *    手机App通过摄像板的IP地址拉取HTTP视频流
 * ============================================================ */

#define WIFI_STA_SSID            "BlindGuide_AP"    /* 主控AP热点名称 */
#define WIFI_STA_PASSWORD        "12345678"         /* 主控AP密码 */
#define WIFI_STA_STATIC_IP       "192.168.4.10"     /* 摄像板静态IP */
#define WIFI_STA_GATEWAY         "192.168.4.1"      /* 网关(主控AP IP) */
#define WIFI_STA_SUBNET          "255.255.255.0"    /* 子网掩码 */
#define WIFI_STA_RETRY_INTERVAL  5000               /* WiFi重连间隔(ms) */
#define WIFI_STA_MAX_RETRIES     10                 /* 最大重试次数 */

/* ============================================================
 *  四、HTTP 服务器配置 (v2.0: MJPEG视频流)
 *    摄像板运行HTTP服务器，手机App拉取视频流
 * ============================================================ */

#define HTTP_SERVER_PORT         80                 /* HTTP服务端口 */
#define MJPEG_STREAM_PATH        "/video"           /* MJPEG视频流端点 */
#define CAPTURE_PATH             "/capture"         /* 单帧捕获端点 */
#define STATUS_PATH              "/status"          /* 状态查询端点 */
#define MJPEG_BOUNDARY           "frame"            /* MJPEG boundary分隔符 */
#define MJPEG_MAX_CLIENTS        3                  /* 最大MJPEG客户端数 */

/* ============================================================
 *  五、图像参数配置
 *    VGA 分辨率 640x480，JPEG 压缩输出
 *    QVGA 320x240 用于调试/低带宽场景
 * ============================================================ */

#define CAMERA_FRAME_WIDTH    640      /* 图像宽度 (VGA) */
#define CAMERA_FRAME_HEIGHT   480      /* 图像高度 (VGA) */
#define CAMERA_PIXEL_FORMAT   PIXFORMAT_JPEG  /* 像素格式: JPEG 压缩 */
#define CAMERA_FRAME_SIZE     FRAMESIZE_VGA   /* 帧尺寸枚举: VGA */
#define CAMERA_JPEG_QUALITY   8        /* JPEG 质量 (0-63, 越小质量越高) */
#define CAMERA_XCLK_FREQ      20000000 /* 摄像头主时钟频率 20MHz */
#define CAMERA_FB_COUNT       2        /* 帧缓冲区数量 (双缓冲) */
#define CAMERA_FB_LOCATION    CAMERA_FB_IN_PSRAM  /* 帧缓冲存放位置: PSRAM */

#define CAMERA_FPS_TARGET     10       /* 目标帧率 10fps */
#define CAMERA_FRAME_INTERVAL_MS  (1000 / CAMERA_FPS_TARGET)  /* 帧间隔(ms) */

/* JPEG 图像最大尺寸预估 (VGA JPEG 通常 15-50KB) */
#define JPEG_MAX_SIZE         (80 * 1024)   /* JPEG 最大 80KB */

/* ============================================================
 *  六、FreeRTOS 任务配置
 *    摄像板运行 FreeRTOS 任务
 *    数值越大优先级越高
 * ============================================================ */

/* 任务优先级 */
#define TASK_PRIORITY_CAM_CAPTURE   7    /* 图像采集 - 最高优先级 */
#define TASK_PRIORITY_HTTP_SERVER   5    /* HTTP服务器 */
#define TASK_PRIORITY_WIFI_MONITOR  4    /* WiFi连接监控 */

/* 任务栈大小 (单位: 字节) */
#define TASK_STACK_CAM_CAPTURE      8192  /* 摄像头采集需要较大栈 */
#define TASK_STACK_HTTP_SERVER      8192  /* HTTP服务器任务栈 */
#define TASK_STACK_WIFI_MONITOR     4096  /* WiFi监控任务栈 */

/* 任务运行核心分配 (ESP32-S3 双核: 0=PRO_CPU, 1=APP_CPU) */
#define TASK_CORE_CAM_CAPTURE       1    /* 摄像头采集在 APP_CPU */
#define TASK_CORE_HTTP_SERVER       0    /* HTTP服务器在 PRO_CPU */
#define TASK_CORE_WIFI_MONITOR      0    /* WiFi监控在 PRO_CPU */

/* ============================================================
 *  七、其他系统配置
 * ============================================================ */

#define FRAME_QUEUE_LENGTH          2     /* 帧队列长度 */
#define HEARTBEAT_PERIOD_MS         5000  /* 心跳周期 5秒 */

/* ============================================================
 *  八、扩展接口预留
 * ============================================================ */

#define PIN_EXPAND_IO_0       21   /* 扩展 IO 0 */
#define PIN_EXPAND_IO_1       47   /* 扩展 IO 1 */
#define PIN_EXPAND_IO_2       48   /* 扩展 IO 2 */
#define PIN_EXPAND_IO_3       14   /* 扩展 IO 3 */

#endif /* CONFIG_H */
