/* ============================================================
 * 文件名: config.h
 * 功能描述: 导盲头环主控板（ESP32-S3-N16R8）全局配置中心
 *           集中管理所有引脚定义、硬件参数、WiFi配置、任务调度参数、
 *           预警阈值等。修改硬件接线后只需修改本文件即可。
 * 依赖关系: 无外部依赖，被所有其他模块引用
 * 接口说明: 仅提供宏定义，无函数接口
 *
 * 硬件平台: ESP32-S3-N16R8（16MB Flash / 8MB PSRAM，八线PSRAM）
 * 开发环境: Arduino IDE + ESP32 Arduino Core（需开启 USB CDC On Boot）
 *
 * 引脚分配总览（已避开N16R8八线Flash/PSRAM占用引脚GPIO27~37）:
 *   UART1  -> SDM10 前向激光测距          (TX=GPIO17, RX=GPIO18)
 *   UART2  -> RD-03D 前向毫米波雷达        (TX=GPIO19, RX=GPIO20)
 *   UART0  -> RD-03D 后向毫米波雷达        (TX=GPIO43, RX=GPIO44)
 *   SPI    -> 摄像头板从机通信             (MOSI=11, MISO=13, SCK=12, CS=10)
 *   数据就绪中断 <- 摄像头板               (GPIO9, 下降沿触发)
 *   蜂鸣器 PWM                            (GPIO47)
 *   WS2812 RGB LED                        (GPIO48)
 *   调试串口 -> USB CDC（不占用任何UART引脚）
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
 * 二、传感器 UART 引脚配置
 *    ESP32-S3 共3个硬件UART，且支持 USB CDC 调试口
 *    因此3个UART全部可用于传感器，调试走USB CDC不占UART
 *
 *    命名约定: *_RX 表示 ESP32 的 TX（发往传感器）
 *              *_TX 表示 ESP32 的 RX（接收传感器输出）
 * ============================================================ */

/* 前向 SDM10 激光测距 —— UART1
 * 量程22m，精度±1cm，5V供电，UART输出 */
#define PIN_SDM10_RX               17        // ESP32 TX -> SDM10 RX
#define PIN_SDM10_TX               18        // SDM10 TX  -> ESP32 RX
#define SDM10_UART                 Serial1   // Arduino: Serial1 对应 UART1
#define SDM10_BAUDRATE             115200    // SDM10 默认波特率
#define SDM10_MAX_RANGE_CM         2200      // 最大量程 22m = 2200cm
#define SDM10_INVALID_DISTANCE     (-1.0f)   // 无效距离标识

/* 前向 RD-03D 毫米波雷达 —— UART2
 * 支持测距/测角/测速，多目标 */
#define PIN_RADAR_FRONT_RX         19        // ESP32 TX -> 雷达 RX
#define PIN_RADAR_FRONT_TX         20        // 雷达 TX  -> ESP32 RX
#define RADAR_FRONT_UART           Serial2   // Arduino: Serial2 对应 UART2
#define RADAR_BAUDRATE             115200    // RD-03D 默认波特率
#define RADAR_MAX_TARGETS          8         // 单帧最多目标数

/* 后向 RD-03D 毫米波雷达 —— UART0
 * 因为调试口走 USB CDC，UART0(GPIO43/44) 被释放用于后雷达
 * 注意: 必须在 Arduino IDE 开启 "USB CDC On Boot"，否则
 *       Serial0 会与默认调试串口冲突 */
#define PIN_RADAR_REAR_RX          43        // ESP32 TX -> 雷达 RX
#define PIN_RADAR_REAR_TX          44        // 雷达 TX  -> ESP32 RX
#define RADAR_REAR_UART            Serial0   // Arduino: Serial0 对应 UART0

/* 传感器自检超时（ms）—— 上电自检时等待传感器应答的最大时间 */
#define SENSOR_SELFTEST_TIMEOUT_MS 500

/* ============================================================
 * 三、SPI 主机通信引脚（连接摄像头板从机）
 *    主控板为 SPI Master，摄像头板为 SPI Slave
 *    摄像头板有数据就绪时拉低中断引脚，主控再发起读取
 *
 *    接线对应:
 *      摄像头板 SPI_MOSI -> 主控 PIN_SPI_MOSI
 *      摄像头板 SPI_MISO -> 主控 PIN_SPI_MISO
 *      摄像头板 SPI_SCK  -> 主控 PIN_SPI_SCK
 *      摄像头板 SPI_CS   -> 主控 PIN_SPI_CS
 *      摄像头板 DATA_RDY -> 主控 PIN_SPI_DATA_READY
 *      GND               -> GND（必须共地）
 * ============================================================ */
