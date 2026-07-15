/* ============================================================
 *  文件名: ESP32_Camera.ino
 *  功能描述:
 *    导盲头环项目 - 摄像头板(ESP32-S3-WROOM) 主程序 v2.0
 *    v2.0: 彻底移除 SPI 从机通信
 *          改为 WiFi STA 连接主控 AP，HTTP MJPEG 直传手机
 *          摄像板与主控板无硬件连接，完全独立
 *
 *  主流程:
 *    上电 → 初始化OV2640 → 连接主控WiFi(STA) → 启动HTTP服务器
 *    Camera任务: 采集JPEG → 更新帧缓存
 *    HTTP任务:   接受客户端 → 推送MJPEG流 / 响应单帧请求
 *    WiFi任务:   监控连接状态 → 断线自动重连
 *
 *  通信架构 (v2.0):
 *    ┌──────────────────┐                    ┌──────────────┐
 *    │  ESP32_Camera     │  WiFi STA          │  ESP32_Main   │
 *    │  192.168.4.10     │ ◄────────────────► │  WiFi AP      │
 *    │  HTTP :80         │                    │  192.168.4.1  │
 *    └────────┬─────────┘                    └──────────────┘
 *             │
 *             │ HTTP MJPEG (/video) + 单帧 (/capture)
 *             ▼
 *    ┌──────────────┐
 *    │  手机 App     │
 *    │  CameraHttp   │
 *    │  Client       │
 *    └──────────────┘
 *
 *  硬件: ESP32-S3-WROOM + OV2640
 *  编译: Arduino IDE, 选择 "ESP32S3 Dev Module",
 *        PSRAM=OPI 8MB, Flash=16MB
 * ============================================================ */

#include <Arduino.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"

#include "config.h"
#include "camera_driver.h"
#include "camera_web_server.h"

/* 全局对象 */
CameraDriver     g_camera;
CameraWebServer  g_httpServer;

static bool      g_systemReady   = false;
static bool      g_wifiConnected = false;

/* FreeRTOS 任务句柄 */
static TaskHandle_t  g_taskCamCapture  = NULL;
static TaskHandle_t  g_taskHttpServer  = NULL;
static TaskHandle_t  g_taskWifiMonitor = NULL;

/* 帧指针队列 (camera_fb_t*, 采集→HTTP) */
static QueueHandle_t g_frameQueue = NULL;

/* ============================================================
 *  WiFi STA 连接函数
 *    连接主控ESP32的AP热点，设置静态IP
 *    失败时自动重试
 * ============================================================ */
static bool connectToAP()
{
    if (g_wifiConnected) return true;

    DBG_PRINTF("[WiFi] Connecting to AP: %s\n", WIFI_STA_SSID);

    WiFi.mode(WIFI_STA);

    /* 配置静态IP (手机App通过此IP访问摄像板) */
    IPAddress localIp, gateway, subnet;
    if (!localIp.fromString(WIFI_STA_STATIC_IP) ||
        !gateway.fromString(WIFI_STA_GATEWAY) ||
        !subnet.fromString(WIFI_STA_SUBNET)) {
        DBG_PRINTLN("[WiFi] Invalid IP config");
        return false;
    }
    WiFi.config(localIp, gateway, subnet);

    /* 开始连接 */
    WiFi.begin(WIFI_STA_SSID, WIFI_STA_PASSWORD);

    /* 等待连接(最多10秒) */
    int retries = 0;
    while (WiFi.status() != WL_CONNECTED && retries < 20) {
        delay(500);
        DBG_PRINT(".");
        retries++;
    }
    DBG_PRINTLN();

    if (WiFi.status() == WL_CONNECTED) {
        g_wifiConnected = true;
        DBG_PRINTF("[WiFi] Connected! IP: %s\n", WiFi.localIP().toString().c_str());
        DBG_PRINTF("[WiFi] Signal strength: %d dBm\n", WiFi.RSSI());
        return true;
    } else {
        DBG_PRINTF("[WiFi] Connection FAIL (status=%d)\n", WiFi.status());
        return false;
    }
}

/* ============================================================
 *  摄像头采集任务
 *    采集 JPEG → 入队 fb 指针 → 循环
 *    HTTP服务器从队列取帧推送给客户端
 * ============================================================ */
