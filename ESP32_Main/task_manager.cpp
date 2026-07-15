/* ============================================================
 * 文件名: task_manager.cpp
 * 功能描述: FreeRTOS 任务管理实现 v3.0
 *           v3.0: 彻底删除 taskSpi (SPI通信)、删除所有雷达相关代码
 *                 taskSensor 只读取激光+超声波
 *                 taskWifi 仅发送传感器JSON (含camera_ip)
 *                 任务精简为4个
 * 依赖关系: Arduino FreeRTOS、各模块驱动、task_manager.h
 * 接口说明: 见头文件
 *
 * 任务职责:
 *   taskSensor   - 周期读取激光+超声波,写入共享SensorFrame
 *   taskWifi     - 周期上报传感器JSON(含摄像板IP) + 处理TCP命令
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
    : _hSensor(nullptr), _hWifi(nullptr),
      _hDecision(nullptr), _hWeb(nullptr), _mutex(nullptr),
      _workMode(MODE_AUTO), _tasksRunning(false)
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

/* startTasks - 创建并启动4个任务 */
bool TaskManager::startTasks()
{
    if (_tasksRunning) return true;

    BaseType_t r1 = xTaskCreatePinnedToCore(taskSensor,   "Sensor",
                       TASK_STACK_SENSOR,   this, TASK_PRIORITY_SENSOR,
                       &_hSensor,   TASK_CORE_SENSOR);
    BaseType_t r2 = xTaskCreatePinnedToCore(taskWifi,     "WiFi",
                       TASK_STACK_WIFI,     this, TASK_PRIORITY_WIFI,
                       &_hWifi,     TASK_CORE_WIFI);
    BaseType_t r3 = xTaskCreatePinnedToCore(taskDecision, "Decision",
                       TASK_STACK_DECISION, this, TASK_PRIORITY_DECISION,
                       &_hDecision, TASK_CORE_DECISION);
    BaseType_t r4 = xTaskCreatePinnedToCore(taskWeb,      "Web",
                       TASK_STACK_WEB,      this, TASK_PRIORITY_WEB,
                       &_hWeb,      TASK_CORE_WEB);

    _tasksRunning = (r1 == pdPASS && r2 == pdPASS && r3 == pdPASS && r4 == pdPASS);

    DBG_PRINTF("[TM] tasks %s\n", _tasksRunning ? "started (4 tasks)" : "CREATE FAIL");
    return _tasksRunning;
}

