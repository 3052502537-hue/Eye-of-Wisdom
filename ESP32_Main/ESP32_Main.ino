/* ============================================================
 * 文件名: ESP32_Main.ino
 * 功能描述: 导盲头环主控板主程序入口 v3.0
 *           v3.0: 彻底移除 SPI 主机通信、Rd-03D 毫米波雷达（前后全删）
 *                 只负责 HC-SR04 超声波 + SDM10 激光 → TCP JSON → 手机
 *                 摄像板通过 WiFi STA 独立连接主控 AP，手机直连摄像板拉流
 * 依赖关系: Arduino Core、config.h、所有模块驱动、task_manager
 * 接口说明: 标准 Arduino setup()/loop()
 *
 * 系统架构 (v3.0):
 *   ┌──────────────────┐   TCP:8888 JSON     ┌──────────────┐
 *   │  ESP32_Main 主控  │ ◄──────────────────► │  手机 App     │
 *   │  HC-SR04 + SDM10  │   传感器数据+命令    │              │
 *   │  WiFi AP 热点     │                     │ 传感器显示    │
 *   └────────┬─────────┘                     │ HC-SR04雷达图 │
 *            │ WiFi AP                       │ 激光距离      │
 *   ┌────────┴─────────┐                     │ AI处理画面    │
 *   │  ESP32_Camera     │                     │ 原始画面      │
 *   │  OV2640 + WiFi    │ ◄── HTTP 拉流 ─── │              │
 *   │  STA 连主控AP     │   /video /capture  └──────────────┘
 *   └──────────────────┘
 *
 * 启动流程 (v3.0: 精简):
 *   上电 -> RGB亮蓝 -> 蜂鸣器短响
 *        -> 初始化传感器UART (SDM10激光)
 *        -> 初始化HC-SR04超声波 (GPIO直连)
 *        -> 传感器自检(失败仅闪烁告警,不阻塞)
 *        -> 启动WiFi AP(无论传感器是否在线,必须启动)
 *        -> 启动TCP服务器
 *        -> 创建FreeRTOS任务(4个)
 *
 * 硬件: ESP32-S3-N16R8
 * 编译: Arduino IDE, 选择 "ESP32S3 Dev Module",
 *       开启 "USB CDC On Boot",
 *       Flash Size=16MB, PSRAM=OPI 8MB
 *
 * 容错原则: 传感器故障不影响WiFi热点和手机连接
 * ============================================================ */

#include <Arduino.h>
#include "config.h"
#include "protocol.h"
#include "task_manager.h"

/* 全局任务管理器实例 */
TaskManager g_tm;

/* 模块在线状态(供任务检查) */
static bool g_laserOnline     = false;
static bool g_ultrasonicOnline = false;

/* ============================================================
 * setup - 系统初始化与启动流程
 *
 * 初始化顺序: 告警 -> UART传感器 -> GPIO传感器 -> 自检 -> WiFi -> 任务
 * ============================================================ */
