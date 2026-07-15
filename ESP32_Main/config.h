/* ============================================================
 * 文件名: config.h
 * 功能描述: 导盲头环主控板（ESP32-S3-N16R8）全局配置中心 v3.0
 *           v3.0: 彻底移除 Rd-03D 毫米波雷达、SPI 主机通信
 *                 保留 HC-SR04 超声波 + SDM10 激光测距
 *                 摄像板独立通过WiFi STA连接主控AP，手机直连摄像板HTTP拉流
 *                 任务精简为 4 个（无 SPI 任务）
 *           集中管理所有引脚定义、硬件参数、WiFi配置、任务调度参数、
 *           预警阈值等。修改硬件接线后只需修改本文件即可。
 * 依赖关系: 无外部依赖，被所有其他模块引用
 * 接口说明: 仅提供宏定义，无函数接口
 *
 * 硬件平台: ESP32-S3-N16R8（16MB Flash / 8MB PSRAM，八线PSRAM）
 * 开发环境: Arduino IDE + ESP32 Arduino Core（需开启 USB CDC On Boot）
 *
 * 系统架构 (v3.0):
 *   ┌──────────────────┐       TCP:8888 (JSON)      ┌──────────┐
 *   │  ESP32_Main 主控  │ ◄────────────────────────► │ 手机 App  │
 *   │  HC-SR04 + SDM10  │                            │          │
 *   │  WiFi AP 热点     │                            │ 传感器显示 │
 *   └────────┬─────────┘                            │ AI视觉    │
 *            │ WiFi AP                               └────┬─────┘
 *   ┌────────┴─────────┐                            HTTP拉流 │
 *   │  ESP32_Camera    │ ◄──────────────────────────┤ /video   │
 *   │  OV2640 + WiFi   │                            │ /capture │
 *   │  STA 连接主控AP   │                            └──────────┘
 *   └──────────────────┘
 *
 * 引脚分配总览（v3.0 精简版）:
 *   UART1  -> SDM10 前向激光测距          (TX=GPIO17, RX=GPIO18)
 *   GPIO   -> HC-SR04 前向超声波           (Trig=GPIO4, Echo=GPIO5)
 *   蜂鸣器 PWM                            (GPIO47)
 *   WS2812 RGB LED                        (GPIO48)
 *   调试串口 -> USB CDC（不占用任何UART引脚）
 *
 * 已释放引脚（可供未来扩展）:
 *   GPIO6-13, GPIO19-20, GPIO38-45
 * ============================================================ */

#ifndef CONFIG_H
#define CONFIG_H

#include <stdint.h>

/* ============================================================
 * 一、全局调试开关
 *    打开后各模块通过 USB CDC(Serial) 输出调试信息
 *    使用方式: 在 Arduino IDE 工具菜单开启 "USB CDC On Boot"
 * ============================================================ */
#define DEBUG                       // 调试总开关，注释掉则关闭所有调试输出

#ifdef DEBUG
  #define DBG_BAUDRATE             115200   // USB CDC 虚拟串口波特率（CDC下仅名义值）
#endif

/* ============================================================
 * 二、传感器引脚配置
 *
 *    命名约定: *_RX 表示 ESP32 的 TX（发往传感器）
 *              *_TX 表示 ESP32 的 RX（接收传感器输出）
 * ============================================================ */

/* 前向 SDM10 激光测距 —— UART1
 * 量程10m(SDM10标准版, V2.0数据手册)，精度±5cm(<5m)/1%(≥5m)，5V供电，UART输出
 * 协议: 帧头0x5C + 距离2B LE(mm) + 校验和(~(DIST_L+DIST_H)) = 4字节, 波特率460800
 * 输出模式: 上电自动连续输出 50Hz */
#define PIN_SDM10_RX               17        // ESP32 TX -> SDM10 RX
#define PIN_SDM10_TX               18        // SDM10 TX  -> ESP32 RX
#define SDM10_UART                 Serial1   // Arduino: Serial1 对应 UART1
#define SDM10_BAUDRATE             460800    // SDM10 默认波特率(出厂460800)
#define SDM10_MAX_RANGE_CM         1000      // 最大量程 10m = 1000cm(标准版,90%反射率)
#define SDM10_INVALID_DISTANCE     (-1.0f)   // 无效距离标识

/* 前向 HC-SR04 超声波测距 —— GPIO 直连 (v2.0: 替代 Rd-03D)
 * 量程 2cm-400cm，精度 ±3mm，Trig 发 10μs 脉冲，Echo 测回波脉宽
 * GPIO4/5 为原后雷达引脚释放，板上完全空闲 */
#define PIN_HCSR04_TRIG             4         // ESP32 → HC-SR04 Trig
#define PIN_HCSR04_ECHO             5         // HC-SR04 Echo → ESP32
#define HCSR04_MAX_RANGE_CM         400       // HC-SR04 最大量程 4m
#define HCSR04_TIMEOUT_US           25000     // pulseIn 超时 ≈4.3m@343m/s

