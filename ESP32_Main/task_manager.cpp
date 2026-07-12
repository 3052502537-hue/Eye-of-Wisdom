/* ============================================================
 * 文件名: task_manager.cpp
 * 功能描述: FreeRTOS 任务管理实现
 *           实现5个任务的创建、任务函数逻辑、任务间数据共享
 * 依赖关系: Arduino FreeRTOS、各模块驱动、task_manager.h
 * 接口说明: 见头文件
 *
 * 任务职责:
 *   taskSensor   - 周期读取激光+前后雷达,写入共享SensorFrame
 *   taskSpi      - 响应数据就绪中断读取摄像头图像帧
 *   taskWifi     - 周期上报传感器JSON + 发送图像UDP + 处理TCP命令
 *   taskDecision - 周期评估危险等级,控制蜂鸣器/RGB报警
 *   taskWeb      - 处理Web HTTP请求
 * ============================================================ */

#include "task_manager.h"

#ifdef DEBUG
  #define DBG_PRINT(x)     Serial.print(x)
  #define DBG_PRINTLN(x)   Serial.println(x)
  #define DBG_PRINTF(fmt, ...) Serial.printf(fmt, ##__VA_ARGS__)
#else
  #define DBG_PRINT(x)
  #define DBG_PRINTLN(x)
  #define DBG_PRINTF(fmt, ...)
#endif

/* 全局任务管理器指针(供静态任务函数访问实例) */
static TaskManager* g_tm = nullptr;

/* 构造函数 */
TaskManager::TaskManager()
    : _hSensor(nullptr), _hSpi(nullptr), _hWifi(nullptr),
      _hDecision(nullptr), _hWeb(nullptr), _mutex(nullptr),
      _workMode(MODE_NORMAL), _tasksRunning(false)
{
    memset(&_sensorFrame, 0, sizeof(_sensorFrame));
}

/* 析构函数 */
TaskManager::~TaskManager()
{
    stopAll();
    if (_mutex) {
        vSemaphoreDelete(_mutex);
        _mutex = nullptr;
    }
}

/* begin - 初始化任务管理器 */
bool TaskManager::begin()
{
    if (_mutex) return true;
    _mutex = xSemaphoreCreateMutex();
    if (!_mutex) {
        DBG_PRINTLN("[TM] mutex create fail");
        return false;
    }
    g_tm = this;
    return true;
}

/* initAll - 初始化所有模块驱动 */
bool TaskManager::initAll()
{
    bool ok = true;

    /* 初始化激光测距(UART1) */
    if (!_sdm10.begin()) {
        DBG_PRINTLN("[TM] SDM10 init fail");
        ok = false;
    }

    /* 初始化前向雷达(UART2) */
    if (!_radarFront.begin(RADAR_FRONT_UART, PIN_RADAR_FRONT_TX,
                           PIN_RADAR_FRONT_RX, RADAR_BAUDRATE)) {
        DBG_PRINTLN("[TM] radar front init fail");
        ok = false;
    }

    /* 初始化后向雷达(UART0) */
    if (!_radarRear.begin(RADAR_REAR_UART, PIN_RADAR_REAR_TX,
                          PIN_RADAR_REAR_RX, RADAR_BAUDRATE)) {
        DBG_PRINTLN("[TM] radar rear init fail");
        ok = false;
    }

    /* 初始化SPI主机 */
    if (!_spi.begin()) {
        DBG_PRINTLN("[TM] SPI init fail");
        ok = false;
    }

    /* 初始化WiFi */
    if (!_wifi.begin()) {
        DBG_PRINTLN("[TM] WiFi init fail");
        ok = false;
    }

    /* 初始化Web服务器 */
    _web.begin();

    DBG_PRINTF("[TM] initAll %s\n", ok ? "OK" : "PARTIAL");
    return ok;
}

/* startTasks - 创建并启动5个任务 */
bool TaskManager::startTasks()
{
    if (_tasksRunning) return true;

    BaseType_t r1 = xTaskCreatePinnedToCore(taskSensor,   "Sensor",
                       TASK_STACK_SENSOR,   this, TASK_PRIORITY_SENSOR,
                       &_hSensor,   TASK_CORE_SENSOR);
    BaseType_t r2 = xTaskCreatePinnedToCore(taskSpi,      "SpiComm",
                       TASK_STACK_SPI,      this, TASK_PRIORITY_SPI,
                       &_hSpi,      TASK_CORE_SPI);
    BaseType_t r3 = xTaskCreatePinnedToCore(taskWifi,     "WiFi",
                       TASK_STACK_WIFI,     this, TASK_PRIORITY_WIFI,
                       &_hWifi,     TASK_CORE_WIFI);
    BaseType_t r4 = xTaskCreatePinnedToCore(taskDecision, "Decision",
                       TASK_STACK_DECISION, this, TASK_PRIORITY_DECISION,
                       &_hDecision, TASK_CORE_DECISION);
    BaseType_t r5 = xTaskCreatePinnedToCore(taskWeb,      "Web",
                       TASK_STACK_WEB,      this, TASK_PRIORITY_WEB,
                       &_hWeb,      TASK_CORE_WEB);

    _tasksRunning = (r1 == pdPASS && r2 == pdPASS && r3 == pdPASS &&
                     r4 == pdPASS && r5 == pdPASS);

    DBG_PRINTF("[TM] tasks %s\n", _tasksRunning ? "started" : "CREATE FAIL");
    return _tasksRunning;
}

