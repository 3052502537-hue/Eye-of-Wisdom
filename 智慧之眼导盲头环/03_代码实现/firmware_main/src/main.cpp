#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include <freertos/queue.h>

#include "config.h"
#include "types.h"
#include "comm_protocol.h"

#include "drivers/rd03d.h"
#include "drivers/buzzer.h"
#include "drivers/vibrator.h"
#include "drivers/status_led.h"

#include "perception/radar_front.h"
#include "perception/radar_rear.h"
#include "perception/lane_detection.h"
#include "perception/traffic_light.h"
#include "perception/crosswalk.h"

#include "comm/spi_slave.h"

#include "services/warning_service.h"
#include "services/state_manager.h"
#include "services/debug_log.h"

static const char* TAG = "Main";

RD03D               g_radarFront;
RD03D               g_radarRear;
Buzzer              g_buzzer;
Vibrator            g_vibrator;
StatusLED           g_statusLed;

RadarFrontProcessor g_radarFrontProc;
RadarRearProcessor  g_radarRearProc;
LaneDetector        g_laneDetector;
TrafficLightDetector g_trafficLightDetector;
CrosswalkDetector   g_crosswalkDetector;

SpiSlave            g_spiSlave;

WarningService      g_warningService;
StateManager        g_stateManager;
DebugLogger         g_debugLogger;

RadarData_t         g_frontRadarData;
RadarData_t         g_rearRadarData;
LaneResult_t        g_laneResult;
TrafficLightResult_t g_trafficLightResult;
CrosswalkResult_t   g_crosswalkResult;

SemaphoreHandle_t   g_radarFrontMutex;
SemaphoreHandle_t   g_radarRearMutex;
SemaphoreHandle_t   g_visionMutex;

QueueHandle_t       g_warningQueue;

TaskHandle_t        g_taskRadarFront;
TaskHandle_t        g_taskRadarRear;
TaskHandle_t        g_taskVision;
TaskHandle_t        g_taskWarningDecision;
TaskHandle_t        g_taskFeedbackOutput;
TaskHandle_t        g_taskStateManager;
TaskHandle_t        g_taskDebugLog;

static bool g_systemReady = false;