/* ============================================================
 * 三、蜂鸣器与RGB LED 引脚
 * ============================================================ */

/* 蜂鸣器 —— GPIO PWM 控制，仅极危险情况和自检时响 */
#define PIN_BUZZER                 47        // 蜂鸣器驱动引脚
#define BUZZER_LEDC_CHANNEL        0         // LEDC 通道0
#define BUZZER_LEDC_TIMER_BITS     10        // 10位分辨率
#define BUZZER_LEDC_FREQ_HZ        2000      // 默认频率2kHz

/* WS2812 RGB LED —— 板载，GPIO48 */
#define PIN_RGB_LED                48        // WS2812 数据引脚
#define RGB_LED_COUNT              1         // LED 数量

/* ============================================================
 * 四、WiFi AP 配置
 *    ESP32 作为 AP 热点，手机 + 摄像头板连接 ESP32 进行通信
 * ============================================================ */
#define WIFI_AP_SSID               "BlindGuide_AP"         // 热点名称(与Android APP一致)
#define WIFI_AP_PASSWORD           "12345678"              // 密码(至少8位)
#define WIFI_AP_CHANNEL            1                       // 信道
#define WIFI_AP_MAX_CONNECTIONS    4                       // 最大连接数(手机+摄像头板)
#define WIFI_AP_HIDDEN             false                   // 是否隐藏SSID

/* 通信端口 */
#define TCP_PORT                   8888     // TCP端口: 传感器JSON数据传输(与Android APP一致)
#define WEB_SERVER_PORT            80       // Web配置页面端口

/* 客户端缓冲 */
#define TCP_MAX_CLIENTS            4        // TCP最大客户端数
#define TCP_SEND_BUFFER_SIZE       2048     // TCP单次发送缓冲

/* 摄像板 ESP32 WiFi STA 配置 (v3.0: 摄像板独立连接主控AP)
 * 主控通过 JSON 将摄像板 IP 告知手机 App，手机直连摄像板拉流 */
#define CAMERA_ESP32_STATIC_IP     "192.168.4.10"   // 摄像板静态IP（AP子网内）
#define CAMERA_HTTP_PORT           80                // 摄像板HTTP服务端口

/* ============================================================
 * 五、预警距离阈值（单位: 米）
 *    用于主控决策任务判断危险等级
 * ============================================================ */
#define WARN_DISTANCE_ATTENTION    3.0f     // >此值: 安全
#define WARN_DISTANCE_WARNING      2.5f     // <此值: 警告(与Android APP DISTANCE_CAUTION一致)
#define WARN_DISTANCE_DANGER       1.0f     // <此值: 危险（触发蜂鸣器）
#define WARN_DEBOUNCE_FRAMES       3        // 连续N帧确认才触发预警

/* ============================================================
 * 六、FreeRTOS 任务配置
 *    v2.0: 4个任务 (删除 taskSpi)
 *    数值越大优先级越高
 * ============================================================ */

/* 任务优先级 */
#define TASK_PRIORITY_SENSOR       5        // 传感器采集任务(激光+超声波)
#define TASK_PRIORITY_WIFI         4        // WiFi通信任务(TCP传感器上报)
#define TASK_PRIORITY_DECISION     6        // 主控决策任务(危险等级+报警)
#define TASK_PRIORITY_WEB          3        // Web服务任务

/* 任务栈大小（单位: 字节） */
#define TASK_STACK_SENSOR          4096
#define TASK_STACK_WIFI            8192     // WiFi+TCP需要较大栈
#define TASK_STACK_DECISION        4096
#define TASK_STACK_WEB             8192     // Web服务器需要较大栈

/* 任务周期（ms） */
#define TASK_PERIOD_SENSOR_MS      50       // 传感器采集周期 20Hz
#define TASK_PERIOD_DECISION_MS    100      // 决策周期 10Hz
#define TASK_PERIOD_WIFI_TX_MS     100      // WiFi数据上报周期 (v2.0: 仅传感器JSON)

/* 任务核心绑定（ESP32-S3 双核: 0=PRO, 1=APP）
 *   tskNO_AFFINITY 表示不绑定，由调度器决定 */
#define TASK_CORE_SENSOR           1        // 传感器 -> 核1
#define TASK_CORE_WIFI             0        // WiFi -> 核0
#define TASK_CORE_DECISION         1        // 决策 -> 核1
#define TASK_CORE_WEB              0        // Web -> 核0

/* ============================================================
 * 七、系统状态枚举（供RGB指示灯使用）
 * ============================================================ */
#define SYS_STATE_POWER_ON         0        // 上电(蓝)
#define SYS_STATE_INIT             1        // 初始化中(蓝闪)
#define SYS_STATE_NORMAL           2        // 正常工作(绿)
#define SYS_STATE_WARNING          3        // 警告(黄)
#define SYS_STATE_DANGER           4        // 危险(红闪+蜂鸣)
#define SYS_STATE_FAULT            5        // 故障(红)
#define SYS_STATE_WIFI_CONNECTED   6        // WiFi已连接(绿)

#endif /* CONFIG_H */
