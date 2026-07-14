/* ============================================================
 * 文件名: task_manager.h
 * 功能描述: FreeRTOS 任务管理头文件
 *           管理5个核心任务的创建与调度:
 *             1. 传感器采集任务  - 读取激光/雷达数据
 *             2. SPI通信任务     - 接收摄像头图像
 *             3. WiFi通信任务    - 上报传感器JSON+发送图像UDP
 *             4. 主控决策任务    - 危险等级判断+报警控制
 *             5. Web服务任务     - 处理HTTP配置请求
 * 依赖关系: Arduino FreeRTOS、config.h、protocol.h、各模块驱动
 * 接口说明:
 *   TaskManager()                - 构造函数
 *   begin()                      - 创建并启动所有任务
 *   stopAll()                    - 挂起所有任务
 *   getSensorFrame()             - 获取最新传感器数据帧(线程安全)
 *   getWorkMode()                - 获取当前工作模式
 *   setWorkMode()                - 设置工作模式
 *
 * 任务间通信:
 *   - 全局 SensorFrame_t 通过互斥锁保护
 *   - 图像帧通过 SpiMasterComm 的 getLatestFrame 获取
 *   - 危险等级写入 SensorFrame_t.level 由决策任务更新
 * ============================================================ */

#ifndef TASK_MANAGER_H
#define TASK_MANAGER_H

#include <Arduino.h>
#include "config.h"
#include "protocol.h"
#include "sdm10_driver.h"
#include "rd03d_driver.h"
#include "spi_master_comm.h"
#include "wifi_manager.h"
#include "web_server.h"
#include "alarm_manager.h"

class TaskManager {
public:
    TaskManager();
    ~TaskManager();

    /* --------------------------------------------------------
     * begin - 初始化任务管理器(不创建任务，任务由 initAll 创建)
     * 参数: 无
     * 返回: true=成功
     * -------------------------------------------------------- */
    bool begin();

    /* --------------------------------------------------------
     * startTasks - 启动5个FreeRTOS任务
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
    RD03DDriver&       radarFront()  { return _radarFront; }
    RD03DDriver&       radarRear()   { return _radarRear; }
    SpiMasterComm&     spi()         { return _spi; }
    WifiManager&       wifi()        { return _wifi; }
    WebServerManager&  web()         { return _web; }
    AlarmManager&      alarm()       { return _alarm; }

private:
    /* ---- 5个任务函数(静态,供xTaskCreate使用) ---- */
    static void taskSensor(void* arg);     // 传感器采集任务
    static void taskSpi(void* arg);        // SPI通信任务
    static void taskWifi(void* arg);       // WiFi通信任务
    static void taskDecision(void* arg);   // 主控决策任务
    static void taskWeb(void* arg);        // Web服务任务

    /* 更新传感器数据帧(加锁) */
    void updateSensorFrame(const SensorFrame_t* frame);

    /* 危险等级判断 */
    DangerLevel_t evaluateDanger(const SensorFrame_t* frame);

    /* 任务句柄 */
    TaskHandle_t _hSensor;
    TaskHandle_t _hSpi;
    TaskHandle_t _hWifi;
    TaskHandle_t _hDecision;
    TaskHandle_t _hWeb;

    /* 互斥锁保护传感器数据 */
    SemaphoreHandle_t _mutex;

    /* 模块实例 */
    SDM10Driver       _sdm10;
    RD03DDriver       _radarFront;
    RD03DDriver       _radarRear;
    SpiMasterComm     _spi;
    WifiManager       _wifi;
    WebServerManager  _web;
    AlarmManager      _alarm;

    /* 共享数据 */
    SensorFrame_t _sensorFrame;     // 最新传感器数据帧
    WorkMode_t    _workMode;        // 当前工作模式
    bool          _tasksRunning;    // 任务运行标志
};

#endif /* TASK_MANAGER_H */