#define PIN_SPI_MOSI               11        // SPI 主机输出 从机输入
#define PIN_SPI_MISO               13        // SPI 主机输入 从机输出
#define PIN_SPI_SCK                12        // SPI 时钟
#define PIN_SPI_CS                 10        // 片选（主控输出，低有效）
#define PIN_SPI_DATA_READY         9         // 摄像头板数据就绪中断输入(下降沿)
#define SPI_CLOCK_SPEED            8000000UL // SPI 时钟 8MHz（可按连线质量调整）
#define SPI_DMA_BUFFER_SIZE        4096      // 单次DMA读取最大字节数

/* ============================================================
 * 四、蜂鸣器与RGB LED 引脚
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
 * 五、WiFi AP 配置
 *    ESP32 作为 AP 热点，手机连接 ESP32 进行通信
 * ============================================================ */
#define WIFI_AP_SSID               "BlindGuide_AP"         // 热点名称(与Android APP一致)
#define WIFI_AP_PASSWORD           "12345678"              // 密码(至少8位)
#define WIFI_AP_CHANNEL            1                       // 信道
#define WIFI_AP_MAX_CONNECTIONS    4                       // 最大连接数
#define WIFI_AP_HIDDEN             false                   // 是否隐藏SSID

/* 通信端口 */
#define TCP_PORT                   8888     // TCP端口: 传感器JSON数据传输(与Android APP一致)
#define UDP_PORT                   8889     // UDP端口: 图像JPEG二进制传输(与Android APP一致)
#define WEB_SERVER_PORT            80       // Web配置页面端口

/* 客户端缓冲 */
#define TCP_MAX_CLIENTS            4        // TCP最大客户端数
#define TCP_SEND_BUFFER_SIZE       2048     // TCP单次发送缓冲
#define UDP_PACKET_MAX_SIZE        1460     // UDP单包最大字节数(留余量)

/* ============================================================
 * 六、预警距离阈值（单位: 米）
 *    用于主控决策任务判断危险等级
 * ============================================================ */
#define WARN_DISTANCE_ATTENTION    3.0f     // >此值: 安全
#define WARN_DISTANCE_WARNING      2.5f     // <此值: 警告(与Android APP DISTANCE_CAUTION一致)
#define WARN_DISTANCE_DANGER       1.0f     // <此值: 危险（触发蜂鸣器）
#define WARN_DEBOUNCE_FRAMES       3        // 连续N帧确认才触发预警

/* ============================================================
 * 七、图像参数
 * ============================================================ */
#define IMG_FRAME_WIDTH            320      // QVGA 宽
#define IMG_FRAME_HEIGHT           240      // QVGA 高
#define IMG_JPEG_QUALITY           10       // JPEG质量(1-31, 越小质量越高)
#define IMG_MAX_JPEG_SIZE          (60 * 1024)  // 单帧JPEG最大字节数

/* ============================================================
 * 八、FreeRTOS 任务配置
 *    数值越大优先级越高
 * ============================================================ */

/* 任务优先级 */
#define TASK_PRIORITY_SENSOR       5        // 传感器采集任务
#define TASK_PRIORITY_SPI          6        // SPI通信任务(图像接收)
#define TASK_PRIORITY_WIFI         4        // WiFi通信任务
#define TASK_PRIORITY_DECISION     6        // 主控决策任务
#define TASK_PRIORITY_WEB          3        // Web服务任务

/* 任务栈大小（单位: 字节） */
#define TASK_STACK_SENSOR          4096
#define TASK_STACK_SPI             8192     // SPI需要较大缓冲
#define TASK_STACK_WIFI            8192     // WiFi+TCP/UDP需要较大栈
#define TASK_STACK_DECISION        4096
#define TASK_STACK_WEB             8192     // Web服务器需要较大栈

/* 任务周期（ms） */
#define TASK_PERIOD_SENSOR_MS      50       // 传感器采集周期 20Hz
#define TASK_PERIOD_DECISION_MS    100      // 决策周期 10Hz
#define TASK_PERIOD_WIFI_TX_MS     100      // WiFi数据上报周期

/* 任务核心绑定（ESP32-S3 双核: 0=PRO, 1=APP）
 *   tskNO_AFFINITY 表示不绑定，由调度器决定 */
#define TASK_CORE_SENSOR           1        // 传感器 -> 核1
#define TASK_CORE_SPI              0        // SPI -> 核0
#define TASK_CORE_WIFI             0        // WiFi -> 核0
#define TASK_CORE_DECISION         1        // 决策 -> 核1
#define TASK_CORE_WEB              0        // Web -> 核0

/* ============================================================
 * 九、系统状态枚举（供RGB指示灯使用）
 * ============================================================ */
#define SYS_STATE_POWER_ON         0        // 上电(蓝)
#define SYS_STATE_INIT             1        // 初始化中(蓝闪)
#define SYS_STATE_NORMAL           2        // 正常工作(绿)
#define SYS_STATE_WARNING          3        // 警告(黄)
#define SYS_STATE_DANGER           4        // 危险(红闪+蜂鸣)
#define SYS_STATE_FAULT            5        // 故障(红)
#define SYS_STATE_WIFI_CONNECTED   6        // WiFi已连接(绿)

#endif /* CONFIG_H */
