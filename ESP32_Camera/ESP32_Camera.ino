/* ============================================================
 *  文件名: ESP32_Camera.ino
 *  功能描述:
 *    导盲头环项目 - 摄像头板(ESP32-S3-WROOM) 主程序
 *    系统主入口，负责协调各模块工作:
 *    1. 初始化 OV2640 摄像头 (VGA 640x480, JPEG 输出)
 *    2. 初始化 SPI 从机通信 (等待主控板读取数据)
 *    3. 创建 FreeRTOS 多任务:
 *       - 摄像头采集任务: 采集图像帧
 *       - SPI 发送任务: 将图像通过 SPI 从机发送给主控板
 *       - 指令处理任务: 处理主控板下发的指令
 *       - 心跳任务: 定期发送心跳保持链路
 *    4. 主循环监控系统状态
 *
 *  主流程 (采集→发送循环):
 *    初始化OV2640 → 设置VGA分辨率 → 设置JPEG输出
 *    → 采集帧 → 简单处理(降分辨率/裁剪可选)
 *    → 通知主控板数据就绪 → 等待主控读取 → 循环
 *
 *  依赖关系:
 *    - config.h (全局配置)
 *    - camera_driver.h (摄像头驱动)
 *    - spi_slave_comm.h (SPI 从机通信)
 *    - Arduino.h (Arduino 核心)
 *    - freertos (FreeRTOS 任务、队列、信号量)
 *
 *  硬件环境:
 *    - 开发板: ESP32-S3-WROOM (带 PSRAM)
 *    - 摄像头: OV2640 (DVP 接口, 200万像素)
 *    - 通信:   SPI 从机模式
 *    - 电源:   18650 锂电池 (5V/3.3V)
 *
 *  Arduino IDE 配置:
 *    - 开发板: ESP32S3 Dev Module
 *    - PSRAM:  Enabled (OPI PSRAM)
 *    - Flash:  8MB+
 *    - 端口:   选择对应 COM 口
 * ============================================================ */

#include <Arduino.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"

#include "config.h"
#include "camera_driver.h"
#include "spi_slave_comm.h"

/* ============================================================
 *  全局对象声明
 * ============================================================ */
CameraDriver   g_camera;              /* 摄像头驱动对象 */
SpiSlaveComm   g_spiSlave;            /* SPI 从机通信对象 */

/* ============================================================
 *  全局变量声明
 * ============================================================ */
static bool          g_systemReady = false;   /* 系统就绪标志 */
static SemaphoreHandle_t g_frameSem = NULL;   /* 帧就绪信号量 */

/* FreeRTOS 任务句柄 */
static TaskHandle_t  g_taskCamCapture = NULL; /* 摄像头采集任务 */
static TaskHandle_t  g_taskSpiSend    = NULL; /* SPI 发送任务 */
static TaskHandle_t  g_taskComm       = NULL; /* 指令处理任务 */
static TaskHandle_t  g_taskHeartbeat  = NULL; /* 心跳任务 */

/* ============================================================
 *  帧数据传输结构体
 *    用于在采集任务和发送任务之间传递帧信息
 *    自带数据缓冲区，避免 use-after-free
 * ============================================================ */
typedef struct {
    uint8_t   data[JPEG_MAX_SIZE];  /* 图像数据缓冲区(拷贝) */
    uint32_t  size;                 /* 数据大小 */
    uint16_t  width;                /* 图像宽度 */
    uint16_t  height;               /* 图像高度 */
    uint8_t   format;               /* 图像格式 */
} TransferFrame_t;

/* 帧传输队列 */
static QueueHandle_t g_frameQueue = NULL;


/* ============================================================
 *  摄像头采集任务
 *    持续采集 JPEG 图像帧，放入发送队列
 *    采集流程: 采集 → (可选处理) → 入队
 * ============================================================ */
