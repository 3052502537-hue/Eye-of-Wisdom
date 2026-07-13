/* ============================================================
 *  文件名: config.h
 *  功能描述:
 *    导盲头环项目 - 摄像头板(ESP32-S3-WROOM) 全局配置文件
 *    包含 OV2640 DVP 引脚分配、SPI 从机引脚、数据就绪引脚、
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
 *  引脚分配原则:
 *    1. 摄像头 DVP 引脚 (16个) 与 SPI 引脚完全分开，无复用
 *    2. 摄像头数据总线 D0-D7 尽量使用连续 GPIO
 *    3. SPI 从机引脚使用高编号 GPIO，避免与摄像头冲突
 *    4. 数据就绪引脚独立，用于通知主控板
 *
 *  硬件说明:
 *    - 摄像头板: ESP32-S3-WROOM (带 PSRAM)
 *    - 摄像头:   OV2640, 200万像素, DVP 接口
 *    - 主控板:   ESP32-S3-N16R8 (SPI 主机)
 *    - 电源:     18650 锂电池, 5V/3.3V 双路
 * ============================================================ */

#ifndef CONFIG_H
#define CONFIG_H

#include <stdint.h>

/* ============================================================
 *  一、调试配置
 *    通过 #define DEBUG 开启调试输出
 *    在 Arduino IDE 中也可通过 Tools > Debug Level 设置
 * ============================================================ */

// #define DEBUG                  /* 调试总开关，取消注释则开启调试输出 */

#define DEBUG_SERIAL_BAUDRATE    115200   /* 调试串口波特率 */

#ifdef DEBUG
  #define DBG_PRINT(x)           Serial.print(x)       /* 调试打印字符串 */
  #define DBG_PRINTLN(x)         Serial.println(x)     /* 调试打印并换行 */
  #define DBG_PRINTF(fmt, ...)   Serial.printf(fmt, ##__VA_ARGS__)  /* 格式化调试打印 */
#else
  #define DBG_PRINT(x)                                /* 调试关闭时为空操作 */
  #define DBG_PRINTLN(x)
  #define DBG_PRINTF(fmt, ...)
#endif

/* ============================================================
 *  二、OV2640 摄像头引脚配置 (DVP接口, 共16个引脚)
 *    所有引脚与 SPI 引脚完全独立，无复用
 *
 *  注意: GPIO0/GPIO3/GPIO45/GPIO46 为 ESP32-S3 启动配置引脚，
 *        摄像头引脚已避开这些引脚。实际接线请根据开发板原理图核对。
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
 *  三、SPI 从机通信引脚配置
 *    摄像头板为 SPI 从机，主控板为主机
 *    使用 SPI2_HOST (FSPI)，引脚通过 GPIO 矩阵可映射到任意引脚
 *
 *  接线说明:
 *    摄像头板 MOSI  ←→  主控板 MOSI  (主控输出，摄像头输入)
 *    摄像头板 MISO  ←→  主控板 MISO  (摄像头输出，主控输入)
 *    摄像头板 SCLK  ←→  主控板 SCLK  (主控输出时钟)
 *    摄像头板 CS    ←→  主控板 CS    (主控控制片选，低电平有效)
 *    摄像头板 DATA_READY → 主控板 GPIO (摄像头输出就绪信号)
 * ============================================================ */

#define PIN_SPI_MOSI        41         /* SPI 主机输出 → 从机输入 */
#define PIN_SPI_MISO        42         /* SPI 从机输出 → 主机输入 */
#define PIN_SPI_SCLK        40         /* SPI 时钟 (主机输出) */
#define PIN_SPI_CS          39         /* SPI 片选 (低电平有效，主机控制) */

#define SPI_HOST_DEVICE     SPI2_HOST  /* 使用的 SPI 主机号 (FSPI) */
#define SPI_MODE            0          /* SPI 模式 (Mode0: CPOL=0, CPHA=0) */
#define SPI_MAX_TRANSFER    4096       /* SPI 单次最大传输字节数 (受 DMA 限制) */

/* ============================================================
 *  四、数据就绪引脚
 *    摄像头板采集完一帧后，拉高此引脚通知主控板来读取
 *    主控板读取完成后，摄像头板拉低此引脚
 *
 *  信号时序:
 *    1. 摄像头采集一帧 → DATA_READY 拉高
 *    2. 主控检测到 DATA_READY 高电平 → 发起 SPI 读取
 *    3. SPI 传输完成 → DATA_READY 拉低
 *    4. 等待下一帧采集完成，重复
 * ============================================================ */

#define PIN_DATA_READY      38         /* 数据就绪通知引脚 (输出) */
#define DATA_READY_ACTIVE   LOW        /* 就绪信号有效电平 (低电平, 与主控FALLING一致) */
#define DATA_READY_IDLE     HIGH       /* 就绪信号空闲电平 (高电平) */

/* ============================================================
 *  五、图像参数配置
 *    VGA 分辨率 640x480，JPEG 压缩输出
 * ============================================================ */

#define CAMERA_FRAME_WIDTH    640      /* 图像宽度 (VGA) */
#define CAMERA_FRAME_HEIGHT   480      /* 图像高度 (VGA) */
#define CAMERA_PIXEL_FORMAT   PIXFORMAT_JPEG  /* 像素格式: JPEG 压缩 */
#define CAMERA_FRAME_SIZE     FRAMESIZE_VGA   /* 帧尺寸枚举: VGA */
#define CAMERA_JPEG_QUALITY   10       /* JPEG 质量 (数值越小质量越高，5-63) */
#define CAMERA_XCLK_FREQ      20000000 /* 摄像头主时钟频率 20MHz */
#define CAMERA_FB_COUNT       2        /* 帧缓冲区数量 (双缓冲) */
#define CAMERA_FB_LOCATION    CAMERA_FB_IN_PSRAM  /* 帧缓冲存放位置: PSRAM */

