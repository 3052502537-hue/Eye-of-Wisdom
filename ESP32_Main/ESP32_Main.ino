/* ============================================================
 * 文件名: ESP32_Main.ino
 * 功能描述: 导盲头环主控板主程序入口
 *           负责 setup() 硬件初始化与启动流程，
 *           loop() 空闲处理(核心逻辑由FreeRTOS任务承担)
 * 依赖关系: Arduino Core、config.h、所有模块驱动、task_manager
 * 接口说明: 标准 Arduino setup()/loop()
 *
 * 启动流程 (v3.0: 容错设计):
 *   上电 -> RGB亮蓝 -> 蜂鸣器短响
 *        -> 初始化三路UART传感器(先于SPI,避免资源冲突)
 *        -> 传感器自检(失败仅闪烁告警,不阻塞)
 *        -> 初始化SPI(后于UART,避免DMA冲突)
 *        -> 启动WiFi AP(无论传感器/SPI是否在线,必须启动)
 *        -> 创建FreeRTOS任务(各任务独立检查硬件状态)
 *
 * 硬件: ESP32-S3-N16R8
 * 编译: Arduino IDE, 选择 "ESP32S3 Dev Module",
 *       开启 "USB CDC On Boot",
 *       Flash Size=16MB, PSRAM=OPI 8MB
 *
 * 容错原则: 传感器故障/摄像头离线不影响WiFi热点和手机连接
 * ============================================================ */

#include <Arduino.h>
#include "config.h"
#include "protocol.h"
#include "task_manager.h"

/* 全局任务管理器实例 */
TaskManager g_tm;

/* 模块在线状态(供任务检查) */
static bool g_spiOnline    = false;
static bool g_laserOnline  = false;
static bool g_radarFOnline = false;
static bool g_radarROnline = false;

/* ============================================================
 * setup - 系统初始化与启动流程
 *
 * 初始化顺序: 告警 -> UART传感器 -> 自检 -> SPI -> WiFi -> 任务
 * 关键: UART 必须在 SPI 之前初始化!
 *       SPIClass::begin() 可能占用 DMA 通道, 影响后续 UART 分配.
 * ============================================================ */