static void taskCameraCapture(void* arg)
{
    DBG_PRINTLN("[Task] Camera capture task started");

    while (1) {
        uint32_t startTime = millis();

        /* 采集一帧 */
        camera_fb_t* fb = NULL;
        if (!g_camera.capture(&fb)) {
            DBG_PRINTLN("[Task] Capture failed, retry...");
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        /* 更新HTTP服务器的帧缓存(供/capture和/video使用) */
        g_httpServer.updateFrame(fb->buf, fb->len, fb->width, fb->height);

        /* 归还帧缓冲 */
        g_camera.returnFrame(fb);

        /* 帧率控制 */
        uint32_t elapsed = millis() - startTime;
        int32_t wait = CAMERA_FRAME_INTERVAL_MS - (int32_t)elapsed;
        if (wait < 0) wait = 0;
        vTaskDelay(pdMS_TO_TICKS(wait));

#ifdef DEBUG
        static uint32_t lastDbg = 0;
        if (millis() - lastDbg >= 5000) {
            lastDbg = millis();
            DBG_PRINTF("[Task] Frames captured: %lu, HTTP clients: %d\n",
                       (unsigned long)g_camera.getFrameCount(),
                       g_httpServer.getClientCount());
        }
#endif
    }
}

/* ============================================================
 *  HTTP 服务任务
 *    周期处理HTTP请求: 接受新客户端、推送MJPEG帧、响应单帧请求
 *    与 CameraWebServer 协作
 * ============================================================ */
static void taskHttpServer(void* arg)
{
    DBG_PRINTLN("[Task] HTTP server task started");

    while (1) {
        if (g_systemReady && g_httpServer.isRunning()) {
            /* 处理所有未决的HTTP请求和MJPEG帧推送 */
            g_httpServer.handleClients();
        }

        /* 高速轮询 (5ms) 确保MJPEG流低延迟 */
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}

/* ============================================================
 *  WiFi 连接监控任务
 *    监控WiFi连接状态，断线自动重连
 *    定期打印状态信息
 * ============================================================ */
static void taskWifiMonitor(void* arg)
{
    DBG_PRINTLN("[Task] WiFi monitor task started");

    while (1) {
        if (WiFi.status() != WL_CONNECTED) {
            if (g_wifiConnected) {
                DBG_PRINTLN("[Task] WiFi disconnected! Reconnecting...");
                g_wifiConnected = false;
            }
            connectToAP();
        }

        /* 每30秒输出一次状态 */
#ifdef DEBUG
        static uint32_t lastStatus = 0;
        if (millis() - lastStatus >= 30000) {
            lastStatus = millis();
            DBG_PRINTF("[Status] WiFi: %s | IP: %s | RSSI: %d | HTTP clients: %d | "
                       "Frames: cam=%lu sent=%lu\n",
                       g_wifiConnected ? "OK" : "DOWN",
                       WiFi.localIP().toString().c_str(),
                       WiFi.RSSI(),
                       g_httpServer.getClientCount(),
                       (unsigned long)g_camera.getFrameCount(),
                       (unsigned long)g_httpServer.getFrameCount());
        }
#endif

        vTaskDelay(pdMS_TO_TICKS(2000));
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
    DBG_PRINTLN("  导盲头环 - 摄像头板启动 v2.0");
    DBG_PRINTLN("  开发板: ESP32-S3-WROOM");
    DBG_PRINTLN("  摄像头: OV2640 VGA JPEG");
    DBG_PRINTLN("  通信:   WiFi STA + HTTP MJPEG 直传手机");
    DBG_PRINTLN("============================================");

    /* ---- 1. 初始化 OV2640 摄像头 ---- */
    DBG_PRINTLN("[Main] Initializing OV2640...");
    if (g_camera.init()) {
        DBG_PRINTF("[Main] Camera init OK: %dx%d JPEG quality=%d\n",
                   g_camera.getWidth(), g_camera.getHeight(), CAMERA_JPEG_QUALITY);
    } else {
        DBG_PRINTLN("[Main] Camera init FAIL! Check wiring.");
        /* 即使摄像头失败也继续 — WiFi+HTTP仍可用 */
    }

    /* ---- 2. 连接主控AP (WiFi STA模式) ---- */
    DBG_PRINTLN("[Main] Connecting to main AP...");
    if (!connectToAP()) {
        DBG_PRINTLN("[Main] WiFi initial connect FAIL (will retry in monitor task)");
    }

    /* ---- 3. 启动 HTTP 服务器 (MJPEG + capture) ---- */
    DBG_PRINTLN("[Main] Starting HTTP server...");
    if (g_httpServer.begin(HTTP_SERVER_PORT)) {
        DBG_PRINTF("[Main] HTTP server on port %d\n", HTTP_SERVER_PORT);
        DBG_PRINTF("[Main]   MJPEG stream: http://%s%s\n",
                   WIFI_STA_STATIC_IP, MJPEG_STREAM_PATH);
        DBG_PRINTF("[Main]   Capture:      http://%s%s\n",
                   WIFI_STA_STATIC_IP, CAPTURE_PATH);
        DBG_PRINTF("[Main]   Status:       http://%s%s\n",
                   WIFI_STA_STATIC_IP, STATUS_PATH);
    } else {
        DBG_PRINTLN("[Main] HTTP server FAIL!");
    }

    /* ---- 4. 创建 FreeRTOS 任务 ---- */
    DBG_PRINTLN("[Main] Creating FreeRTOS tasks...");

    xTaskCreatePinnedToCore(
        taskCameraCapture, "CamCapture",
        TASK_STACK_CAM_CAPTURE, NULL,
        TASK_PRIORITY_CAM_CAPTURE, &g_taskCamCapture,
        TASK_CORE_CAM_CAPTURE);

    xTaskCreatePinnedToCore(
        taskHttpServer, "HttpServer",
        TASK_STACK_HTTP_SERVER, NULL,
        TASK_PRIORITY_HTTP_SERVER, &g_taskHttpServer,
        TASK_CORE_HTTP_SERVER);

    xTaskCreatePinnedToCore(
        taskWifiMonitor, "WifiMonitor",
        TASK_STACK_WIFI_MONITOR, NULL,
        TASK_PRIORITY_WIFI_MONITOR, &g_taskWifiMonitor,
        TASK_CORE_WIFI_MONITOR);

    g_systemReady = true;
    DBG_PRINTLN("[Main] Camera board ready!");
    DBG_PRINTLN("============================================");
}

/* ============================================================
 *  Arduino loop()
 *    FreeRTOS 任务处理核心逻辑，loop 空闲
 * ============================================================ */
void loop()
{
    if (!g_systemReady) {
        delay(100);
        return;
    }

    /* 空闲等待 — 所有工作由 FreeRTOS 任务完成 */
    vTaskDelay(pdMS_TO_TICKS(1000));
}