#define CAMERA_FPS_TARGET     6        /* 目标帧率 6fps (5-8fps 范围) */
#define CAMERA_FRAME_INTERVAL_MS  (1000 / CAMERA_FPS_TARGET)  /* 帧间隔(ms) */

/* JPEG 图像最大尺寸预估 (VGA JPEG 通常 10-50KB) */
#define JPEG_MAX_SIZE         (60 * 1024)  /* JPEG 最大 60KB */

/* ============================================================
 *  六、SPI 通信协议常量(与主控板 protocol.h 一致)
 *    帧格式: |AA 55|cmd(1B)|len(2B LE)|data(NB)|CRC8(1B)|55 AA|
 * ============================================================ */

#define SPI_FRAME_HEADER      0xAA55   /* 帧头 (2字节) */
#define SPI_FRAME_TAIL        0x55AA   /* 帧尾 (2字节) */
#define SPI_FRAME_HEADER_LEN  2        /* 帧头长度 */
#define SPI_FRAME_TAIL_LEN    2        /* 帧尾长度 */
#define SPI_FRAME_CMD_LEN     1        /* 命令字段长度 */
#define SPI_FRAME_LEN_LEN     2        /* 数据长度字段 (2字节小端, 与主控一致) */
#define SPI_FRAME_CRC_LEN     1        /* CRC 校验长度 */
/* 帧开销 = 帧头 + 命令 + 长度 + CRC + 帧尾 = 8字节 */
#define SPI_FRAME_OVERHEAD    (SPI_FRAME_HEADER_LEN + SPI_FRAME_CMD_LEN + \
                               SPI_FRAME_LEN_LEN + SPI_FRAME_CRC_LEN + SPI_FRAME_TAIL_LEN)

/* SPI 命令码定义(与主控板 protocol.h SpiCommand_t 一致) */
#define SPI_CMD_IMG_FRAME_START   0x01   /* 图像帧起始(元数据) */
#define SPI_CMD_IMG_FRAME_DATA    0x02   /* 图像帧数据块 */
#define SPI_CMD_IMG_FRAME_END     0x03   /* 图像帧结束 */
#define SPI_CMD_SET_RESOLUTION    0x10   /* 设置分辨率 (主控下发) */
#define SPI_CMD_SET_FPS           0x11   /* 设置帧率 (主控下发) */
#define SPI_CMD_SET_QUALITY       0x12   /* 设置JPEG质量 (主控下发) */
#define SPI_CMD_ACK               0x20   /* 应答 */
#define SPI_CMD_NACK              0x21   /* 否定应答 */
#define SPI_CMD_HEARTBEAT         0x30   /* 心跳信号 */

/* 图像格式标识 */
#define IMG_FMT_JPEG          2        /* JPEG 格式 */
#define IMG_FMT_RGB565        0        /* RGB565 格式 */
#define IMG_FMT_GRAYSCALE     1        /* 灰度格式 */

/* ============================================================
 *  七、FreeRTOS 任务配置
 *    摄像头板运行多个 FreeRTOS 任务
 *    数值越大优先级越高
 * ============================================================ */

/* 任务优先级 */
#define TASK_PRIORITY_CAM_CAPTURE   7    /* 图像采集 - 最高优先级 */
#define TASK_PRIORITY_SPI_SEND      6    /* SPI 发送 */
#define TASK_PRIORITY_IMG_PROC      5    /* 图像处理 */
#define TASK_PRIORITY_COMM          4    /* 指令处理 */
#define TASK_PRIORITY_HEARTBEAT     2    /* 心跳保活 */

/* 任务栈大小 (单位: 字节) */
#define TASK_STACK_CAM_CAPTURE      8192  /* 摄像头采集需要较大栈 */
#define TASK_STACK_SPI_SEND         6144  /* SPI 发送任务栈 */
#define TASK_STACK_IMG_PROC         4096  /* 图像处理任务栈 */
#define TASK_STACK_COMM             3072  /* 指令处理任务栈 */
#define TASK_STACK_HEARTBEAT        2048  /* 心跳任务栈 */

/* 任务运行核心分配 (ESP32-S3 双核: 0=PRO_CPU, 1=APP_CPU) */
#define TASK_CORE_CAM_CAPTURE       1    /* 摄像头采集在 APP_CPU */
#define TASK_CORE_SPI_SEND          0    /* SPI 发送在 PRO_CPU */
#define TASK_CORE_IMG_PROC          1    /* 图像处理在 APP_CPU */
#define TASK_CORE_COMM              0    /* 指令处理在 PRO_CPU */
#define TASK_CORE_HEARTBEAT         0    /* 心跳在 PRO_CPU */

/* ============================================================
 *  八、其他系统配置
 * ============================================================ */

#define HEARTBEAT_PERIOD_MS         2000  /* 心跳周期 2秒 */
#define SPI_TRANS_TIMEOUT_MS        5000  /* SPI 传输超时 5秒 */
#define FRAME_QUEUE_LENGTH          2     /* 帧队列长度 */
#define MAX_RX_QUEUE_LENGTH         5     /* 接收指令队列长度 */

/* ============================================================
 *  九、扩展接口预留 (供未来功能扩展使用)
 * ============================================================ */

#define PIN_EXPAND_IO_0       21   /* 扩展 IO 0 (状态指示灯等) */
#define PIN_EXPAND_IO_1       47   /* 扩展 IO 1 */
#define PIN_EXPAND_IO_2       48   /* 扩展 IO 2 */
#define PIN_EXPAND_IO_3       14   /* 扩展 IO 3 (注意避免与摄像头D4冲突，可调) */

#endif /* CONFIG_H */
