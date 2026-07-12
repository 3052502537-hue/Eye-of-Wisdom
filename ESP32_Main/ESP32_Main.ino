/* ============================================================
 * 文件名: ESP32_Main.ino
 * 功能描述: 导盲头环主控板主程序入口
 *           负责 setup() 硬件初始化与启动流程，
 *           loop() 空闲处理(核心逻辑由FreeRTOS任务承担)
 * 依赖关系: Arduino Core、config.h、所有模块驱动、task_manager
 * 接口说明: 标准 Arduino setup()/loop()
 *
 * 启动流程:
 *   上电 -> RGB亮蓝 -> 蜂鸣器短响自检 -> 初始化SPI
 *        -> 初始化三路UART传感器 -> 传感器自检
 *        -> 成功绿灯/失败红灯闪+蜂鸣报警
 *        -> 启动WiFi AP -> 等待手机连接 -> 进入工作模式
 *
 * 硬件: ESP32-S3-N16R8
 * 编译: Arduino IDE, 选择 "ESP32S3 Dev Module",
 *       开启 "USB CDC On Boot"(释放UART0给后雷达),
 *       Flash Size=16MB, PSRAM=OPI 8MB
 * ============================================================ */

#include <Arduino.h>
#include "config.h"
#include "protocol.h"
#include "task_manager.h"

/* 全局任务管理器实例 */
TaskManager g_tm;

/* ============================================================
 * setup - 系统初始化与启动流程
 * ============================================================ */
void setup()
{
    /* ---- 1. 初始化调试串口(USB CDC) ---- */
#ifdef DEBUG
    Serial.begin(DBG_BAUDRATE);
    delay(500);   // 等待USB CDC就绪
    Serial.println();
    Serial.println(F("==== BlindGuide HeadRing Boot ===="));
    Serial.println(F("MCU: ESP32-S3-N16R8"));
#endif

    /* ---- 2. 初始化蜂鸣器+RGB,亮蓝色(电源指示) ---- */
    g_tm.alarm().begin();
    g_tm.alarm().setSystemState(SYS_STATE_POWER_ON);   // 蓝色
    g_tm.alarm().update();

    /* ---- 3. 蜂鸣器短响自检 ---- */
    g_tm.alarm().beep(2000, 100);

    /* ---- 4. 进入初始化状态(蓝色闪烁) ---- */
    g_tm.alarm().setSystemState(SYS_STATE_INIT);
    g_tm.alarm().update();

    /* ---- 5. 初始化任务管理器(创建互斥锁) ---- */
    if (!g_tm.begin()) {
        Serial.println(F("[MAIN] TaskManager begin FAIL"));
    }

    /* ---- 6. 初始化SPI主机通信 ---- */
    Serial.println(F("[MAIN] init SPI master..."));
    if (!g_tm.spi().begin()) {
        Serial.println(F("[MAIN] SPI init FAIL"));
    }
    /* SPI通信自检(摄像头板可能尚未就绪,失败不阻塞) */
    if (g_tm.spi().selfTest()) {
        Serial.println(F("[MAIN] SPI self-test OK"));
    } else {
        Serial.println(F("[MAIN] SPI self-test no response (camera not ready?)"));
    }

    /* ---- 7. 初始化三路UART传感器 ---- */
    Serial.println(F("[MAIN] init sensors..."));
    g_tm.laser().begin();        // SDM10 激光(UART1)
    g_tm.radarFront().begin(RADAR_FRONT_UART, PIN_RADAR_FRONT_TX,
                            PIN_RADAR_FRONT_RX, RADAR_BAUDRATE);   // 前雷达(UART2)
    g_tm.radarRear().begin(RADAR_REAR_UART, PIN_RADAR_REAR_TX,
                           PIN_RADAR_REAR_RX, RADAR_BAUDRATE);     // 后雷达(UART0)

    /* ---- 8. 传感器自检 ---- */
    Serial.println(F("[MAIN] sensor self-test..."));
    bool sdmOk   = g_tm.laser().selfTest();
    bool frontOk = g_tm.radarFront().selfTest();
    bool rearOk  = g_tm.radarRear().selfTest();

    Serial.printf("[MAIN] SDM10:%s  RadarFront:%s  RadarRear:%s\n",
                  sdmOk ? "OK" : "FAIL",
                  frontOk ? "OK" : "FAIL",
                  rearOk ? "OK" : "FAIL");

    /* ---- 9. 自检结果指示 ---- */
    if (sdmOk && frontOk && rearOk) {
        /* 全部成功: 绿灯 */
        g_tm.alarm().setSystemState(SYS_STATE_NORMAL);
        g_tm.alarm().update();
        Serial.println(F("[MAIN] self-test ALL OK -> GREEN"));
    } else {
        /* 失败: 红灯闪烁 + 蜂鸣报警 */
        for (int i = 0; i < 5; i++) {
            g_tm.alarm().setSystemState(SYS_STATE_FAULT);
            g_tm.alarm().update();
            g_tm.alarm().beep(2000, 100);
            delay(200);
            g_tm.alarm().setRgbColor(0, 0, 0);
            delay(200);
        }
        Serial.println(F("[MAIN] self-test FAILED -> RED + BUZZER"));
    }

    /* ---- 10. 启动WiFi AP热点 ---- */
    Serial.println(F("[MAIN] starting WiFi AP..."));
    if (!g_tm.wifi().startAP()) {
        Serial.println(F("[MAIN] WiFi AP FAIL!"));
        g_tm.alarm().setSystemState(SYS_STATE_FAULT);
    }

    /* ---- 11. 启动TCP/UDP服务器 ---- */
    g_tm.wifi().startTcpServer();
    g_tm.wifi().startUdpServer();
    Serial.printf("[MAIN] AP IP: %s  TCP:%d  UDP:%d\n",
                  g_tm.wifi().getApIp().toString().c_str(),
                  TCP_PORT, UDP_PORT);

    /* ---- 12. 启动Web服务器 ---- */
    g_tm.web().begin();
    Serial.println(F("[MAIN] Web server started"));

    /* ---- 13. 注册WiFi命令回调 ---- */
    g_tm.wifi().setCommandCallback([](AppCommand_t cmd, const char* json, size_t len) {
        Serial.printf("[MAIN] App cmd=%d: %s\n", cmd, json);
        /* TODO: 按命令类型处理(set_mode/set_warn/set_img等) */
    });

    /* ---- 14. 进入工作模式: 等待手机连接 ---- */
    Serial.println(F("[MAIN] waiting for phone to connect..."));
    g_tm.alarm().setSystemState(SYS_STATE_NORMAL);
    g_tm.alarm().update();

    /* ---- 15. 创建并启动5个FreeRTOS任务 ---- */
    if (!g_tm.startTasks()) {
        Serial.println(F("[MAIN] task create FAIL!"));
        g_tm.alarm().setSystemState(SYS_STATE_FAULT);
    } else {
        Serial.println(F("[MAIN] all tasks started, entering work mode"));
    }

    Serial.println(F("==== Boot Complete ===="));
}

/* ============================================================
 * loop - 主循环
 *   核心逻辑由FreeRTOS任务承担,loop仅做低优先级空闲处理
 *   持续刷新RGB动态效果,检测WiFi连接状态变化
 * ============================================================ */
void loop()
{
    /* 周期刷新RGB闪烁效果(决策任务也会刷新,这里冗余保底) */
    g_tm.alarm().update();

    /* 短延时,让出CPU给FreeRTOS任务 */
    delay(50);
}