/* stopAll - 挂起所有任务 */
void TaskManager::stopAll()
{
    if (!_tasksRunning) return;
    if (_hSensor)   vTaskSuspend(_hSensor);
    if (_hSpi)      vTaskSuspend(_hSpi);
    if (_hWifi)     vTaskSuspend(_hWifi);
    if (_hDecision) vTaskSuspend(_hDecision);
    if (_hWeb)      vTaskSuspend(_hWeb);
    _tasksRunning = false;
}

/* updateSensorFrame - 更新共享传感器数据(加锁) */
void TaskManager::updateSensorFrame(const SensorFrame_t* frame)
{
    if (!_mutex || !frame) return;
    if (xSemaphoreTake(_mutex, 5)) {
        memcpy(&_sensorFrame, frame, sizeof(_sensorFrame));
        xSemaphoreGive(_mutex);
    }
}

/* getSensorFrame - 获取传感器数据(加锁) */
bool TaskManager::getSensorFrame(SensorFrame_t* outFrame)
{
    if (!_mutex || !outFrame) return false;
    if (xSemaphoreTake(_mutex, 5)) {
        memcpy(outFrame, &_sensorFrame, sizeof(_sensorFrame));
        xSemaphoreGive(_mutex);
        return true;
    }
    return false;
}

/* evaluateDanger - 危险等级判断 */
DangerLevel_t TaskManager::evaluateDanger(const SensorFrame_t* frame)
{
    if (!frame) return LEVEL_SAFE;

    /* 取前方最近距离(激光优先,其次雷达) */
    float minDist = 999.0f;
    if (frame->laser.valid && frame->laser.distance > 0) {
        minDist = frame->laser.distance;
    }
    for (uint8_t i = 0; i < frame->radarFront.count; i++) {
        if (frame->radarFront.targets[i].distance > 0 &&
            frame->radarFront.targets[i].distance < minDist) {
            minDist = frame->radarFront.targets[i].distance;
        }
    }

    /* 按阈值判断等级 */
    if (minDist < WARN_DISTANCE_DANGER)    return LEVEL_DANGER;
    if (minDist < WARN_DISTANCE_WARNING)   return LEVEL_WARNING;
    if (minDist < WARN_DISTANCE_ATTENTION) return LEVEL_ATTENTION;
    return LEVEL_SAFE;
}

/* ============================================================
 * taskSensor - 传感器采集任务
 *   周期: TASK_PERIOD_SENSOR_MS (50ms / 20Hz)
 *   读取激光+前后雷达,组装SensorFrame并更新共享区
 * ============================================================ */
void TaskManager::taskSensor(void* arg)
{
    TaskManager* self = (TaskManager*)arg;
    SensorFrame_t frame;
    memset(&frame, 0, sizeof(frame));

    for (;;) {
        uint32_t startMs = millis();

        memset(&frame, 0, sizeof(frame));
        frame.timestamp = millis();

        /* 读取激光测距 */
        float dist;
        if (self->_sdm10.readDistance(dist)) {
            frame.laser.distance = dist;
            frame.laser.valid = 1;
        } else {
            frame.laser.valid = 0;
        }

        /* 读取前向雷达 */
        uint8_t cnt = 0;
        if (self->_radarFront.readTargets(frame.radarFront.targets, &cnt)) {
            frame.radarFront.count = cnt;
            frame.radarFront.valid = 1;
        }

        /* 读取后向雷达 */
        if (self->_radarRear.readTargets(frame.radarRear.targets, &cnt)) {
            frame.radarRear.count = cnt;
            frame.radarRear.valid = 1;
        }

        /* 更新共享数据 */
        self->updateSensorFrame(&frame);

        /* 周期等待 */
        int32_t elapsed = (int32_t)(millis() - startMs);
        int32_t wait = TASK_PERIOD_SENSOR_MS - elapsed;
        if (wait < 0) wait = 0;
        vTaskDelay(pdMS_TO_TICKS(wait));
    }
}

/* ============================================================
 * taskSpi - SPI通信任务
 *   等待数据就绪中断标志,读取并重组图像帧
 * ============================================================ */