void setup()
{
    /* ---- 1. 调试串口 (USB CDC) ---- */
#ifdef DEBUG
    Serial.begin(DBG_BAUDRATE);
    delay(500);
    Serial.println();
    Serial.println(F("==== BlindGuide HeadRing Boot ===="));
    Serial.println(F("MCU: ESP32-S3-N16R8"));
#endif

    /* ---- 2. 蜂鸣器 + RGB LED ---- */
    g_tm.alarm().begin();
    g_tm.alarm().setSystemState(SYS_STATE_POWER_ON);
    g_tm.alarm().update();

    /* ---- 3. 蜂鸣器短响 ---- */
    g_tm.alarm().beep(2000, 100);

    /* ---- 4. 初始化状态 ---- */
    g_tm.alarm().setSystemState(SYS_STATE_INIT);
    g_tm.alarm().update();

    /* ---- 5. 任务管理器 (互斥锁) ---- */
    if (!g_tm.begin()) {
        Serial.println(F("[MAIN] TaskManager begin FAIL"));
    }

    /* ============================================================
     * 6. 传感器 UART 初始化 (必须在 SPI 之前!)
     *    每个传感器独立初始化, 失败不阻塞后续流程
     * ============================================================ */
    Serial.println(F("[MAIN] ---- Sensor UART Init ----"));

    /* 6a. SDM10 激光 (UART1, GPIO17/18) */
    Serial.println(F("[MAIN] init SDM10 laser (UART1)..."));
    if (g_tm.laser().begin()) {
        Serial.println(F("[MAIN]   SDM10 UART OK"));
    } else {
        Serial.println(F("[MAIN]   SDM10 UART FAIL (continuing)"));
    }

    /* 6b. 前向雷达 (Serial0, GPIO6/7) */
    Serial.println(F("[MAIN] init front radar (UART0)..."));
    if (g_tm.radarFront().begin(RADAR_FRONT_UART, PIN_RADAR_FRONT_TX,
                                 PIN_RADAR_FRONT_RX, RADAR_BAUDRATE)) {
        Serial.println(F("[MAIN]   Front Radar UART OK"));
    } else {
        Serial.println(F("[MAIN]   Front Radar UART FAIL (continuing)"));
    }

    /* 6c. 后向雷达 (Serial2, GPIO4/5) */
    Serial.println(F("[MAIN] init rear radar (UART2)..."));
    if (g_tm.radarRear().begin(RADAR_REAR_UART, PIN_RADAR_REAR_TX,
                                PIN_RADAR_REAR_RX, RADAR_BAUDRATE)) {
        Serial.println(F("[MAIN]   Rear Radar UART OK"));
    } else {
        Serial.println(F("[MAIN]   Rear Radar UART FAIL (continuing)"));
    }

    /* ============================================================
     * 7. 传感器自检 (无传感器时全部超时失败, 正常现象)
     *    红灯闪烁提示, 但不阻塞 WiFi 启动
     * ============================================================ */
    Serial.println(F("[MAIN] ---- Sensor Self-Test ----"));
    g_laserOnline  = g_tm.laser().selfTest();
    g_radarFOnline = g_tm.radarFront().selfTest();
    g_radarROnline = g_tm.radarRear().selfTest();

    Serial.printf("[MAIN] Self-test: SDM10=%s  RadarFront=%s  RadarRear=%s\n",
                  g_laserOnline  ? "OK" : "FAIL",
                  g_radarFOnline ? "OK" : "FAIL",
                  g_radarROnline ? "OK" : "FAIL");

    if (g_laserOnline && g_radarFOnline && g_radarROnline) {
        g_tm.alarm().setSystemState(SYS_STATE_NORMAL);
        g_tm.alarm().update();
        Serial.println(F("[MAIN] All sensors OK -> GREEN"));
    } else {
        /* 红灯闪烁 3 次 + 蜂鸣 (缩短: 3次 ≈ 1.2秒) */
        for (int i = 0; i < 3; i++) {
            g_tm.alarm().setSystemState(SYS_STATE_FAULT);
            g_tm.alarm().update();
            g_tm.alarm().beep(2000, 80);
            delay(150);
            g_tm.alarm().setRgbColor(0, 0, 0);
            delay(150);
        }
        Serial.println(F("[MAIN] Some sensors offline -> RED flash (WiFi will still start)"));
    }

    /* ============================================================
     * 8. SPI 主机通信 (在 UART 之后初始化, 避免 DMA 资源冲突)
     *    摄像头板未连接时失败是正常的, 不影响主控独立工作
     * ============================================================ */
    Serial.println(F("[MAIN] ---- SPI Init ----"));
    if (g_tm.spi().begin()) {
        Serial.println(F("[MAIN] SPI master init OK"));
        if (g_tm.spi().selfTest()) {
            g_spiOnline = true;
            Serial.println(F("[MAIN] SPI self-test OK (camera connected)"));
        } else {
            Serial.println(F("[MAIN] SPI self-test: no camera (continuing)"));
        }
    } else {
        Serial.println(F("[MAIN] SPI init FAIL (continuing without camera)"));
    }

    /* ============================================================
     * 9. WiFi AP 热点 (无论传感器/摄像头状态如何, 必须启动)
     * ============================================================ */
    Serial.println(F("[MAIN] ---- WiFi AP ----"));
    if (!g_tm.wifi().startAP()) {
        Serial.println(F("[MAIN] WiFi AP FAIL!"));
        g_tm.alarm().setSystemState(SYS_STATE_FAULT);
        g_tm.alarm().update();
    } else {
        Serial.printf("[MAIN] AP SSID=%s  IP=%s\n",
                      WIFI_AP_SSID, g_tm.wifi().getApIp().toString().c_str());
    }

    /* ---- 10. TCP/UDP 服务器 ---- */
    g_tm.wifi().startTcpServer();
    g_tm.wifi().startUdpServer();
    Serial.printf("[MAIN] TCP:%d  UDP:%d\n", TCP_PORT, UDP_PORT);

    /* ---- 11. Web 配置服务器 ---- */
    g_tm.web().begin();
    Serial.println(F("[MAIN] Web server on port 80"));

    /* ---- 12. 命令回调 (精简版: 不含 SPI 操作, 安全) ---- */
    g_tm.wifi().setCommandCallback([](AppCommand_t cmd, const char* json, size_t len) {
#ifdef DEBUG
        Serial.printf("[CMD] cmd=%d json=%.*s\n", cmd, (int)len, json);
#endif
        switch (cmd) {
        case CMD_SET_MODE: {
            const char* p = strstr(json, "\"mode\":");
            if (p) {
                int mode = atoi(p + 7);
                if (mode >= MODE_SENSOR_ONLY && mode <= MODE_DEBUG) {
                    g_tm.setWorkMode((WorkMode_t)mode);
                }
            }
            break;
        }
        case CMD_SET_WARN:
            /* 运行时阈值暂用编译期常量, 此处仅日志 */
            break;
        case CMD_SET_IMG:
            /* 图像参数转发(仅在SPI在线时) */
            if (g_spiOnline) {
                const char* resP = strstr(json, "\"res\":");
                const char* qP   = strstr(json, "\"q\":");
                if (resP) {
                    uint8_t res = (uint8_t)atoi(resP + 6);
                    uint8_t d[1] = {res};
                    g_tm.spi().sendCommand(SPI_CMD_SET_RESOLUTION, d, 1);
                }
                if (qP) {
                    uint8_t quality = (uint8_t)atoi(qP + 4);
                    uint8_t d[1] = {quality};
                    g_tm.spi().sendCommand(SPI_CMD_SET_QUALITY, d, 1);
                }
            }
            break;
        case CMD_SET_BUZZER: {
            const char* onP = strstr(json, "\"on\":");
            if (onP) {
                bool on = (atoi(onP + 5) != 0);
                if (on) g_tm.alarm().setBuzzerOn(2000);
                else    g_tm.alarm().setBuzzerOff();
            }
            break;
        }
        case CMD_REBOOT:
            g_tm.wifi().sendSensorJson("{\"type\":\"status\",\"msg\":\"rebooting\"}", 38);
            delay(500);
            ESP.restart();
            break;
        case CMD_CALIBRATE:
            if (g_laserOnline || g_radarFOnline) {
                bool laserOk = g_tm.laser().selfTest();
                bool radarOk = g_tm.radarFront().selfTest();
                char rsp[128];
                snprintf(rsp, sizeof(rsp),
                    "{\"type\":\"status\",\"calibrate\":{\"laser\":%s,\"radar\":%s}}",
                    laserOk ? "true" : "false", radarOk ? "true" : "false");
                g_tm.wifi().sendSensorJson(rsp, strlen(rsp));
            }
            break;
        case CMD_QUERY_STATUS: {
            char rsp[256];
            snprintf(rsp, sizeof(rsp),
                "{\"type\":\"status\",\"mode\":%d,\"clients\":%u,\"uptime\":%lu}",
                (int)g_tm.getWorkMode(),
                g_tm.wifi().getClientCount(),
                (unsigned long)millis());
            g_tm.wifi().sendSensorJson(rsp, strlen(rsp));
            break;
        }
        default:
            break;
        }
    });

    /* ---- 13. 创建 FreeRTOS 任务 (各任务内部检查硬件状态) ---- */
    Serial.println(F("[MAIN] starting FreeRTOS tasks..."));
    if (!g_tm.startTasks()) {
        Serial.println(F("[MAIN] Task create FAIL!"));
    } else {
        Serial.println(F("[MAIN] All tasks started"));
    }

    /* ---- 14. 进入工作模式 ---- */
    g_tm.alarm().setSystemState(SYS_STATE_NORMAL);
    g_tm.alarm().update();
    Serial.println(F("[MAIN] GREEN - system ready"));
    Serial.println(F("==== Boot Complete ===="));
}

/* ============================================================
 * loop - 主循环
 *   核心逻辑由FreeRTOS任务承担,loop仅做低优先级空闲处理
 *   持续刷新RGB动态效果
 * ============================================================ */
void loop()
{
    g_tm.alarm().update();
    delay(50);
}
