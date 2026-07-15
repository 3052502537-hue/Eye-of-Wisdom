/* ============================================================
 * 文件名: task_manager.h
 * 功能描述: FreeRTOS 任务管理头文件 v3.0
 *           v3.0: 彻底删除雷达驱动(rd03d)和SPI驱动(spi_master_comm)
 *                 只保留 HC-SR04 超声波 + SDM10 激光
 *                 管理4个核心任务的创建与调度:
 *                   1. 传感器采集任务  - 读取激光+超声波数据
 *                   2. WiFi通信任务    - 上报传感器JSON(TCP)
 *                   3. 主控决策任务    - 危险等级判断+报警控制
 *                   4. Web服务任务     - 处理HTTP配置请求
 * 依赖关系: Arduino FreeRTOS、config.h、protocol.h、各模块驱动
 * 接口说明:
 *   TaskManager()                - 构造函数
 *   begin()                      - 初始化任务管理器
 *   startTasks()                 - 创建并启动所有任务
 *   stopAll()                    - 挂起所有任务
 *   getSensorFrame()             - 获取最新传感器数据帧(线程安全)
 *   getWorkMode()                - 获取当前工作模式
 *   setWorkMode()                - 设置工作模式
 *
 * 任务间通信:
 *   - 全局 SensorFrame_t 通过互斥锁保护
 *   - 危险等级写入 SensorFrame_t.level 由决策任务更新
 * ============================================================ */

#ifndef TASK_MANAGER_H
#define TASK_MANAGER_H

#include <Arduino.h>
#include "config.h"
#include "protocol.h"
#include "sdm10_driver.h"
#include "hc_sr04_driver.h"
#include "wifi_manager.h"
#include "web_server.h"
#include "alarm_manager.h"

class TaskManager {
public:
    TaskManager();
    ~TaskManager();

    /* --------------------------------------------------------
     * begin - 初始化任务管理器(创建互斥锁)
     * 参数: 无
     * 返回: true=成功
     * -------------------------------------------------------- */
    bool begin();

    /* --------------------------------------------------------
     * startTasks - 启动4个FreeRTOS任务
     * 返回: true=全部创建成功
     * -------------------------------------------------------- */
    bool startTasks();

    /* --------------------------------------------------------
     * stopAll - 挂起所有任务
     * -------------------------------------------------------- */
    void stopAll();

    /* --------------------------------------------------------
     * getSensorFrame - 获取最新传感器数据(线程安全)
     * 参数: outFrame - 输出帧
     * 返回: true=成功
     * -------------------------------------------------------- */
    bool getSensorFrame(SensorFrame_t* outFrame);

    /* 工作模式控制 */
    WorkMode_t getWorkMode() const { return _workMode; }
    void setWorkMode(WorkMode_t mode) { _workMode = mode; }

    /* 模块访问器(供主程序自检使用) */
    SDM10Driver&       laser()       { return _sdm10; }
    HCSR04Driver&      ultrasonic()  { return _ultrasonic; }
    WifiManager&       wifi()        { return _wifi; }
    WebServerManager&  web()         { return _web; }
    AlarmManager&      alarm()       { return _alarm; }

private:
    /* ---- 4个任务函数(静态,供xTaskCreate使用) ---- */
    static void taskSensor(void* arg);     // 传感器采集任务(激光+超声波)
    static void taskWifi(void* arg);       // WiFi通信任务(TCP传感器上报)
    static void taskDecision(void* arg);   // 主控决策任务(危险等级+报警)
    static void taskWeb(void* arg);        // Web服务任务

    /* 更新传感器数据帧(加锁) */
    void updateSensorFrame(const SensorFrame_t* frame);

    /* 危险等级判断(融合激光+超声波) */
    DangerLevel_t evaluateDanger(const SensorFrame_t* frame);

    /* 任务句柄 */
    TaskHandle_t _hSensor;
    TaskHandle_t _hWifi;
    TaskHandle_t _hDecision;
    TaskHandle_t _hWeb;

    /* 互斥锁保护传感器数据 */
    SemaphoreHandle_t _mutex;

    /* 模块实例 */
    SDM10Driver       _sdm10;
    HCSR04Driver      _ultrasonic;
    WifiManager       _wifi;
    WebServerManager  _web;
    AlarmManager      _alarm;

    /* 共享数据 */
    SensorFrame_t _sensorFrame;     // 最新传感器数据帧
    WorkMode_t    _workMode;        // 当前工作模式
    bool          _tasksRunning;    // 任务运行标志
};

#endif /* TASK_MANAGER_H */