static void taskRadarFront(void* arg)
{
    ESP_LOGI(TAG, "Front radar task started");

    while (1) {
        RadarTarget_t targets[RADAR_MAX_TARGETS];
        uint8_t count = 0;

        if (g_radarFront.readTargets(targets, &count)) {
            if (xSemaphoreTake(g_radarFrontMutex, portMAX_DELAY) == pdTRUE) {
                g_frontRadarData.targetCount = count;
                memcpy(g_frontRadarData.targets, targets, count * sizeof(RadarTarget_t));
                g_frontRadarData.timestamp = (uint32_t)(esp_log_timestamp() / 1000);
                xSemaphoreGive(g_radarFrontMutex);
            }

            RadarData_t tempData;
            if (xSemaphoreTake(g_radarFrontMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
                memcpy(&tempData, &g_frontRadarData, sizeof(RadarData_t));
                xSemaphoreGive(g_radarFrontMutex);
                g_radarFrontProc.update(&tempData);
            }
        }

        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

static void taskRadarRear(void* arg)
{
    ESP_LOGI(TAG, "Rear radar task started");

    while (1) {
        RadarTarget_t targets[RADAR_MAX_TARGETS];
        uint8_t count = 0;

        if (g_radarRear.readTargets(targets, &count)) {
            if (xSemaphoreTake(g_radarRearMutex, portMAX_DELAY) == pdTRUE) {
                g_rearRadarData.targetCount = count;
                memcpy(g_rearRadarData.targets, targets, count * sizeof(RadarTarget_t));
                g_rearRadarData.timestamp = (uint32_t)(esp_log_timestamp() / 1000);
                xSemaphoreGive(g_radarRearMutex);
            }

            RadarData_t tempData;
            if (xSemaphoreTake(g_radarRearMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
                memcpy(&tempData, &g_rearRadarData, sizeof(RadarData_t));
                xSemaphoreGive(g_radarRearMutex);
                g_radarRearProc.update(&tempData);
            }
        }

        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

static void taskVisionProcess(void* arg)
{
    ESP_LOGI(TAG, "Vision process task started");

    while (1) {
        if (g_spiSlave.isImageReady()) {
            uint8_t* imgBuf = g_spiSlave.getImageBuffer();
            int width = g_spiSlave.getImageWidth();
            int height = g_spiSlave.getImageHeight();

            if (imgBuf && width > 0 && height > 0) {
                if (xSemaphoreTake(g_visionMutex, portMAX_DELAY) == pdTRUE) {
                    g_laneDetector.detect(imgBuf, width, height);
                    g_trafficLightDetector.detect(imgBuf, width, height);
                    g_crosswalkDetector.detect(imgBuf, width, height);

                    g_laneResult = g_laneDetector.getResult();
                    g_trafficLightResult = g_trafficLightDetector.getResult();
                    g_crosswalkResult = g_crosswalkDetector.getResult();

                    xSemaphoreGive(g_visionMutex);
                }
            }

            g_spiSlave.clearImageReady();
        }

        vTaskDelay(pdMS_TO_TICKS(33));
    }
}

static void taskWarningDecision(void* arg)
{
    ESP_LOGI(TAG, "Warning decision task started");

    while (1) {
        RadarData_t frontData, rearData;
        LaneResult_t laneResult;
        TrafficLightResult_t tlResult;
        CrosswalkResult_t cwResult;

        if (xSemaphoreTake(g_radarFrontMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
            memcpy(&frontData, &g_frontRadarData, sizeof(RadarData_t));
            xSemaphoreGive(g_radarFrontMutex);
        }

        if (xSemaphoreTake(g_radarRearMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
            memcpy(&rearData, &g_rearRadarData, sizeof(RadarData_t));
            xSemaphoreGive(g_radarRearMutex);
        }

        if (xSemaphoreTake(g_visionMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
            laneResult = g_laneResult;
            tlResult = g_trafficLightResult;
            cwResult = g_crosswalkResult;
            xSemaphoreGive(g_visionMutex);
        }

        g_warningService.update(&frontData, &rearData, &laneResult, &tlResult, &cwResult);

        vTaskDelay(pdMS_TO_TICKS(WARNING_UPDATE_PERIOD_MS));
    }
}

static void taskFeedbackOutput(void* arg)
{
    ESP_LOGI(TAG, "Feedback output task started");

    Warning_t warn;
    bool buzzerActive = false;
    uint32_t lastBuzzTime = 0;

    while (1) {
        if (xQueueReceive(g_warningService.getWarningQueue(), &warn, pdMS_TO_TICKS(50)) == pdTRUE) {
            g_debugLogger.logWarning(&warn);

            switch (warn.level) {
                case WARN_LEVEL_DANGER:
                    g_buzzer.startTone(BUZZER_FREQ_DANGER);
                    buzzerActive = true;
                    g_vibrator.vibrate(warn.direction, VIB_DURATION_DANGER);
                    break;
                case WARN_LEVEL_WARNING:
                    g_buzzer.beep(BUZZER_FREQ_WARNING, 100);
                    g_vibrator.vibrate(warn.direction, VIB_DURATION_WARNING);
                    break;
                case WARN_LEVEL_ATTENTION:
                    g_buzzer.beep(BUZZER_FREQ_ATTENTION, 50);
                    g_vibrator.vibrate(warn.direction, VIB_DURATION_ATTENTION);
                    break;
                default:
                    if (buzzerActive) {
                        g_buzzer.stopTone();
                        buzzerActive = false;
                    }
                    break;
            }
        } else {
            WarnLevel_t curLevel = g_warningService.getCurrentLevel();
            if (curLevel <= WARN_LEVEL_SAFE && buzzerActive) {
                g_buzzer.stopTone();
                buzzerActive = false;
            }
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

static void taskStateManagerFunc(void* arg)
{
    ESP_LOGI(TAG, "State manager task started");

    while (1) {
        g_stateManager.update();
        vTaskDelay(pdMS_TO_TICKS(STATE_UPDATE_PERIOD_MS));
    }
}

static void taskDebugLogFunc(void* arg)
{
    ESP_LOGI(TAG, "Debug log task started");

    while (1) {
        g_debugLogger.processCommands();
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

static void onImageReceived(void* arg)
{
    BaseType_t higherPriorityTaskWoken = pdFALSE;
    xTaskNotifyFromISR(g_taskVision, 0, eNoAction, &higherPriorityTaskWoken);
}

void setup()
{
    Serial.begin(DEBUG_SERIAL_BAUDRATE);
    delay(1000);

    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "  智慧之眼导盲头环 - 主控板启动");
    ESP_LOGI(TAG, "========================================");

    g_radarFrontMutex = xSemaphoreCreateMutex();
    g_radarRearMutex = xSemaphoreCreateMutex();
    g_visionMutex = xSemaphoreCreateMutex();

    ESP_LOGI(TAG, "Initializing drivers...");
    g_statusLed.init(PIN_STATUS_LED);
    g_statusLed.blink(100, 100, 3);

    g_radarFront.init(RADAR_FRONT_UART, PIN_RADAR_FRONT_TX, PIN_RADAR_FRONT_RX, RADAR_BAUDRATE);
    g_radarRear.init(RADAR_REAR_UART, PIN_RADAR_REAR_TX, PIN_RADAR_REAR_RX, RADAR_BAUDRATE);

    g_buzzer.init(PIN_BUZZER, BUZZER_LEDC_CHANNEL, BUZZER_LEDC_TIMER);
    g_vibrator.init(PIN_VIB_FRONT, PIN_VIB_REAR, PIN_VIB_LEFT, PIN_VIB_RIGHT);

    ESP_LOGI(TAG, "Initializing perception modules...");
    g_radarFrontProc.init();
    g_radarRearProc.init();
    g_laneDetector.init();
    g_trafficLightDetector.init();
    g_crosswalkDetector.init();

    ESP_LOGI(TAG, "Initializing SPI slave...");
    g_spiSlave.init(PIN_SPI_MOSI, PIN_SPI_MISO, PIN_SPI_SCLK, PIN_SPI_CS);
    g_spiSlave.registerImageCallback(onImageReceived, NULL);
    g_spiSlave.start();

    ESP_LOGI(TAG, "Initializing services...");
    g_warningService.init();
    g_stateManager.init();
    g_debugLogger.init();

    g_debugLogger.setRadarDataPtr(&g_frontRadarData, &g_rearRadarData);
    g_debugLogger.setLaneResultPtr(&g_laneResult);
    g_debugLogger.setTrafficLightPtr(&g_trafficLightResult);
    g_debugLogger.setCrosswalkPtr(&g_crosswalkResult);

    ESP_LOGI(TAG, "Creating FreeRTOS tasks...");

    xTaskCreatePinnedToCore(
        taskRadarFront, "radar_front",
        TASK_STACK_RADAR_FRONT, NULL,
        TASK_PRIORITY_RADAR_FRONT, &g_taskRadarFront, 1);

    xTaskCreatePinnedToCore(
        taskRadarRear, "radar_rear",
        TASK_STACK_RADAR_REAR, NULL,
        TASK_PRIORITY_RADAR_REAR, &g_taskRadarRear, 1);

    xTaskCreatePinnedToCore(
        taskVisionProcess, "vision_proc",
        TASK_STACK_VISION, NULL,
        TASK_PRIORITY_VISION, &g_taskVision, 1);

    xTaskCreatePinnedToCore(
        taskWarningDecision, "warn_dec",
        TASK_STACK_WARNING, NULL,
        TASK_PRIORITY_WARNING, &g_taskWarningDecision, 0);

    xTaskCreatePinnedToCore(
        taskFeedbackOutput, "feedback",
        TASK_STACK_FEEDBACK, NULL,
        TASK_PRIORITY_FEEDBACK, &g_taskFeedbackOutput, 0);

    xTaskCreatePinnedToCore(
        taskStateManagerFunc, "state_mgr",
        TASK_STACK_STATE_MGR, NULL,
        TASK_PRIORITY_STATE_MGR, &g_taskStateManager, 0);

    xTaskCreatePinnedToCore(
        taskDebugLogFunc, "debug_log",
        TASK_STACK_DEBUG, NULL,
        TASK_PRIORITY_DEBUG, &g_taskDebugLog, 0);

    g_systemReady = true;
    g_statusLed.blink(50, 50, 5);

    ESP_LOGI(TAG, "System initialization complete!");
    ESP_LOGI(TAG, "Type 'help' in serial monitor for debug commands");
}

void loop()
{
    if (!g_systemReady) {
        delay(100);
        return;
    }

    vTaskDelay(pdMS_TO_TICKS(1000));
}
