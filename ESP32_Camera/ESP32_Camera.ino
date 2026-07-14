/* ============================================================
 *  文件名: ESP32_Camera.ino
 *  功能描述:
 *    导盲头环项目 - 摄像头板(ESP32-S3-WROOM) 主程序 v1.1
 *    v1.1: 修复 TransferFrame_t 栈溢出 — 改用 camera_fb_t* 指针队列
 *
 *  主流程:
 *    初始化OV2640 → SPI从机 → 创建任务
 *    Camera任务: 采集 → 队列传fb指针
 *    SPI任务:    取fb → SPI发送 → 归还fb
 * ============================================================ */

#include <Arduino.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"

#include "config.h"
#include "camera_driver.h"
#include "spi_slave_comm.h"

/* 全局对象 */
CameraDriver   g_camera;
SpiSlaveComm   g_spiSlave;

static bool          g_systemReady = false;

/* FreeRTOS 任务句柄 */
static TaskHandle_t  g_taskCamCapture = NULL;
static TaskHandle_t  g_taskSpiSend    = NULL;
static TaskHandle_t  g_taskComm       = NULL;
static TaskHandle_t  g_taskHeartbeat  = NULL;

/* 帧指针队列 (传递 camera_fb_t*, 不拷贝数据, 避免栈溢出) */
static QueueHandle_t g_frameQueue = NULL;


/* ============================================================
 *  摄像头采集任务
 *    采集 JPEG → 入队 fb 指针 → 循环
 * ============================================================ */