static void taskCameraCapture(void* arg)
{
    DBG_PRINTLN("[Task] 摄像头采集任务启动");

    camera_fb_t* fb = NULL;
    uint32_t lastCaptureTime = 0;

    while (1) {
        /* 记录采集开始时间 (用于帧率控制) */
        uint32_t startTime = millis();

        /* --- 采集一帧图像 --- */
        if (!g_camera.capture(&fb)) {
            DBG_PRINTLN("[Task] 采集失败，等待重试...");
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        /* --- 简单图像处理 (预留接口) ---
         * 此处可添加:
         *   - 降分辨率: 将 VGA 降采样到 QVGA
         *   - 裁剪: 裁剪感兴趣区域
         *   - 格式转换: JPEG → 其他格式
         * 当前框架直接使用原始 JPEG 数据
         */

        /* --- 准备传输帧(拷贝数据, 避免 use-after-free) --- */
        TransferFrame_t frame;
        memset(&frame, 0, sizeof(frame));

        uint32_t copySize = fb->len;
        if (copySize > JPEG_MAX_SIZE) {
            copySize = JPEG_MAX_SIZE;  /* 截断超大数据 */
        }
        memcpy(frame.data, fb->buf, copySize);            /* 拷贝JPEG数据 */
        frame.size   = copySize;
        frame.width  = fb->width;
        frame.height = fb->height;
        frame.format = IMG_FMT_JPEG;

        /* 立即释放摄像头帧缓冲(数据已拷贝到frame.data) */
        g_camera.returnFrame(fb);
        fb = NULL;

        /* 尝试放入发送队列 (非阻塞) */
        if (xQueueSend(g_frameQueue, &frame, 0) != pdTRUE) {
            /* 队列满，丢弃当前帧 */
            DBG_PRINTLN("[Task] 发送队列满，丢弃帧");
        }

        /* --- 帧率控制 --- */
        /* 计算本次采集耗时，确保帧间隔满足目标帧率 */
        uint32_t elapsed = millis() - startTime;
        uint32_t waitTime = 0;
        if (elapsed < CAMERA_FRAME_INTERVAL_MS) {
            waitTime = CAMERA_FRAME_INTERVAL_MS - elapsed;
        }
        vTaskDelay(pdMS_TO_TICKS(waitTime));

#ifdef DEBUG
        uint32_t frameInterval = millis() - lastCaptureTime;
        lastCaptureTime = millis();
        DBG_PRINTF("[Task] 采集 #%lu, 耗时%ums, 间隔%ums\n",
                   (unsigned long)g_camera.getFrameCount(),
                   (unsigned)elapsed, (unsigned)frameInterval);
#endif
    }
}


/* ============================================================
 *  SPI 发送任务
 *    从帧队列获取图像数据，通过 SPI 从机发送给主控板
 *    发送流程: 取帧 → 构帧 → DATA_READY通知 → 等待读取 → 清除信号
 * ============================================================ */
static void taskSpiSend(void* arg)
{
    DBG_PRINTLN("[Task] SPI 发送任务启动");

    TransferFrame_t frame;

    while (1) {
        /* 从队列获取待发送帧 (阻塞等待) */
        if (xQueueReceive(g_frameQueue, &frame, portMAX_DELAY) == pdTRUE) {
            if (!frame.data || frame.size == 0) {
                continue;
            }

#ifdef DEBUG
            DBG_PRINTF("[Task] 开始发送帧: %ux%u, %u字节\n",
                       frame.width, frame.height, (unsigned)frame.size);
            uint32_t sendStart = millis();
#endif

            /* 通过 SPI 从机发送图像数据给主控板 */
            /* sendFrame 内部会:
             *   1. 构建协议帧 (元数据 + 图像数据)
             *   2. 拉高 DATA_READY 通知主控板
             *   3. 等待主控板发起 SPI 读取
             *   4. 拉低 DATA_READY
             */
            bool ok = g_spiSlave.sendFrame(
                frame.data,
                frame.size,
                frame.width,
                frame.height,
                frame.format);

            if (!ok) {
                DBG_PRINTLN("[Task] SPI 发送失败");
            }

#ifdef DEBUG
            uint32_t sendTime = millis() - sendStart;
            DBG_PRINTF("[Task] 发送完成, 耗时%ums, %s\n",
                       (unsigned)sendTime, ok ? "成功" : "失败");
#endif
        }
    }
}


/* ============================================================
 *  指令处理任务
 *    接收并处理主控板通过 SPI 下发的指令
 *    支持指令: 设置分辨率、设置帧率、设置JPEG质量
 * ============================================================ */
static void taskCommHandler(void* arg)
{
    DBG_PRINTLN("[Task] 指令处理任务启动");

    RxFrame_t rxFrame;

    while (1) {
        /* 从 SPI 接收队列获取指令 (阻塞等待 100ms) */
        if (xQueueReceive(g_spiSlave.getRxQueue(), &rxFrame,
                          pdMS_TO_TICKS(100)) == pdTRUE) {
            DBG_PRINTF("[Task] 收到指令: cmd=0x%02X, len=%u\n",
                       rxFrame.cmd, rxFrame.dataLen);

            /* 根据命令码执行对应操作 */
            switch (rxFrame.cmd) {
                /* --- 设置分辨率 --- */
                case SPI_CMD_SET_RESOLUTION:
                    if (rxFrame.dataLen >= 1) {
                        uint8_t resId = rxFrame.data[0];
                        DBG_PRINTF("[Task] 设置分辨率: %u\n", resId);
                        switch (resId) {
                            case 0:  /* QVGA */
                                g_camera.setFrameSize(FRAMESIZE_QVGA);
                                break;
                            case 1:  /* VGA */
                                g_camera.setFrameSize(FRAMESIZE_VGA);
                                break;
                            default:
                                DBG_PRINTLN("[Task] 未知分辨率ID");
                                break;
                        }
                    }
                    break;

                /* --- 设置帧率 --- */
                case SPI_CMD_SET_FPS:
                    if (rxFrame.dataLen >= 1) {
                        uint8_t fps = rxFrame.data[0];
                        DBG_PRINTF("[Task] 设置帧率: %u (当前为配置固定值)\n", fps);
                        /* 帧率通过任务延时控制，此处仅记录 */
                        /* 如需动态调整，可修改全局变量并在采集任务中读取 */
                    }
                    break;

                /* --- 设置 JPEG 质量 --- */
                case SPI_CMD_SET_QUALITY:
                    if (rxFrame.dataLen >= 1) {
                        uint8_t quality = rxFrame.data[0];
                        DBG_PRINTF("[Task] 设置JPEG质量: %u\n", quality);
                        g_camera.setQuality(quality);
                    }
                    break;

                /* --- 未知指令 --- */
                default:
                    DBG_PRINTF("[Task] 未知指令: 0x%02X\n", rxFrame.cmd);
                    break;
            }
        }
    }
}


/* ============================================================
 *  心跳任务
 *    定期发送心跳信号，保持与主控板的通信链路
 * ============================================================ */
static void taskHeartbeat(void* arg)
{
    DBG_PRINTLN("[Task] 心跳任务启动");

    while (1) {
        /* 发送心跳 (时间戳) */
        uint32_t timestamp = millis();
        g_spiSlave.sendHeartbeat(timestamp);

        /* 等待下一个心跳周期 */
        vTaskDelay(pdMS_TO_TICKS(HEARTBEAT_PERIOD_MS));
    }
}


/* ============================================================
 *  Arduino setup() 函数
 *    系统初始化入口
 * ============================================================ */
void setup()
{
    /* --- 初始化调试串口 --- */
    Serial.begin(DEBUG_SERIAL_BAUDRATE);
    delay(1000);   /* 等待串口稳定 */

    DBG_PRINTLN("============================================");
    DBG_PRINTLN("  导盲头环 - 摄像头板启动");
    DBG_PRINTLN("  版本: v1.0");
    DBG_PRINTLN("  开发板: ESP32-S3-WROOM");
    DBG_PRINTLN("  摄像头: OV2640 VGA JPEG");
    DBG_PRINTLN("============================================");

    /* --- 创建帧就绪信号量 --- */
    g_frameSem = xSemaphoreCreateBinary();
    if (!g_frameSem) {
        DBG_PRINTLN("[Main] 信号量创建失败!");
    }

    /* --- 创建帧传输队列 --- */
    g_frameQueue = xQueueCreate(FRAME_QUEUE_LENGTH, sizeof(TransferFrame_t));
    if (!g_frameQueue) {
        DBG_PRINTLN("[Main] 帧队列创建失败!");
    }

    /* --- 初始化摄像头 --- */
    DBG_PRINTLN("[Main] 初始化 OV2640 摄像头...");
    if (g_camera.init()) {
        DBG_PRINTF("[Main] 摄像头初始化成功: %dx%d\n",
                   g_camera.getWidth(), g_camera.getHeight());
    } else {
        DBG_PRINTLN("[Main] 摄像头初始化失败! 请检查接线");
        /* 摄像头初始化失败不停止，继续初始化其他模块 */
    }

    /* --- 初始化 SPI 从机通信 --- */
    DBG_PRINTLN("[Main] 初始化 SPI 从机通信...");
    if (g_spiSlave.init()) {
        DBG_PRINTLN("[Main] SPI 从机初始化成功");
    } else {
        DBG_PRINTLN("[Main] SPI 从机初始化失败! 请检查接线");
    }

    /* --- 创建 FreeRTOS 任务 --- */
    DBG_PRINTLN("[Main] 创建 FreeRTOS 任务...");

    /* 摄像头采集任务 (核心1, 高优先级) */
    xTaskCreatePinnedToCore(
        taskCameraCapture,         /* 任务函数 */
        "CamCapture",              /* 任务名称 */
        TASK_STACK_CAM_CAPTURE,    /* 栈大小 */
        NULL,                      /* 参数 */
        TASK_PRIORITY_CAM_CAPTURE, /* 优先级 */
        &g_taskCamCapture,         /* 任务句柄 */
        TASK_CORE_CAM_CAPTURE      /* 运行核心 */
    );

    /* SPI 发送任务 (核心0, 高优先级) */
    xTaskCreatePinnedToCore(
        taskSpiSend,
        "SpiSend",
        TASK_STACK_SPI_SEND,
        NULL,
        TASK_PRIORITY_SPI_SEND,
        &g_taskSpiSend,
        TASK_CORE_SPI_SEND
    );

    /* 指令处理任务 (核心0, 中优先级) */
    xTaskCreatePinnedToCore(
        taskCommHandler,
        "CommHandler",
        TASK_STACK_COMM,
        NULL,
        TASK_PRIORITY_COMM,
        &g_taskComm,
        TASK_CORE_COMM
    );

    /* 心跳任务 (核心0, 低优先级) */
    xTaskCreatePinnedToCore(
        taskHeartbeat,
        "Heartbeat",
        TASK_STACK_HEARTBEAT,
        NULL,
        TASK_PRIORITY_HEARTBEAT,
        &g_taskHeartbeat,
        TASK_CORE_HEARTBEAT
    );

    /* --- 系统初始化完成 --- */
    g_systemReady = true;
    DBG_PRINTLN("[Main] 摄像头板初始化完成，开始运行!");
    DBG_PRINTLN("============================================");
}


/* ============================================================
 *  Arduino loop() 函数
 *    主循环，监控系统状态
 *    实际工作由 FreeRTOS 任务处理，主循环仅做状态监控
 * ============================================================ */
void loop()
{
    /* 系统未就绪时等待 */
    if (!g_systemReady) {
        delay(100);
        return;
    }

#ifdef DEBUG
    /* 调试模式下定期打印系统状态 */
    static uint32_t lastStatusTime = 0;
    uint32_t now = millis();
    if (now - lastStatusTime >= 5000) {   /* 每5秒打印一次 */
        lastStatusTime = now;
        DBG_PRINTF("[Status] 运行时间: %lus | 采集帧数: %lu | 发送帧数: %lu\n",
                   (unsigned long)(now / 1000),
                   (unsigned long)g_camera.getFrameCount(),
                   (unsigned long)g_spiSlave.getTxFrameCount());
    }
#endif

    /* 主循环休眠，将 CPU 让给 FreeRTOS 任务 */
    vTaskDelay(pdMS_TO_TICKS(1000));
}