/* stopAll - 挂起所有任务 */
void TaskManager::stopAll()
{
    if (!_tasksRunning) return;
    if (_hSensor)   vTaskSuspend(_hSensor);
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

/* evaluateDanger - 危险等级判断(融合激光+超声波)
 *
 * 融合策略: 取激光和超声波中最近的距离值
 * 阈值逻辑:
 *   minDist < WARN_DISTANCE_DANGER (1.0m)  → DANGER
 *   minDist < WARN_DISTANCE_WARNING(2.5m)  → CAUTION
 *   minDist ≥ 2.5m                          → SAFE
 */
DangerLevel_t TaskManager::evaluateDanger(const SensorFrame_t* frame)
{
    if (!frame) return LEVEL_SAFE;

    /* 取前方最近距离(激光+超声波融合) */
    float minDist = 999.0f;

    /* 激光优先(精度高，量程远) */
    if (frame->laser.valid && frame->laser.distance > 0) {
        minDist = frame->laser.distance;
    }

    /* 超声波辅助(近距离盲区补充, HC-SR04最近可测2cm) */
    if (frame->ultrasonicDist > 0 && frame->ultrasonicDist < minDist) {
        minDist = frame->ultrasonicDist;
    }

    /* 按阈值判断等级(3级, 与APP AppConfig.java 一致) */
    if (minDist < WARN_DISTANCE_DANGER)    return LEVEL_DANGER;   // < 1.0m → 危险
    if (minDist < WARN_DISTANCE_WARNING)   return LEVEL_CAUTION;  // < 2.5m → 注意
    return LEVEL_SAFE;                                             // ≥ 2.5m → 安全
}

/* ============================================================
 * taskSensor - 传感器采集任务 (v3.0: 激光 + 超声波)
 *   周期: TASK_PERIOD_SENSOR_MS (50ms / 20Hz)
 *   读取激光+超声波,组装SensorFrame并更新共享区
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

        /* 读取 HC-SR04 超声波 */
        float usDist = self->_ultrasonic.readDistance();
        frame.ultrasonicDist = usDist;  // -1.0f 表示无效

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
 * taskWifi - WiFi通信任务 (v3.0: 仅TCP传感器上报，含camera_ip)
 *   周期上报传感器JSON + 处理TCP客户端
 *   JSON包含camera_ip字段，手机App据此直连摄像板HTTP拉流
 * ============================================================ */
void TaskManager::taskWifi(void* arg)
{
    TaskManager* self = (TaskManager*)arg;
    char jsonBuf[512];

    for (;;) {
        uint32_t startMs = millis();

        /* 1. 处理TCP客户端连接与命令 */
        self->_wifi.processTcpClients();

        /* 2. 有客户端时上报传感器JSON */
        if (self->_wifi.isClientConnected()) {
            SensorFrame_t f;
            if (self->getSensorFrame(&f)) {
                /* 组装JSON (v3.0: 激光+超声波+camera_ip，无雷达) */
                int n = snprintf(jsonBuf, sizeof(jsonBuf),
                    "{\"type\":\"sensor\",\"ts\":%lu,"
                    "\"laser_front\":%.2f,"
                    "\"ultrasonic\":%.2f,"
                    "\"camera_ip\":\"%s\","
                    "\"battery\":%d,\"mode\":%d,\"level\":%u}",
                    (unsigned long)f.timestamp,
                    f.laser.valid ? f.laser.distance : -1.0f,
                    f.ultrasonicDist,
                    CAMERA_ESP32_STATIC_IP,
                    -1,   // battery: 暂未接入电量检测, -1表示未知
                    (int)self->_workMode,
                    (unsigned)f.level);

                if (n > 0 && n < (int)sizeof(jsonBuf)) {
                    self->_wifi.sendSensorJson(jsonBuf, n);
                }
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
 * taskDecision - 主控决策任务 (v3.0: 激光+超声波融合判断)
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

            /* 危险等级防抖: 连续N帧DANGER才确认, 单帧降级为CAUTION */
            if (level >= LEVEL_DANGER) {
                if (dangerCount < 255) dangerCount++;
            } else {
                dangerCount = 0;
            }

            /* 未达到确认帧数前, 压制DANGER→CAUTION (防止单帧噪声误触发) */
            if (level >= LEVEL_DANGER && dangerCount < WARN_DEBOUNCE_FRAMES) {
                level = LEVEL_CAUTION;
            }

            DangerLevel_t finalLevel = level;

            /* 更新共享帧中的等级(加锁) */
            if (xSemaphoreTake(self->_mutex, 5)) {
                self->_sensorFrame.level = finalLevel;
                xSemaphoreGive(self->_mutex);
            }

            /* 控制报警 */
            if (finalLevel >= LEVEL_DANGER) {
                self->_alarm.setDangerAlarm(true);
                self->_alarm.setSystemState(SYS_STATE_DANGER);
            } else if (finalLevel == LEVEL_CAUTION) {
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
 * taskWeb - Web服务任务 (v3.0: 激光+超声波数据展示)
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
            bool laserOk = self->_sdm10.selfTest();
            DBG_PRINTF("[TM] calibrate result: laser=%s\n",
                       laserOk ? "OK" : "FAIL");
            self->_alarm.setSystemState(laserOk ? SYS_STATE_NORMAL : SYS_STATE_FAULT);
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