static void taskCameraCapture(void* arg)
{
    DBG_PRINTLN("[Task] 摄像头采集任务启动");

    uint32_t lastCaptureTime = 0;

    while (1) {
        uint32_t startTime = millis();

        /* 采集一帧 */
        camera_fb_t* fb = NULL;
        if (!g_camera.capture(&fb)) {
            DBG_PRINTLN("[Task] 采集失败，等待重试...");
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        /* 将 fb 指针放入队列 (非阻塞, 队列满则丢弃旧帧) */
        if (xQueueSend(g_frameQueue, &fb, 0) != pdTRUE) {
            /* 队列满 → SPI发送太慢, 丢弃当前帧 */
            g_camera.returnFrame(fb);
            DBG_PRINTLN("[Task] 发送队列满，丢弃帧");
        }

        /* 帧率控制 */
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
 *    从队列取 fb 指针 → SPI 发送 → 归还 fb
 * ============================================================ */
static void taskSpiSend(void* arg)
{
    DBG_PRINTLN("[Task] SPI 发送任务启动");

    while (1) {
        camera_fb_t* fb = NULL;
        if (xQueueReceive(g_frameQueue, &fb, portMAX_DELAY) == pdTRUE) {
            if (!fb || fb->len == 0) {
                if (fb) g_camera.returnFrame(fb);
                continue;
            }

            /* 关键: 先从PSRAM拷到堆内存, 归还fb, 再SPI发送
             * 避免SPI DMA活跃期间访问PSRAM导致Cache/MMU错误 */
            size_t imgSize = fb->len;
            uint16_t imgW = fb->width, imgH = fb->height;
            uint8_t* imgCopy = (uint8_t*)malloc(imgSize);
            if (!imgCopy) {
                DBG_PRINTLN("[Task] malloc失败, 丢弃帧");
                g_camera.returnFrame(fb);
                continue;
            }
            memcpy(imgCopy, fb->buf, imgSize);
            g_camera.returnFrame(fb);  // 立即归还, 摄像头可继续采集

#ifdef DEBUG
            DBG_PRINTF("[Task] 开始发送帧: %ux%u, %u字节\n", imgW, imgH, (unsigned)imgSize);
            uint32_t sendStart = millis();
#endif

            bool ok = g_spiSlave.sendFrame(imgCopy, imgSize, imgW, imgH, IMG_FMT_JPEG);
            free(imgCopy);

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
 *    接收并处理主控板下发的 SPI 指令
 * ============================================================ */
static void taskCommHandler(void* arg)
{
    DBG_PRINTLN("[Task] 指令处理任务启动");

    RxFrame_t rxFrame;

    while (1) {
        if (xQueueReceive(g_spiSlave.getRxQueue(), &rxFrame,
                          pdMS_TO_TICKS(100)) == pdTRUE) {
            DBG_PRINTF("[Task] 收到指令: cmd=0x%02X, len=%u\n",
                       rxFrame.cmd, rxFrame.dataLen);

            switch (rxFrame.cmd) {
                case SPI_CMD_SET_RESOLUTION:
                    if (rxFrame.dataLen >= 1) {
                        uint8_t resId = rxFrame.data[0];
                        DBG_PRINTF("[Task] 设置分辨率: %u\n", resId);
                        switch (resId) {
                            case 0: g_camera.setFrameSize(FRAMESIZE_QVGA); break;
                            case 1: g_camera.setFrameSize(FRAMESIZE_VGA);  break;
                            default: DBG_PRINTLN("[Task] 未知分辨率ID");   break;
                        }
                    }
                    break;

                case SPI_CMD_SET_FPS:
                    if (rxFrame.dataLen >= 1) {
                        DBG_PRINTF("[Task] 设置帧率: %u (当前为配置固定值)\n", rxFrame.data[0]);
                    }
                    break;

                case SPI_CMD_SET_QUALITY:
                    if (rxFrame.dataLen >= 1) {
                        uint8_t quality = rxFrame.data[0];
                        DBG_PRINTF("[Task] 设置JPEG质量: %u\n", quality);
                        g_camera.setQuality(quality);
                    }
                    break;

                default:
                    DBG_PRINTF("[Task] 未知指令: 0x%02X\n", rxFrame.cmd);
                    break;
            }
        }
    }
}


/* ============================================================
 *  心跳任务
 * ============================================================ */
static void taskHeartbeat(void* arg)
{
    DBG_PRINTLN("[Task] 心跳任务启动");

    while (1) {
        uint32_t timestamp = millis();
        g_spiSlave.sendHeartbeat(timestamp);
        vTaskDelay(pdMS_TO_TICKS(HEARTBEAT_PERIOD_MS));
    }
}


/* ============================================================
 *  Arduino setup()
 * ============================================================ */
void setup()
{
    Serial.begin(DEBUG_SERIAL_BAUDRATE);
    delay(1000);

    DBG_PRINTLN("============================================");
    DBG_PRINTLN("  导盲头环 - 摄像头板启动 v1.1");
    DBG_PRINTLN("  开发板: ESP32-S3-WROOM");
    DBG_PRINTLN("  摄像头: OV2640 VGA JPEG");
    DBG_PRINTLN("============================================");

    /* 创建帧指针队列 (仅存指针, 4字节/槽, 极小内存) */
    g_frameQueue = xQueueCreate(FRAME_QUEUE_LENGTH, sizeof(camera_fb_t*));
    if (!g_frameQueue) {
        DBG_PRINTLN("[Main] 帧队列创建失败!");
    }

    /* 初始化摄像头 */
    DBG_PRINTLN("[Main] 初始化 OV2640 摄像头...");
    if (g_camera.init()) {
        DBG_PRINTF("[Main] 摄像头初始化成功: %dx%d\n",
                   g_camera.getWidth(), g_camera.getHeight());
    } else {
        DBG_PRINTLN("[Main] 摄像头初始化失败! 请检查接线");
    }

    /* 初始化 SPI 从机 */
    DBG_PRINTLN("[Main] 初始化 SPI 从机通信...");
    if (g_spiSlave.init()) {
        DBG_PRINTLN("[Main] SPI 从机初始化成功");
    } else {
        DBG_PRINTLN("[Main] SPI 从机初始化失败!");
    }

    /* 创建 FreeRTOS 任务 */
    DBG_PRINTLN("[Main] 创建 FreeRTOS 任务...");

    xTaskCreatePinnedToCore(
        taskCameraCapture, "CamCapture",
        TASK_STACK_CAM_CAPTURE, NULL,
        TASK_PRIORITY_CAM_CAPTURE, &g_taskCamCapture,
        TASK_CORE_CAM_CAPTURE);

    xTaskCreatePinnedToCore(
        taskSpiSend, "SpiSend",
        TASK_STACK_SPI_SEND, NULL,
        TASK_PRIORITY_SPI_SEND, &g_taskSpiSend,
        TASK_CORE_SPI_SEND);

    xTaskCreatePinnedToCore(
        taskCommHandler, "CommHandler",
        TASK_STACK_COMM, NULL,
        TASK_PRIORITY_COMM, &g_taskComm,
        TASK_CORE_COMM);

    xTaskCreatePinnedToCore(
        taskHeartbeat, "Heartbeat",
        TASK_STACK_HEARTBEAT, NULL,
        TASK_PRIORITY_HEARTBEAT, &g_taskHeartbeat,
        TASK_CORE_HEARTBEAT);

    g_systemReady = true;
    DBG_PRINTLN("[Main] 摄像头板初始化完成，开始运行!");
    DBG_PRINTLN("============================================");
}


/* ============================================================
 *  Arduino loop()
 * ============================================================ */
void loop()
{
    if (!g_systemReady) {
        delay(100);
        return;
    }

#ifdef DEBUG
    static uint32_t lastStatusTime = 0;
    uint32_t now = millis();
    if (now - lastStatusTime >= 5000) {
        lastStatusTime = now;
        DBG_PRINTF("[Status] 运行时间: %lus | 采集: %lu | 发送: %lu\n",
                   (unsigned long)(now / 1000),
                   (unsigned long)g_camera.getFrameCount(),
                   (unsigned long)g_spiSlave.getTxFrameCount());
    }
#endif

    vTaskDelay(pdMS_TO_TICKS(1000));
}
