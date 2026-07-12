#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include <freertos/queue.h>

#include "config.h"
#include "comm_protocol.h"

#include "drivers/ov2640_cam.h"
#include "comm/spi_master.h"
#include "image_proc/img_preprocess.h"

static const char* TAG = "CamMain";

OV2640Cam          g_camera;
SpiMaster          g_spiMaster;
ImagePreprocessor  g_imgPreproc;

SemaphoreHandle_t  g_frameMutex;
QueueHandle_t      g_frameQueue;

TaskHandle_t       g_taskCamCapture;
TaskHandle_t       g_taskImgProc;
TaskHandle_t       g_taskSpiSend;
TaskHandle_t       g_taskComm;
TaskHandle_t       g_taskHeartbeat;

static uint8_t*    g_capturedFrame = NULL;
static int         g_frameWidth = 0;
static int         g_frameHeight = 0;
static uint32_t    g_frameCount = 0;
static bool        g_systemReady = false;

typedef struct {
    uint8_t* data;
    int width;
    int height;
    uint8_t format;
    uint32_t size;
} FrameInfo_t;

static void taskCameraCapture(void* arg)
{
    ESP_LOGI(TAG, "Camera capture task started");

    camera_fb_t* fb = NULL;

    while (1) {
        if (g_camera.capture(&fb) && fb) {
            if (xSemaphoreTake(g_frameMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
                if (g_capturedFrame) {
                    free(g_capturedFrame);
                }
                g_capturedFrame = (uint8_t*)malloc(fb->len);
                if (g_capturedFrame) {
                    memcpy(g_capturedFrame, fb->buf, fb->len);
                    g_frameWidth = fb->width;
                    g_frameHeight = fb->height;
                    g_frameCount++;
                }
                xSemaphoreGive(g_frameMutex);
            }

            FrameInfo_t frameInfo;
            frameInfo.data = g_capturedFrame;
            frameInfo.width = fb->width;
            frameInfo.height = fb->height;
            frameInfo.format = IMG_FMT_RGB565;
            frameInfo.size = fb->len;

            xQueueSend(g_frameQueue, &frameInfo, 0);
            g_camera.returnFrame(fb);
            fb = NULL;
        } else {
            ESP_LOGW(TAG, "Camera capture failed");
        }

        vTaskDelay(pdMS_TO_TICKS(1000 / CAMERA_FPS_TARGET));
    }
}

static void taskImgPreprocess(void* arg)
{
    ESP_LOGI(TAG, "Image preprocess task started");

    FrameInfo_t frameInfo;

    while (1) {
        if (xQueueReceive(g_frameQueue, &frameInfo, portMAX_DELAY) == pdTRUE) {
            ESP_LOGD(TAG, "Preprocessing frame: %dx%d",
                     frameInfo.width, frameInfo.height);

            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }
}

static void taskSpiSend(void* arg)
{
    ESP_LOGI(TAG, "SPI send task started");

    FrameInfo_t frameInfo;

    while (1) {
        if (xQueueReceive(g_frameQueue, &frameInfo, pdMS_TO_TICKS(100)) == pdTRUE) {
            if (frameInfo.data && frameInfo.size > 0) {
                bool ok = g_spiMaster.sendImageFrame(
                    frameInfo.data,
                    frameInfo.width,
                    frameInfo.height,
                    frameInfo.format,
                    frameInfo.size);

                if (!ok) {
                    ESP_LOGW(TAG, "Failed to send image frame");
                }
            }
        }
    }
}

static void taskCommHandler(void* arg)
{
    ESP_LOGI(TAG, "Command handler task started");

    SpiFrame_t rxFrame;

    while (1) {
        if (xQueueReceive(g_spiMaster.getRxQueue(), &rxFrame, pdMS_TO_TICKS(100)) == pdTRUE) {
            ESP_LOGI(TAG, "Received command: 0x%02X, len=%u",
                     rxFrame.cmd, rxFrame.dataLen);

            switch (rxFrame.cmd) {
                case SPI_CMD_SET_RESOLUTION:
                    if (rxFrame.dataLen >= 1) {
                        ESP_LOGI(TAG, "Set resolution: %u", rxFrame.data[0]);
                    }
                    break;
                case SPI_CMD_SET_FPS:
                    if (rxFrame.dataLen >= 1) {
                        ESP_LOGI(TAG, "Set FPS: %u", rxFrame.data[0]);
                    }
                    break;
                default:
                    break;
            }
        }
    }
}

static void taskHeartbeatFunc(void* arg)
{
    ESP_LOGI(TAG, "Heartbeat task started");

    while (1) {
        uint32_t timestamp = (uint32_t)(esp_log_timestamp() / 1000);
        g_spiMaster.sendHeartbeat(timestamp);
        vTaskDelay(pdMS_TO_TICKS(HEARTBEAT_PERIOD_MS));
    }
}

void setup()
{
    Serial.begin(DEBUG_SERIAL_BAUDRATE);
    delay(1000);

    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "  智慧之眼导盲头环 - 摄像头板启动");
    ESP_LOGI(TAG, "========================================");

    g_frameMutex = xSemaphoreCreateMutex();
    g_frameQueue = xQueueCreate(2, sizeof(FrameInfo_t));

    ESP_LOGI(TAG, "Initializing camera...");
    bool camOk = g_camera.init(
        PIN_CAMERA_PWDN, PIN_CAMERA_RESET, PIN_CAMERA_XCLK,
        PIN_CAMERA_SIOD, PIN_CAMERA_SIOC,
        PIN_CAMERA_VSYNC, PIN_CAMERA_HREF, PIN_CAMERA_PCLK,
        PIN_CAMERA_D0, PIN_CAMERA_D1, PIN_CAMERA_D2, PIN_CAMERA_D3,
        PIN_CAMERA_D4, PIN_CAMERA_D5, PIN_CAMERA_D6, PIN_CAMERA_D7);

    if (camOk) {
        ESP_LOGI(TAG, "Camera initialized OK: %dx%d",
                 g_camera.getWidth(), g_camera.getHeight());
    } else {
        ESP_LOGE(TAG, "Camera initialization FAILED!");
    }

    ESP_LOGI(TAG, "Initializing image preprocessor...");
    g_imgPreproc.init(CAMERA_FRAME_WIDTH, CAMERA_FRAME_HEIGHT);

    ESP_LOGI(TAG, "Initializing SPI master...");
    g_spiMaster.init(PIN_SPI_MOSI, PIN_SPI_MISO, PIN_SPI_SCLK, PIN_SPI_CS);
    g_spiMaster.start();

    ESP_LOGI(TAG, "Creating tasks...");

    xTaskCreatePinnedToCore(
        taskCameraCapture, "cam_cap",
        8192, NULL,
        TASK_PRIORITY_CAM_CAPTURE, &g_taskCamCapture, 1);

    xTaskCreatePinnedToCore(
        taskImgPreprocess, "img_proc",
        TASK_STACK_IMG_PROC, NULL,
        TASK_PRIORITY_IMG_PROC, &g_taskImgProc, 1);

    xTaskCreatePinnedToCore(
        taskSpiSend, "spi_send",
        TASK_STACK_SPI_SEND, NULL,
        TASK_PRIORITY_SPI_SEND, &g_taskSpiSend, 0);

    xTaskCreatePinnedToCore(
        taskCommHandler, "comm",
        TASK_STACK_COMM, NULL,
        TASK_PRIORITY_COMM, &g_taskComm, 0);

    xTaskCreatePinnedToCore(
        taskHeartbeatFunc, "heartbeat",
        TASK_STACK_HEARTBEAT, NULL,
        TASK_PRIORITY_HEARTBEAT, &g_taskHeartbeat, 0);

    g_systemReady = true;
    ESP_LOGI(TAG, "Camera board initialization complete!");
}

void loop()
{
    if (!g_systemReady) {
        delay(100);
        return;
    }

    vTaskDelay(pdMS_TO_TICKS(1000));
}