void TaskManager::taskSpi(void* arg)
{
    TaskManager* self = (TaskManager*)arg;

    for (;;) {
        /* 尝试读取SPI帧(内部检查中断标志) */
        self->_spi.readFrameIfReady();

        /* SPI任务较频繁,短延时让出CPU */
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}

/* ============================================================
 * taskWifi - WiFi通信任务
 *   周期上报传感器JSON + 发送图像UDP + 处理TCP客户端
 * ============================================================ */
void TaskManager::taskWifi(void* arg)
{
    TaskManager* self = (TaskManager*)arg;
    char jsonBuf[1024];

    for (;;) {
        uint32_t startMs = millis();

        /* 1. 处理TCP客户端连接与命令 */
        self->_wifi.processTcpClients();

        /* 2. 有客户端时上报传感器JSON */
        if (self->_wifi.isClientConnected()) {
            SensorFrame_t f;
            if (self->getSensorFrame(&f)) {
                /* 组装JSON字符串 */
                int n = snprintf(jsonBuf, sizeof(jsonBuf),
                    "{\"type\":\"sensor\",\"ts\":%lu,"
                    "\"laser\":{\"dist\":%.2f,\"valid\":%u},"
                    "\"radar_f\":{\"count\":%u,\"valid\":%u},"
                    "\"radar_r\":{\"count\":%u,\"valid\":%u},"
                    "\"level\":%u,\"img\":%d}",
                    (unsigned long)f.timestamp,
                    f.laser.distance, f.laser.valid,
                    f.radarFront.count, f.radarFront.valid,
                    f.radarRear.count, f.radarRear.valid,
                    f.level,
                    self->_spi.isFrameReady() ? 1 : 0);

                if (n > 0) {
                    self->_wifi.sendSensorJson(jsonBuf, n);
                }
            }

            /* 3. 发送图像UDP(若有新帧) */
            ImageFrame_t img;
            if (self->_spi.getLatestFrame(&img) && img.size > 0) {
                self->_wifi.sendImageUdp(img.data, img.size, img.frameId);
            }
        }

        /* 周期等待 */
        int32_t elapsed = (int32_t)(millis() - startMs);
        int32_t wait = TASK_PERIOD_WIFI_TX_MS - elapsed;
        if (wait < 0) wait = 0;
        vTaskDelay(pdMS_TO_TICKS(wait));
    }
}

/* ============================================================
 * taskDecision - 主控决策任务
 *   周期评估危险等级,控制蜂鸣器/RGB报警
 * ============================================================ */
void TaskManager::taskDecision(void* arg)
{
    TaskManager* self = (TaskManager*)arg;
    uint8_t dangerCount = 0;   // 危险确认计数(防抖)

    for (;;) {
        SensorFrame_t f;
        if (self->getSensorFrame(&f)) {
            DangerLevel_t level = self->evaluateDanger(&f);

            /* 危险等级防抖: 连续N帧确认 */
            if (level >= LEVEL_DANGER) {
                if (dangerCount < 255) dangerCount++;
            } else {
                dangerCount = 0;
            }

            /* 更新共享帧中的等级 */
            if (xSemaphoreTake(self->_mutex, 5)) {
                self->_sensorFrame.level = (dangerCount >= WARN_DEBOUNCE_FRAMES)
                                           ? LEVEL_DANGER : level;
                xSemaphoreGive(self->_mutex);
            }

            /* 控制报警 */
            if (self->_sensorFrame.level >= LEVEL_DANGER) {
                self->_alarm.setDangerAlarm(true);
                self->_alarm.setSystemState(SYS_STATE_DANGER);
            } else if (self->_sensorFrame.level == LEVEL_WARNING) {
                self->_alarm.setDangerAlarm(false);
                self->_alarm.setSystemState(SYS_STATE_WARNING);
            } else {
                self->_alarm.setDangerAlarm(false);
                if (self->_wifi.isClientConnected()) {
                    self->_alarm.setSystemState(SYS_STATE_WIFI_CONNECTED);
                } else {
                    self->_alarm.setSystemState(SYS_STATE_NORMAL);
                }
            }
        }

        /* 刷新RGB闪烁效果 */
        self->_alarm.update();

        vTaskDelay(pdMS_TO_TICKS(TASK_PERIOD_DECISION_MS));
    }
}

/* ============================================================
 * taskWeb - Web服务任务
 *   处理HTTP请求,并同步Web参数到传感器数据展示
 * ============================================================ */
void TaskManager::taskWeb(void* arg)
{
    TaskManager* self = (TaskManager*)arg;

    for (;;) {
        /* 处理HTTP请求 */
        self->_web.handleClient();

        /* 同步最新传感器数据到Web展示 */
        SensorFrame_t f;
        if (self->getSensorFrame(&f)) {
            self->_web.updateSensorData(&f);
        }

        /* 处理Web触发的校准/重启请求 */
        if (self->_web.isCalibrateRequested()) {
            self->_web.clearCalibrateRequest();
            DBG_PRINTLN("[TM] web calibrate triggered");
            /* TODO: 执行实际校准流程 */
        }
        if (self->_web.isRebootRequested()) {
            self->_web.clearRebootRequest();
            DBG_PRINTLN("[TM] web reboot -> ESP.restart()");
            delay(100);
            ESP.restart();
        }

        vTaskDelay(pdMS_TO_TICKS(20));
    }
}