void setup()
{
    /* ---- 1. 调试串口 (USB CDC) ---- */
#ifdef DEBUG
    Serial.begin(DBG_BAUDRATE);
    delay(500);
    Serial.println();
    Serial.println(F("==== BlindGuide HeadRing Boot v3.0 ===="));
    Serial.println(F("MCU: ESP32-S3-N16R8"));
    Serial.println(F("Sensors: SDM10(Laser) + HC-SR04(Ultrasonic)"));
    Serial.println(F("Camera: Independent ESP32 via WiFi STA"));
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
     * 6. SDM10 激光测距传感器 (UART1)
     *    量程10m, 精度±5cm, 50Hz连续输出
     * ============================================================ */
    Serial.println(F("[MAIN] ---- Sensor Init ----"));

    Serial.println(F("[MAIN] Initializing SDM10 Laser..."));
    if (g_tm.laser().begin()) {
        Serial.println(F("[MAIN] SDM10 UART1 init OK (460800bps)"));
        if (g_tm.laser().selfTest()) {
            g_laserOnline = true;
            Serial.println(F("[MAIN] SDM10 self-test OK"));
        } else {
            Serial.println(F("[MAIN] SDM10 self-test FAIL (will retry in task)"));
        }
    } else {
        Serial.println(F("[MAIN] SDM10 init FAIL"));
    }

    /* ============================================================
     * 7. HC-SR04 超声波传感器 (GPIO4/GPIO5)
     *    量程2cm-400cm, 精度±3mm, 替代前方Rd-03D雷达
     * ============================================================ */
    Serial.println(F("[MAIN] Initializing HC-SR04 Ultrasonic..."));
    if (g_tm.ultrasonic().begin(PIN_HCSR04_TRIG, PIN_HCSR04_ECHO)) {
        /* 快速验证: 读取一次看是否超时 */
        float testDist = g_tm.ultrasonic().readDistance();
        if (testDist > 0) {
            g_ultrasonicOnline = true;
            Serial.printf("[MAIN] HC-SR04 init OK, test=%.2f m\n", testDist);
        } else {
            Serial.println(F("[MAIN] HC-SR04 init OK but no echo (will retry in task)"));
        }
    } else {
        Serial.println(F("[MAIN] HC-SR04 init FAIL"));
    }

    /* ============================================================
     * 8. WiFi AP 热点
     *    手机 + 摄像板均连接此热点
     *    主控IP: 192.168.4.1
     *    摄像板静态IP: 192.168.4.10 (通过JSON告知手机)
     * ============================================================ */
    Serial.println(F("[MAIN] ---- WiFi AP ----"));
    if (!g_tm.wifi().startAP()) {
        Serial.println(F("[MAIN] WiFi AP FAIL!"));
        g_tm.alarm().setSystemState(SYS_STATE_FAULT);
        g_tm.alarm().update();
    } else {
        Serial.printf("[MAIN] AP SSID=%s  IP=%s\n",
                      WIFI_AP_SSID, g_tm.wifi().getApIp().toString().c_str());
        Serial.printf("[MAIN] Camera ESP32 expected at %s\n", CAMERA_ESP32_STATIC_IP);
    }

    /* ---- 9. TCP 服务器 (传感器JSON + 命令) ---- */
    g_tm.wifi().startTcpServer();
    Serial.printf("[MAIN] TCP server on port %d\n", TCP_PORT);

    /* ---- 10. Web 配置服务器 ---- */
    g_tm.web().begin();
    Serial.println(F("[MAIN] Web server on port 80"));

    /* ============================================================
     * 11. 命令回调注册
     *     处理手机App发来的控制命令
     * ============================================================ */
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
                    Serial.printf("[CMD] mode set to %d\n", mode);
                }
            }
            break;
        }
        case CMD_SET_WARN:
            /* 运行时阈值暂用编译期常量, 此处仅日志 */
            Serial.printf("[CMD] warn params: %.*s\n", (int)len, json);
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
        case CMD_CALIBRATE: {
            bool laserOk = g_tm.laser().selfTest();
            char rsp[128];
            snprintf(rsp, sizeof(rsp),
                "{\"type\":\"status\",\"calibrate\":{\"laser\":%s,\"ultrasonic\":true}}",
                laserOk ? "true" : "false");
            g_tm.wifi().sendSensorJson(rsp, strlen(rsp));
            break;
        }
        case CMD_QUERY_STATUS: {
            char rsp[256];
            snprintf(rsp, sizeof(rsp),
                "{\"type\":\"status\",\"mode\":%d,\"clients\":%u,\"uptime\":%lu,"
                "\"camera_ip\":\"%s\"}",
                (int)g_tm.getWorkMode(),
                g_tm.wifi().getClientCount(),
                (unsigned long)millis(),
                CAMERA_ESP32_STATIC_IP);
            g_tm.wifi().sendSensorJson(rsp, strlen(rsp));
            break;
        }
        default:
            break;
        }
    });

    /* ---- 12. 创建 FreeRTOS 任务 (4个，各任务内部检查硬件状态) ---- */
    Serial.println(F("[MAIN] Starting FreeRTOS tasks..."));
    if (!g_tm.startTasks()) {
        Serial.println(F("[MAIN] Task create FAIL!"));
    } else {
        Serial.println(F("[MAIN] All 4 tasks started"));
    }

    /* ---- 13. 进入工作模式 ---- */
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
