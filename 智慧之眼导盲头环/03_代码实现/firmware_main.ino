/*
  智慧之眼导盲头环 - 主控板固件 (单文件版)
  Arduino IDE 配置:
  - 开发板: ESP32S3 Dev Module
  - Flash Size: 16MB
  - PSRAM: OPI 8MB
  - Upload Mode: UART0 / Hardware CDC
  - USB Mode: Hardware CDC and JTAG
  - Partition Scheme: Default 16MB with FFAT

  引脚分配:
  - 前雷达: TX=17, RX=18 (UART1)
  - 后雷达: TX=19, RX=20 (UART2)
  - 蜂鸣器: GPIO 47
  - 振动马达: 前=42, 后=43, 左=44, 右=45
  - SPI(连摄像头): MOSI=35, MISO=36, SCLK=37, CS=38
  - LED: GPIO 48
*/

/* ============================================================
 *  智慧之眼导盲头环 - 主控板固件 (单文件版)
 *  版本: v1.0
 *  更新日期: 2026-07-10
 *
 *  说明: 本文件由 config.h / types.h / comm_protocol.h 以及
 *       drivers / perception / comm / services 各模块合并而成。
 *       可直接在 Arduino IDE 中打开烧录。
 *
 *  烧录说明:
 *    1. 使用 Arduino IDE 打开此 .ino 文件
 *    2. 在 Tools 菜单中选择 "ESP32S3 Dev Module"
 *    3. 按照文件头的配置注释设置 Flash/PSRAM/USB 等参数
 *    4. 选择正确的 COM 端口后点击 Upload
 * ============================================================ */

/* ============================================================
 *  系统头文件
 * ============================================================ */
#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include <freertos/queue.h>

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdarg.h>
#include <string.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include "esp_log.h"
#include "driver/uart.h"
#include "driver/ledc.h"
#include "driver/gpio.h"
#include "driver/spi_slave.h"

/* ============================================================
 *  一、引脚定义与可调参数 (来自 config.h)
 * ============================================================ */

/* 前雷达 - 前方障碍物检测 */
#define PIN_RADAR_FRONT_TX      17      /* 雷达TX → ESP32 RX */
#define PIN_RADAR_FRONT_RX      18      /* ESP32 TX → 雷达RX */
#define RADAR_FRONT_UART        UART_NUM_1

/* 后雷达 - 后方接近预警 */
#define PIN_RADAR_REAR_TX       19      /* 雷达TX → ESP32 RX */
#define PIN_RADAR_REAR_RX       20      /* ESP32 TX → 雷达RX */
#define RADAR_REAR_UART         UART_NUM_2

/* 雷达公共参数 */
#define RADAR_BAUDRATE          115200  /* RD03D 默认波特率 */

/* 蜂鸣器 - PWM控制不同频率 */
#define PIN_BUZZER              47      /* PWM输出引脚 */
#define BUZZER_LEDC_CHANNEL     0
#define BUZZER_LEDC_TIMER       LEDC_TIMER_0

/* 振动马达 - 四路方向指示 */
#define PIN_VIB_FRONT           42      /* 前方振动 */
#define PIN_VIB_REAR            43      /* 后方振动 */
#define PIN_VIB_LEFT            44      /* 左侧振动 */
#define PIN_VIB_RIGHT           45      /* 右侧振动 */

/* 状态LED */
#define PIN_STATUS_LED          48      /* 板载LED或外接LED */

/* SPI从机通信（连接摄像头板） */
#define PIN_SPI_MOSI            35      /* 摄像头板MOSI → 主控板MOSI */
#define PIN_SPI_MISO            36      /* 主控板MISO → 摄像头板MISO */
#define PIN_SPI_SCLK            37      /* 摄像头板SCLK → 主控板SCLK */
#define PIN_SPI_CS              38      /* 摄像头板CS → 主控板CS */
#define SPI_HOST                SPI2_HOST
#define SPI_CLOCK_SPEED         10000000 /* 10MHz */

/* SPI 图像分块传输的固定块大小 (spi_slave.cpp 中使用) */
#define SPI_SEND_BLOCK_SIZE     1024

/* 预警距离阈值配置 (单位: 米) */
#define WARN_DISTANCE_ATTENTION  3.0f   /* 大于此值: 安全 */
#define WARN_DISTANCE_WARNING    2.0f   /* 小于此值: 警告 */
#define WARN_DISTANCE_DANGER     1.0f   /* 小于此值: 危险 */

#define WARN_DEBOUNCE_FRAMES     3      /* 连续N帧确认才触发预警 */

/* 蜂鸣器预警参数 */
#define BUZZER_FREQ_ATTENTION    1000   /* 注意: 1kHz */
#define BUZZER_FREQ_WARNING      1500   /* 警告: 1.5kHz */
#define BUZZER_FREQ_DANGER       2000   /* 危险: 2kHz */

#define BUZZER_INTERVAL_ATTENTION  500  /* 间断间隔(ms) */
#define BUZZER_INTERVAL_WARNING    250
#define BUZZER_INTERVAL_DANGER     100  /* 危险时几乎连续 */

/* 振动马达预警参数 */
#define VIB_DURATION_ATTENTION   100    /* 单次振动时长(ms) */
#define VIB_DURATION_WARNING     150
#define VIB_DURATION_DANGER      200

/* 视觉检测配置 */
#define VISION_FRAME_WIDTH       320    /* QVGA 宽度 */
#define VISION_FRAME_HEIGHT      240    /* QVGA 高度 */
#define VISION_FPS_TARGET        15     /* 目标帧率 */

/* 盲道检测 */
#define LANE_ROI_TOP_RATIO       0.4f   /* ROI从图像40%高度处开始 */
#define LANE_DETECT_CONFIDENCE   0.5f   /* 最低置信度 */

/* 红绿灯检测 */
#define TL_ROI_TOP_RATIO         0.0f   /* ROI从顶部开始 */
#define TL_ROI_BOTTOM_RATIO      0.4f   /* ROI到40%高度处 */
#define TL_DETECT_CONFIDENCE     0.6f   /* 最低置信度 */

/* 斑马线检测 */
#define CW_ROI_TOP_RATIO         0.3f   /* ROI从30%高度处开始 */
#define CW_ROI_BOTTOM_RATIO      0.9f   /* ROI到90%高度处 */
#define CW_MIN_STRIPES           5      /* 最少条纹数 */
#define CW_DETECT_CONFIDENCE     0.5f   /* 最低置信度 */

/* FreeRTOS任务优先级 */
#define TASK_PRIORITY_RADAR_FRONT   5
#define TASK_PRIORITY_RADAR_REAR    5
#define TASK_PRIORITY_SPI_RECV      6
#define TASK_PRIORITY_VISION        4
#define TASK_PRIORITY_WARNING       6
#define TASK_PRIORITY_FEEDBACK      5
#define TASK_PRIORITY_STATE_MGR     3
#define TASK_PRIORITY_DEBUG         2
#define TASK_PRIORITY_HEARTBEAT     1

/* FreeRTOS任务栈大小 */
#define TASK_STACK_RADAR_FRONT   2048
#define TASK_STACK_RADAR_REAR    2048
#define TASK_STACK_SPI_RECV      4096
#define TASK_STACK_VISION        16384
#define TASK_STACK_WARNING       4096
#define TASK_STACK_FEEDBACK      2048
#define TASK_STACK_STATE_MGR     2048
#define TASK_STACK_DEBUG         4096
#define TASK_STACK_HEARTBEAT     1024

/* 任务周期与队列配置 */
#define WARNING_UPDATE_PERIOD_MS   100
#define STATE_UPDATE_PERIOD_MS     500
#define HEARTBEAT_PERIOD_MS        1000

#define DEBUG_QUEUE_SIZE           20
#define WARNING_QUEUE_SIZE         10

/* 调试串口配置 */
#define DEBUG_SERIAL_BAUDRATE      115200

/* ============================================================
 *  二、SPI 通信协议定义 (来自 comm_protocol.h)
 * ============================================================ */

#define SPI_FRAME_HEADER     0xAA55
#define SPI_FRAME_TAIL       0x55AA
#define SPI_FRAME_HEADER_LEN 2
#define SPI_FRAME_TAIL_LEN   2
#define SPI_FRAME_CMD_LEN    1
#define SPI_FRAME_LEN_LEN    2
#define SPI_FRAME_CRC_LEN    1
#define SPI_FRAME_OVERHEAD   (SPI_FRAME_HEADER_LEN + SPI_FRAME_CMD_LEN + SPI_FRAME_LEN_LEN + SPI_FRAME_CRC_LEN + SPI_FRAME_TAIL_LEN)

#define SPI_CMD_IMG_FRAME_START   0x01
#define SPI_CMD_IMG_FRAME_DATA    0x02
#define SPI_CMD_IMG_FRAME_END     0x03
#define SPI_CMD_SET_RESOLUTION    0x10
#define SPI_CMD_SET_FPS           0x11
#define SPI_CMD_ACK               0x20
#define SPI_CMD_HEARTBEAT         0x30

#define IMG_FMT_RGB565  0
#define IMG_FMT_GRAYSCALE 1
#define IMG_FMT_JPEG     2

#define RESOLUTION_QVGA    0
#define RESOLUTION_VGA     1
#define RESOLUTION_QVGA_W  320
#define RESOLUTION_QVGA_H  240
#define RESOLUTION_VGA_W   640
#define RESOLUTION_VGA_H   480

#define SPI_MAX_DATA_LEN  4096

/* SPI 从机最大图像缓冲区大小 (RGB565 QVGA) */
#define SPI_SLAVE_MAX_IMG_BUF_SIZE (320 * 240 * 2)

/* ============================================================
 *  三、数据结构定义 (来自 types.h)
 * ============================================================ */

#define RADAR_MAX_TARGETS 5

typedef struct {
    uint8_t  targetId;
    float    distance;
    float    speed;
    float    angle;
    float    snr;
} RadarTarget_t;

typedef struct {
    RadarTarget_t targets[RADAR_MAX_TARGETS];
    uint8_t       targetCount;
    uint32_t      timestamp;
} RadarData_t;

typedef enum {
    LANE_CENTER = 0,
    LANE_LEFT,
    LANE_RIGHT,
    LANE_LOST
} LanePosition_t;

typedef struct {
    bool           detected;
    LanePosition_t position;
    float          confidence;
    float          offsetRatio;
    uint32_t       timestamp;
} LaneResult_t;

typedef enum {
    LIGHT_NONE = 0,
    LIGHT_RED,
    LIGHT_YELLOW,
    LIGHT_GREEN
} TrafficLightState_t;

typedef struct {
    bool                detected;
    TrafficLightState_t state;
    float               confidence;
    uint32_t            timestamp;
} TrafficLightResult_t;

typedef struct {
    bool     detected;
    float    confidence;
    uint8_t  stripeCount;
    uint32_t timestamp;
} CrosswalkResult_t;

typedef enum {
    WARN_DIR_FRONT = 0,
    WARN_DIR_FRONT_LEFT,
    WARN_DIR_FRONT_RIGHT,
    WARN_DIR_LEFT,
    WARN_DIR_RIGHT,
    WARN_DIR_REAR,
    WARN_DIR_ALL
} WarnDirection_t;

typedef enum {
    WARN_LEVEL_SAFE = 0,
    WARN_LEVEL_ATTENTION,
    WARN_LEVEL_WARNING,
    WARN_LEVEL_DANGER
} WarnLevel_t;

typedef enum {
    WARN_SOURCE_RADAR_FRONT = 0,
    WARN_SOURCE_RADAR_REAR,
    WARN_SOURCE_LANE_DEPARTURE,
    WARN_SOURCE_TRAFFIC_LIGHT,
    WARN_SOURCE_CROSSWALK
} WarnSource_t;

typedef struct {
    WarnSource_t    source;
    WarnLevel_t     level;
    WarnDirection_t direction;
    float           distance;
    char            message[64];
    uint32_t        timestamp;
} Warning_t;

typedef enum {
    SYS_STATE_INIT = 0,
    SYS_STATE_STANDBY,
    SYS_STATE_WALKING,
    SYS_STATE_WARNING,
    SYS_STATE_FAULT
} SystemState_t;

/* ============================================================
 *  四、SPI 通信协议数据结构 (来自 comm_protocol.h)
 * ============================================================ */

typedef struct {
    uint8_t  cmd;
    uint16_t dataLen;
    uint8_t  data[SPI_MAX_DATA_LEN];
} SpiFrame_t;

typedef struct {
    uint16_t width;
    uint16_t height;
    uint8_t  format;
    uint32_t totalSize;
} ImgFrameStart_t;

typedef struct {
    uint16_t blockIndex;
    uint16_t blockSize;
    uint8_t* data;
} ImgFrameData_t;

typedef struct {
    uint16_t totalBlocks;
} ImgFrameEnd_t;

typedef struct {
    uint8_t  originalCmd;
    uint8_t  status;
} AckPayload_t;

typedef struct {
    uint32_t timestamp;
} HeartbeatPayload_t;

/* CRC8 校验函数 (来自 comm_protocol.h) */
static inline uint8_t crc8(const uint8_t* data, size_t len)
{
    uint8_t crc = 0x00;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (uint8_t j = 0; j < 8; j++) {
            if (crc & 0x80) {
                crc = (crc << 1) ^ 0x07;
            } else {
                crc <<= 1;
            }
        }
    }
    return crc;
}

/* ============================================================
 *  五、模块专属枚举定义
 * ============================================================ */

/* 前雷达分区 (来自 radar_front.h) */
#define FRONT_RADAR_ZONES 3

typedef enum {
    ZONE_FRONT_LEFT = 0,
    ZONE_FRONT_CENTER,
    ZONE_FRONT_RIGHT
} FrontRadarZone_t;

/* 盲道颜色模式 (来自 lane_detection.h) */
typedef enum {
    LANE_COLOR_YELLOW = 0,
    LANE_COLOR_GRAY,
    LANE_COLOR_AUTO
} LaneColorMode_t;

/* 状态变更回调函数类型 (来自 state_manager.h) */
typedef void (*StateChangeCallback)(SystemState_t oldState, SystemState_t newState, void* arg);

/* 调试命令枚举 (来自 debug_log.h) */
typedef enum {
    DEBUG_CMD_HELP = 0,
    DEBUG_CMD_RADAR,
    DEBUG_CMD_LANE,
    DEBUG_CMD_LIGHT,
    DEBUG_CMD_WARN,
    DEBUG_CMD_BUZZER,
    DEBUG_CMD_VIB,
    DEBUG_CMD_FPS,
    DEBUG_CMD_STATE,
    DEBUG_CMD_UNKNOWN
} DebugCommand_t;

/* ============================================================
 *  六、类声明
 * ============================================================ */

/* ---- RD03D 雷达驱动 ---- */
class RD03D {
public:
    RD03D();
    ~RD03D();

    bool init(uart_port_t uartNum, int txPin, int rxPin, uint32_t baudrate = 115200);
    bool readTargets(RadarTarget_t* targets, uint8_t* count);
    bool setSensitivity(uint8_t level);
    bool setMaxDistance(float distance);
    bool setBaudrate(uint32_t baudrate);
    void reset(void);

private:
    uart_port_t _uartNum;
    bool _initialized;

    bool parseFrame(const uint8_t* data, size_t len, RadarTarget_t* targets, uint8_t* count);
    bool sendCommand(uint8_t cmd, const uint8_t* payload, uint8_t payloadLen);
    size_t readAvailable(uint8_t* buf, size_t bufSize, uint32_t timeoutMs);
    bool findFrameHeader(const uint8_t* data, size_t len, size_t* offset);
    uint8_t calculateChecksum(const uint8_t* data, size_t len);
};

/* ---- 蜂鸣器驱动 ---- */
class Buzzer {
public:
    Buzzer();
    ~Buzzer();

    bool init(int pin, int ledcChannel = 0, int ledcTimer = 0);
    void beep(uint32_t frequency, uint32_t durationMs);
    void startTone(uint32_t frequency);
    void stopTone(void);
    void setVolume(uint8_t volume);
    void playPattern(const uint16_t* pattern, uint8_t count);

    bool isBeeping(void) const { return _beeping; }

private:
    int     _pin;
    int     _ledcChannel;
    int     _ledcTimer;
    bool    _initialized;
    bool    _beeping;
    uint8_t _volume;
};

/* ---- 振动马达驱动 ---- */
class Vibrator {
public:
    Vibrator();
    ~Vibrator();

    bool init(int frontPin, int rearPin, int leftPin, int rightPin);

    void vibrate(WarnDirection_t direction, uint32_t durationMs);
    void vibrateFront(uint32_t durationMs);
    void vibrateRear(uint32_t durationMs);
    void vibrateLeft(uint32_t durationMs);
    void vibrateRight(uint32_t durationMs);
    void vibrateAll(uint32_t durationMs);
    void stopAll(void);

    void setIntensity(uint8_t intensity);
    uint8_t getIntensity(void) const { return _intensity; }

private:
    int  _frontPin;
    int  _rearPin;
    int  _leftPin;
    int  _rightPin;
    bool _initialized;
    uint8_t _intensity;

    void setPin(int pin, bool state);
};

/* ---- 状态LED驱动 ---- */
class StatusLED {
public:
    StatusLED();
    ~StatusLED();

    bool init(int pin);

    void on(void);
    void off(void);
    void toggle(void);
    void blink(uint32_t onMs, uint32_t offMs, uint8_t count);
    void setPattern(uint8_t pattern);

    bool getState(void) const { return _state; }

private:
    int  _pin;
    bool _initialized;
    bool _state;
};

/* ---- 前雷达处理 ---- */
class RadarFrontProcessor {
public:
    RadarFrontProcessor();
    ~RadarFrontProcessor();

    void init(void);
    void update(const RadarData_t* rawData);

    float getNearestDistance(FrontRadarZone_t zone) const;
    WarnLevel_t getDangerLevel(FrontRadarZone_t zone) const;
    WarnLevel_t getOverallDangerLevel(void) const;
    WarnDirection_t getClosestDirection(void) const;
    float getClosestDistance(void) const;

    void setDangerThresholds(float attention, float warning, float danger);
    void setDebounceFrames(uint8_t frames);

    const RadarData_t* getFilteredData(void) const { return &_filteredData; }

private:
    RadarData_t _filteredData;
    float       _zoneDistances[FRONT_RADAR_ZONES];
    WarnLevel_t _zoneLevels[FRONT_RADAR_ZONES];
    WarnLevel_t _overallLevel;
    WarnDirection_t _closestDir;
    float       _closestDist;

    float _thresholdAttention;
    float _thresholdWarning;
    float _thresholdDanger;

    uint8_t _debounceFrames;
    uint8_t _levelStableCount[FRONT_RADAR_ZONES];
    WarnLevel_t _pendingLevels[FRONT_RADAR_ZONES];

    void assignTargetsToZones(const RadarData_t* rawData);
    void calculateDangerLevels(void);
    void updateOverallLevel(void);
    void applyDebounce(void);
    WarnLevel_t distanceToLevel(float distance) const;
};

/* ---- 后雷达处理 ---- */
class RadarRearProcessor {
public:
    RadarRearProcessor();
    ~RadarRearProcessor();

    void init(void);
    void update(const RadarData_t* rawData);

    bool isApproachingFast(void) const;
    float getApproachSpeed(void) const;
    float getNearestDistance(void) const;
    WarnLevel_t getDangerLevel(void) const;

    void setFastApproachSpeed(float speedMps);
    void setDangerThresholds(float attention, float warning, float danger);
    void setDebounceFrames(uint8_t frames);

    const RadarData_t* getFilteredData(void) const { return &_filteredData; }

private:
    RadarData_t _filteredData;
    float       _nearestDistance;
    float       _approachSpeed;
    bool        _isApproachingFast;
    WarnLevel_t _dangerLevel;

    float _fastApproachSpeed;
    float _thresholdAttention;
    float _thresholdWarning;
    float _thresholdDanger;

    uint8_t _debounceFrames;
    uint8_t _fastApproachCount;
    bool    _pendingFastApproach;

    void findNearestTarget(const RadarData_t* rawData);
    void calculateDangerLevel(void);
    void applyDebounce(void);
    WarnLevel_t distanceToLevel(float distance) const;
};

/* ---- 盲道检测 ---- */
class LaneDetector {
public:
    LaneDetector();
    ~LaneDetector();

    void init(void);
    void detect(uint8_t* image, int width, int height);

    LaneResult_t getResult(void) const { return _result; }

    void setColorMode(LaneColorMode_t mode);
    LaneColorMode_t getColorMode(void) const { return _colorMode; }

    void setRoiTopRatio(float ratio);
    void setConfidenceThreshold(float threshold);
    void setDebounceFrames(uint8_t frames);

    void setHsvYellowLow(uint8_t h, uint8_t s, uint8_t v);
    void setHsvYellowHigh(uint8_t h, uint8_t s, uint8_t v);

private:
    LaneResult_t _result;
    LaneColorMode_t _colorMode;
    float _roiTopRatio;
    float _confidenceThreshold;
    uint8_t _debounceFrames;

    uint8_t _hsvYellowLow[3];
    uint8_t _hsvYellowHigh[3];

    uint8_t _stableCount;
    LanePosition_t _pendingPosition;
    bool _pendingDetected;

    void extractRoi(uint8_t* image, int width, int height,
                    uint8_t** roiOut, int* roiW, int* roiH);

    bool convertToHsv(uint8_t* rgbImage, uint8_t* hsvImage, int w, int h);
    int colorThreshold(uint8_t* hsvImage, uint8_t* binary, int w, int h);
    void edgeDetect(uint8_t* gray, uint8_t* edges, int w, int h);
    int houghLines(uint8_t* edges, int w, int h, float* lines, int maxLines);
    LanePosition_t calculatePosition(float* lines, int lineCount, int width);
    float calculateConfidence(int lineCount, int validPairs);
    void applyDebounce(void);
};

/* ---- 红绿灯检测 ---- */
class TrafficLightDetector {
public:
    TrafficLightDetector();
    ~TrafficLightDetector();

    void init(void);
    void detect(uint8_t* image, int width, int height);

    TrafficLightResult_t getResult(void) const { return _result; }

    void setRoiRange(float topRatio, float bottomRatio);
    void setConfidenceThreshold(float threshold);
    void setDebounceFrames(uint8_t frames);

    void setRedHsvRange(uint8_t hLow1, uint8_t hHigh1, uint8_t hLow2, uint8_t hHigh2,
                        uint8_t sLow, uint8_t vLow);
    void setYellowHsvRange(uint8_t hLow, uint8_t hHigh, uint8_t sLow, uint8_t vLow);
    void setGreenHsvRange(uint8_t hLow, uint8_t hHigh, uint8_t sLow, uint8_t vLow);

    void setMinLightArea(int minArea);
    void setCircularityThreshold(float threshold);

private:
    TrafficLightResult_t _result;

    float _roiTopRatio;
    float _roiBottomRatio;
    float _confidenceThreshold;
    uint8_t _debounceFrames;

    uint8_t _redHlow1, _redHhigh1;
    uint8_t _redHlow2, _redHhigh2;
    uint8_t _redSlow, _redVlow;

    uint8_t _yellowHlow, _yellowHhigh;
    uint8_t _yellowSlow, _yellowVlow;

    uint8_t _greenHlow, _greenHhigh;
    uint8_t _greenSlow, _greenVlow;

    int   _minLightArea;
    float _circularityThreshold;

    uint8_t _stableCount;
    TrafficLightState_t _pendingState;
    bool _pendingDetected;

    void extractRoi(uint8_t* image, int width, int height,
                    uint8_t** roiOut, int* roiW, int* roiH, int* roiX, int* roiY);
    bool convertToHsv(uint8_t* rgbImage, uint8_t* hsvImage, int w, int h);
    int  colorSegmentation(uint8_t* hsvImage, uint8_t* mask, int w, int h,
                           uint8_t hLow, uint8_t hHigh,
                           uint8_t sLow, uint8_t vLow);
    void morphologicalOps(uint8_t* binary, int w, int h);
    int  findLightContours(uint8_t* binary, int w, int h,
                           int* centersX, int* centersY, int* areas, int maxLights);
    float calculateCircularity(int area, float perimeter);
    TrafficLightState_t determineState(int redCount, int yellowCount, int greenCount,
                                       float redConf, float yellowConf, float greenConf);
    void applyDebounce(void);
};

/* ---- 斑马线检测 ---- */
class CrosswalkDetector {
public:
    CrosswalkDetector();
    ~CrosswalkDetector();

    void init(void);
    void detect(uint8_t* image, int width, int height);

    CrosswalkResult_t getResult(void) const { return _result; }

    void setRoiRange(float topRatio, float bottomRatio);
    void setMinStripes(uint8_t count);
    void setConfidenceThreshold(float threshold);
    void setDebounceFrames(uint8_t frames);

    void setStripeWidthRange(int minWidth, int maxWidth);
    void setStripeSpacingTolerance(float tolerance);

private:
    CrosswalkResult_t _result;

    float _roiTopRatio;
    float _roiBottomRatio;
    uint8_t _minStripes;
    float _confidenceThreshold;
    uint8_t _debounceFrames;

    int   _minStripeWidth;
    int   _maxStripeWidth;
    float _stripeSpacingTolerance;

    uint8_t _stableCount;
    bool _pendingDetected;
    uint8_t _pendingStripeCount;

    void extractRoi(uint8_t* image, int width, int height,
                    uint8_t** roiOut, int* roiW, int* roiH);
    void convertToGray(uint8_t* rgbImage, uint8_t* gray, int w, int h);
    void edgeDetect(uint8_t* gray, uint8_t* edges, int w, int h);
    int houghHorizontalLines(uint8_t* edges, int w, int h,
                             float* lines, int maxLines);
    int findParallelStripes(float* lines, int lineCount, int width,
                            int* stripePositions, int* stripeWidths, int maxStripes);
    int countEquallySpacedStripes(int* positions, int count, float tolerance);
    float calculateConfidence(int stripeCount, int validCount);
    void applyDebounce(void);
};

/* ---- SPI 从机通信 ---- */
class SpiSlave {
public:
    SpiSlave();
    ~SpiSlave();

    bool init(int mosiPin, int misoPin, int sclkPin, int csPin);
    void start(void);
    void stop(void);

    bool isImageReady(void) const { return _imageReady; }
    uint8_t* getImageBuffer(void) { return _imageBuffer; }
    int getImageWidth(void) const { return _imageWidth; }
    int getImageHeight(void) const { return _imageHeight; }
    uint8_t getImageFormat(void) const { return _imageFormat; }
    void clearImageReady(void) { _imageReady = false; }

    bool sendCommand(uint8_t cmd, const uint8_t* data, uint16_t len);
    bool waitForImage(uint32_t timeoutMs);

    QueueHandle_t getFrameQueue(void) const { return _frameQueue; }

    void registerImageCallback(void (*callback)(void* arg), void* arg);

private:
    int _mosiPin;
    int _misoPin;
    int _sclkPin;
    int _csPin;
    bool _initialized;
    bool _running;

    uint8_t* _imageBuffer;
    int      _imageWidth;
    int      _imageHeight;
    uint8_t  _imageFormat;
    bool     _imageReady;

    uint32_t _imgTotalSize;
    uint16_t _imgTotalBlocks;
    uint16_t _imgReceivedBlocks;

    QueueHandle_t _frameQueue;

    void (*_imageCallback)(void* arg);
    void* _imageCallbackArg;

    bool parseFrame(const uint8_t* data, size_t len);
    bool handleFrameStart(const uint8_t* data, uint16_t len);
    bool handleFrameData(const uint8_t* data, uint16_t len);
    bool handleFrameEnd(const uint8_t* data, uint16_t len);
    bool handleCommand(const uint8_t* data, uint16_t len);
    bool handleAck(const uint8_t* data, uint16_t len);
    bool handleHeartbeat(const uint8_t* data, uint16_t len);

    bool sendAck(uint8_t originalCmd, uint8_t status);
    bool buildFrame(uint8_t cmd, const uint8_t* data, uint16_t len,
                   uint8_t* outBuf, uint16_t* outLen);

    static void spiTaskFunc(void* arg);
    void spiTask(void);
};

/* ---- 预警服务 ---- */
class WarningService {
public:
    WarningService();
    ~WarningService();

    bool init(void);
    void update(const RadarData_t* frontRadar, const RadarData_t* rearRadar,
                const LaneResult_t* lane, const TrafficLightResult_t* trafficLight,
                const CrosswalkResult_t* crosswalk);

    void trigger(WarnSource_t source, WarnLevel_t level,
                 WarnDirection_t direction, float distance, const char* message);

    QueueHandle_t getWarningQueue(void) const { return _warningQueue; }

    WarnLevel_t getCurrentLevel(void) const { return _currentLevel; }
    WarnSource_t getTopSource(void) const { return _topSource; }

    void setDebounceFrames(uint8_t frames) { _debounceFrames = frames; }

    void dumpStatus(void);

private:
    QueueHandle_t _warningQueue;
    WarnLevel_t   _currentLevel;
    WarnSource_t  _topSource;
    WarnDirection_t _currentDirection;
    uint8_t       _debounceFrames;

    WarnLevel_t _pendingLevel;
    WarnSource_t _pendingSource;
    WarnDirection_t _pendingDirection;
    uint8_t _stableCount;

    WarnLevel_t evaluateFrontRadar(const RadarData_t* radar);
    WarnLevel_t evaluateRearRadar(const RadarData_t* radar);
    WarnLevel_t evaluateLane(const LaneResult_t* lane);
    WarnLevel_t evaluateTrafficLight(const TrafficLightResult_t* tl);
    WarnLevel_t evaluateCrosswalk(const CrosswalkResult_t* cw);

    WarnLevel_t getHigherLevel(WarnLevel_t a, WarnLevel_t b);
    void applyDebounce(WarnLevel_t newLevel, WarnSource_t newSource, WarnDirection_t newDir);
    void dispatchWarning(WarnSource_t source, WarnLevel_t level,
                         WarnDirection_t direction, float distance);
};

/* ---- 状态管理 ---- */
class StateManager {
public:
    StateManager();
    ~StateManager();

    void init(void);
    void update(void);

    SystemState_t getState(void) const { return _currentState; }
    bool setState(SystemState_t newState);

    void registerCallback(StateChangeCallback callback, void* arg);

    const char* getStateName(SystemState_t state) const;

    bool isWalking(void) const { return _currentState == SYS_STATE_WALKING; }
    bool isWarning(void) const { return _currentState == SYS_STATE_WARNING; }
    bool isFault(void) const { return _currentState == SYS_STATE_FAULT; }

    void setRadarOnline(bool front, bool rear);
    void setCameraOnline(bool online);
    bool getRadarFrontOnline(void) const { return _radarFrontOnline; }
    bool getRadarRearOnline(void) const { return _radarRearOnline; }
    bool getCameraOnline(void) const { return _cameraOnline; }

private:
    SystemState_t _currentState;
    SystemState_t _previousState;

    bool _radarFrontOnline;
    bool _radarRearOnline;
    bool _cameraOnline;

    StateChangeCallback _callback;
    void* _callbackArg;

    uint32_t _stateEnterTime;

    bool canTransition(SystemState_t from, SystemState_t to);
    void onStateChanged(SystemState_t oldState, SystemState_t newState);
};

/* ---- 调试日志 ---- */
class DebugLogger {
public:
    DebugLogger();
    ~DebugLogger();

    bool init(void);
    void processCommands(void);

    void log(const char* tag, const char* fmt, ...);
    void logWarning(const Warning_t* warn);

    void printHelp(void);
    void printRadarStatus(void);
    void printLaneStatus(void);
    void printLightStatus(void);
    void printWarningStatus(void);
    void printState(void);
    void printFps(void);

    QueueHandle_t getLogQueue(void) const { return _logQueue; }

    void testBuzzer(uint32_t freq);
    void testVibrator(WarnDirection_t dir);

    void setRadarDataPtr(const RadarData_t* front, const RadarData_t* rear);
    void setLaneResultPtr(const LaneResult_t* lane);
    void setTrafficLightPtr(const TrafficLightResult_t* tl);
    void setCrosswalkPtr(const CrosswalkResult_t* cw);

private:
    QueueHandle_t _logQueue;
    bool _initialized;

    const RadarData_t* _frontRadarPtr;
    const RadarData_t* _rearRadarPtr;
    const LaneResult_t* _lanePtr;
    const TrafficLightResult_t* _tlPtr;
    const CrosswalkResult_t* _cwPtr;

    DebugCommand_t parseCommand(const char* cmdStr);
    void handleCommand(const char* cmdStr);
};

/* ============================================================
 *  七、类实现
 * ============================================================ */

/* ---- RD03D 雷达驱动实现 ---- */
static const char* TAG_RD03D = "RD03D";

RD03D::RD03D() : _uartNum(UART_NUM_0), _initialized(false)
{
}

RD03D::~RD03D()
{
    if (_initialized) {
        uart_driver_delete(_uartNum);
        _initialized = false;
    }
}

bool RD03D::init(uart_port_t uartNum, int txPin, int rxPin, uint32_t baudrate)
{
    if (_initialized) {
        ESP_LOGW(TAG_RD03D, "Already initialized");
        return false;
    }

    _uartNum = uartNum;

    uart_config_t uartConfig = {
        .baud_rate = (int)baudrate,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    esp_err_t ret = uart_driver_install(_uartNum, 1024 * 4, 0, 0, NULL, 0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG_RD03D, "UART driver install failed: %s", esp_err_to_name(ret));
        return false;
    }

    ret = uart_param_config(_uartNum, &uartConfig);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG_RD03D, "UART param config failed: %s", esp_err_to_name(ret));
        return false;
    }

    ret = uart_set_pin(_uartNum, txPin, rxPin, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG_RD03D, "UART set pin failed: %s", esp_err_to_name(ret));
        return false;
    }

    _initialized = true;
    ESP_LOGI(TAG_RD03D, "RD03D initialized on UART%d, baud=%lu", uartNum, (unsigned long)baudrate);
    return true;
}

bool RD03D::readTargets(RadarTarget_t* targets, uint8_t* count)
{
    if (!_initialized || !targets || !count) {
        return false;
    }

    uint8_t buf[512];
    size_t readLen = readAvailable(buf, sizeof(buf), 100);

    if (readLen == 0) {
        *count = 0;
        return true;
    }

    return parseFrame(buf, readLen, targets, count);
}

bool RD03D::parseFrame(const uint8_t* data, size_t len, RadarTarget_t* targets, uint8_t* count)
{
    if (!data || len < 8 || !targets || !count) {
        return false;
    }

    *count = 0;

    size_t offset = 0;
    if (!findFrameHeader(data, len, &offset)) {
        return false;
    }

    ESP_LOGD(TAG_RD03D, "Frame found at offset %u, data[0]=0x%02X data[1]=0x%02X",
             (unsigned)offset, data[offset], data[offset + 1]);

    return true;
}

bool RD03D::sendCommand(uint8_t cmd, const uint8_t* payload, uint8_t payloadLen)
{
    if (!_initialized) {
        return false;
    }

    return true;
}

size_t RD03D::readAvailable(uint8_t* buf, size_t bufSize, uint32_t timeoutMs)
{
    if (!_initialized || !buf || bufSize == 0) {
        return 0;
    }

    int len = uart_read_bytes(_uartNum, buf, bufSize, timeoutMs / portTICK_PERIOD_MS);
    return (len > 0) ? (size_t)len : 0;
}

bool RD03D::findFrameHeader(const uint8_t* data, size_t len, size_t* offset)
{
    if (!data || len < 2 || !offset) {
        return false;
    }

    for (size_t i = 0; i < len - 1; i++) {
        if (data[i] == 0xAA && data[i + 1] == 0x55) {
            *offset = i;
            return true;
        }
    }
    return false;
}

uint8_t RD03D::calculateChecksum(const uint8_t* data, size_t len)
{
    uint8_t sum = 0;
    for (size_t i = 0; i < len; i++) {
        sum += data[i];
    }
    return sum;
}

bool RD03D::setSensitivity(uint8_t level)
{
    if (!_initialized) {
        return false;
    }

    ESP_LOGI(TAG_RD03D, "Set sensitivity level: %u (TODO: implement)", level);
    return true;
}

bool RD03D::setMaxDistance(float distance)
{
    if (!_initialized) {
        return false;
    }

    ESP_LOGI(TAG_RD03D, "Set max distance: %.2f (TODO: implement)", distance);
    return true;
}

bool RD03D::setBaudrate(uint32_t baudrate)
{
    if (!_initialized) {
        return false;
    }

    ESP_LOGI(TAG_RD03D, "Set baudrate: %lu (TODO: implement)", (unsigned long)baudrate);
    return true;
}

void RD03D::reset(void)
{
    ESP_LOGI(TAG_RD03D, "Reset RD03D (TODO: implement)");
}

/* ---- 蜂鸣器驱动实现 ---- */
static const char* TAG_Buzzer = "Buzzer";

Buzzer::Buzzer()
    : _pin(-1), _ledcChannel(0), _ledcTimer(0),
      _initialized(false), _beeping(false), _volume(128)
{
}

Buzzer::~Buzzer()
{
    if (_initialized) {
        ledc_stop((ledc_mode_t)LEDC_LOW_SPEED_MODE, (ledc_channel_t)_ledcChannel, 0);
        _initialized = false;
    }
}

bool Buzzer::init(int pin, int ledcChannel, int ledcTimer)
{
    if (_initialized) {
        return false;
    }

    _pin = pin;
    _ledcChannel = ledcChannel;
    _ledcTimer = ledcTimer;

    ledc_timer_config_t timerConfig = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .duty_resolution = LEDC_TIMER_10_BIT,
        .timer_num = (ledc_timer_t)ledcTimer,
        .freq_hz = 1000,
        .clk_cfg = LEDC_AUTO_CLK,
    };

    esp_err_t ret = ledc_timer_config(&timerConfig);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG_Buzzer, "LEDC timer config failed: %s", esp_err_to_name(ret));
        return false;
    }

    ledc_channel_config_t channelConfig = {
        .gpio_num = (gpio_num_t)pin,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = (ledc_channel_t)ledcChannel,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = (ledc_timer_t)ledcTimer,
        .duty = 0,
        .hpoint = 0,
    };

    ret = ledc_channel_config(&channelConfig);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG_Buzzer, "LEDC channel config failed: %s", esp_err_to_name(ret));
        return false;
    }

    _initialized = true;
    ESP_LOGI(TAG_Buzzer, "Buzzer initialized on pin %d, channel %d", pin, ledcChannel);
    return true;
}

void Buzzer::beep(uint32_t frequency, uint32_t durationMs)
{
    if (!_initialized) {
        return;
    }

    startTone(frequency);
    vTaskDelay(pdMS_TO_TICKS(durationMs));
    stopTone();
}

void Buzzer::startTone(uint32_t frequency)
{
    if (!_initialized || frequency == 0) {
        return;
    }

    ledc_set_freq(LEDC_LOW_SPEED_MODE, (ledc_timer_t)_ledcTimer, frequency);

    uint32_t duty = (512 * _volume) / 255;
    ledc_set_duty(LEDC_LOW_SPEED_MODE, (ledc_channel_t)_ledcChannel, duty);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, (ledc_channel_t)_ledcChannel);

    _beeping = true;
    ESP_LOGD(TAG_Buzzer, "Start tone: %lu Hz", (unsigned long)frequency);
}

void Buzzer::stopTone(void)
{
    if (!_initialized) {
        return;
    }

    ledc_set_duty(LEDC_LOW_SPEED_MODE, (ledc_channel_t)_ledcChannel, 0);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, (ledc_channel_t)_ledcChannel);

    _beeping = false;
    ESP_LOGD(TAG_Buzzer, "Stop tone");
}

void Buzzer::setVolume(uint8_t volume)
{
    _volume = volume;
    ESP_LOGD(TAG_Buzzer, "Volume set to %u", volume);
}

void Buzzer::playPattern(const uint16_t* pattern, uint8_t count)
{
    if (!_initialized || !pattern || count == 0) {
        return;
    }

    for (uint8_t i = 0; i < count; i++) {
        if (pattern[i * 2] > 0) {
            startTone(pattern[i * 2]);
        }
        vTaskDelay(pdMS_TO_TICKS(pattern[i * 2 + 1]));
        if (pattern[i * 2] > 0) {
            stopTone();
        }
    }
}

/* ---- 振动马达驱动实现 ---- */
static const char* TAG_Vibrator = "Vibrator";

Vibrator::Vibrator()
    : _frontPin(-1), _rearPin(-1), _leftPin(-1), _rightPin(-1),
      _initialized(false), _intensity(100)
{
}

Vibrator::~Vibrator()
{
    stopAll();
    _initialized = false;
}

bool Vibrator::init(int frontPin, int rearPin, int leftPin, int rightPin)
{
    if (_initialized) {
        return false;
    }

    _frontPin = frontPin;
    _rearPin = rearPin;
    _leftPin = leftPin;
    _rightPin = rightPin;

    gpio_config_t ioConfig = {
        .pin_bit_mask = (1ULL << frontPin) | (1ULL << rearPin) |
                        (1ULL << leftPin) | (1ULL << rightPin),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    esp_err_t ret = gpio_config(&ioConfig);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG_Vibrator, "GPIO config failed: %s", esp_err_to_name(ret));
        return false;
    }

    stopAll();

    _initialized = true;
    ESP_LOGI(TAG_Vibrator, "Vibrator initialized: front=%d rear=%d left=%d right=%d",
             frontPin, rearPin, leftPin, rightPin);
    return true;
}

void Vibrator::vibrate(WarnDirection_t direction, uint32_t durationMs)
{
    if (!_initialized) {
        return;
    }

    switch (direction) {
        case WARN_DIR_FRONT:
            vibrateFront(durationMs);
            break;
        case WARN_DIR_FRONT_LEFT:
            setPin(_frontPin, true);
            setPin(_leftPin, true);
            vTaskDelay(pdMS_TO_TICKS(durationMs));
            setPin(_frontPin, false);
            setPin(_leftPin, false);
            break;
        case WARN_DIR_FRONT_RIGHT:
            setPin(_frontPin, true);
            setPin(_rightPin, true);
            vTaskDelay(pdMS_TO_TICKS(durationMs));
            setPin(_frontPin, false);
            setPin(_rightPin, false);
            break;
        case WARN_DIR_LEFT:
            vibrateLeft(durationMs);
            break;
        case WARN_DIR_RIGHT:
            vibrateRight(durationMs);
            break;
        case WARN_DIR_REAR:
            vibrateRear(durationMs);
            break;
        case WARN_DIR_ALL:
            vibrateAll(durationMs);
            break;
        default:
            break;
    }
}

void Vibrator::vibrateFront(uint32_t durationMs)
{
    if (!_initialized) return;
    setPin(_frontPin, true);
    vTaskDelay(pdMS_TO_TICKS(durationMs));
    setPin(_frontPin, false);
}

void Vibrator::vibrateRear(uint32_t durationMs)
{
    if (!_initialized) return;
    setPin(_rearPin, true);
    vTaskDelay(pdMS_TO_TICKS(durationMs));
    setPin(_rearPin, false);
}

void Vibrator::vibrateLeft(uint32_t durationMs)
{
    if (!_initialized) return;
    setPin(_leftPin, true);
    vTaskDelay(pdMS_TO_TICKS(durationMs));
    setPin(_leftPin, false);
}

void Vibrator::vibrateRight(uint32_t durationMs)
{
    if (!_initialized) return;
    setPin(_rightPin, true);
    vTaskDelay(pdMS_TO_TICKS(durationMs));
    setPin(_rightPin, false);
}

void Vibrator::vibrateAll(uint32_t durationMs)
{
    if (!_initialized) return;
    setPin(_frontPin, true);
    setPin(_rearPin, true);
    setPin(_leftPin, true);
    setPin(_rightPin, true);
    vTaskDelay(pdMS_TO_TICKS(durationMs));
    stopAll();
}

void Vibrator::stopAll(void)
{
    if (!_initialized) return;
    setPin(_frontPin, false);
    setPin(_rearPin, false);
    setPin(_leftPin, false);
    setPin(_rightPin, false);
}

void Vibrator::setIntensity(uint8_t intensity)
{
    _intensity = intensity;
    ESP_LOGD(TAG_Vibrator, "Intensity set to %u", intensity);
}

void Vibrator::setPin(int pin, bool state)
{
    if (pin < 0) return;
    gpio_set_level((gpio_num_t)pin, state ? 1 : 0);
}

/* ---- 状态LED驱动实现 ---- */
static const char* TAG_StatusLED = "StatusLED";

StatusLED::StatusLED() : _pin(-1), _initialized(false), _state(false)
{
}

StatusLED::~StatusLED()
{
    if (_initialized) {
        off();
        _initialized = false;
    }
}

bool StatusLED::init(int pin)
{
    if (_initialized) {
        return false;
    }

    _pin = pin;

    gpio_config_t ioConfig = {
        .pin_bit_mask = 1ULL << pin,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    esp_err_t ret = gpio_config(&ioConfig);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG_StatusLED, "GPIO config failed: %s", esp_err_to_name(ret));
        return false;
    }

    off();
    _initialized = true;
    ESP_LOGI(TAG_StatusLED, "StatusLED initialized on pin %d", pin);
    return true;
}

void StatusLED::on(void)
{
    if (!_initialized) return;
    gpio_set_level((gpio_num_t)_pin, 1);
    _state = true;
}

void StatusLED::off(void)
{
    if (!_initialized) return;
    gpio_set_level((gpio_num_t)_pin, 0);
    _state = false;
}

void StatusLED::toggle(void)
{
    if (!_initialized) return;
    if (_state) {
        off();
    } else {
        on();
    }
}

void StatusLED::blink(uint32_t onMs, uint32_t offMs, uint8_t count)
{
    if (!_initialized) return;

    for (uint8_t i = 0; i < count; i++) {
        on();
        vTaskDelay(pdMS_TO_TICKS(onMs));
        off();
        vTaskDelay(pdMS_TO_TICKS(offMs));
    }
}

void StatusLED::setPattern(uint8_t pattern)
{
    if (!_initialized) return;
    ESP_LOGD(TAG_StatusLED, "Set pattern: %u (TODO: implement)", pattern);
}

/* ---- 前雷达处理实现 ---- */
static const char* TAG_RadarFront = "RadarFront";

RadarFrontProcessor::RadarFrontProcessor()
{
    memset(&_filteredData, 0, sizeof(_filteredData));
    for (int i = 0; i < FRONT_RADAR_ZONES; i++) {
        _zoneDistances[i] = 999.0f;
        _zoneLevels[i] = WARN_LEVEL_SAFE;
        _levelStableCount[i] = 0;
        _pendingLevels[i] = WARN_LEVEL_SAFE;
    }
    _overallLevel = WARN_LEVEL_SAFE;
    _closestDir = WARN_DIR_FRONT;
    _closestDist = 999.0f;

    _thresholdAttention = WARN_DISTANCE_ATTENTION;
    _thresholdWarning = WARN_DISTANCE_WARNING;
    _thresholdDanger = WARN_DISTANCE_DANGER;
    _debounceFrames = WARN_DEBOUNCE_FRAMES;
}

RadarFrontProcessor::~RadarFrontProcessor()
{
}

void RadarFrontProcessor::init(void)
{
    ESP_LOGI(TAG_RadarFront, "Front radar processor initialized");
    ESP_LOGI(TAG_RadarFront, "  Attention: %.2fm, Warning: %.2fm, Danger: %.2fm",
             _thresholdAttention, _thresholdWarning, _thresholdDanger);
    ESP_LOGI(TAG_RadarFront, "  Debounce frames: %u", _debounceFrames);
}

void RadarFrontProcessor::update(const RadarData_t* rawData)
{
    if (!rawData) {
        return;
    }

    assignTargetsToZones(rawData);
    calculateDangerLevels();
    applyDebounce();
    updateOverallLevel();

    ESP_LOGD(TAG_RadarFront, "Front radar: level=%d, closest_dist=%.2fm, dir=%d",
             _overallLevel, _closestDist, _closestDir);
}

void RadarFrontProcessor::assignTargetsToZones(const RadarData_t* rawData)
{
    for (int i = 0; i < FRONT_RADAR_ZONES; i++) {
        _zoneDistances[i] = 999.0f;
    }

    memcpy(&_filteredData, rawData, sizeof(RadarData_t));

    for (uint8_t i = 0; i < rawData->targetCount && i < RADAR_MAX_TARGETS; i++) {
        float angle = rawData->targets[i].angle;
        float dist = rawData->targets[i].distance;

        FrontRadarZone_t zone;
        if (angle < -20.0f) {
            zone = ZONE_FRONT_LEFT;
        } else if (angle > 20.0f) {
            zone = ZONE_FRONT_RIGHT;
        } else {
            zone = ZONE_FRONT_CENTER;
        }

        if (dist < _zoneDistances[zone]) {
            _zoneDistances[zone] = dist;
        }
    }
}

void RadarFrontProcessor::calculateDangerLevels(void)
{
    for (int i = 0; i < FRONT_RADAR_ZONES; i++) {
        WarnLevel_t newLevel = distanceToLevel(_zoneDistances[i]);

        if (newLevel != _pendingLevels[i]) {
            _pendingLevels[i] = newLevel;
            _levelStableCount[i] = 0;
        } else {
            if (_levelStableCount[i] < 255) {
                _levelStableCount[i]++;
            }
        }
    }
}

void RadarFrontProcessor::applyDebounce(void)
{
    for (int i = 0; i < FRONT_RADAR_ZONES; i++) {
        if (_levelStableCount[i] >= _debounceFrames) {
            if (_zoneLevels[i] != _pendingLevels[i]) {
                _zoneLevels[i] = _pendingLevels[i];
                ESP_LOGD(TAG_RadarFront, "Zone %d level changed to %d", i, _zoneLevels[i]);
            }
        }
    }
}

void RadarFrontProcessor::updateOverallLevel(void)
{
    WarnLevel_t maxLevel = WARN_LEVEL_SAFE;
    int closestZone = ZONE_FRONT_CENTER;
    float closestDist = 999.0f;

    for (int i = 0; i < FRONT_RADAR_ZONES; i++) {
        if (_zoneLevels[i] > maxLevel) {
            maxLevel = _zoneLevels[i];
        }
        if (_zoneDistances[i] < closestDist) {
            closestDist = _zoneDistances[i];
            closestZone = i;
        }
    }

    _overallLevel = maxLevel;
    _closestDist = closestDist;

    switch (closestZone) {
        case ZONE_FRONT_LEFT:   _closestDir = WARN_DIR_FRONT_LEFT; break;
        case ZONE_FRONT_CENTER: _closestDir = WARN_DIR_FRONT; break;
        case ZONE_FRONT_RIGHT:  _closestDir = WARN_DIR_FRONT_RIGHT; break;
    }
}

float RadarFrontProcessor::getNearestDistance(FrontRadarZone_t zone) const
{
    if (zone < 0 || zone >= FRONT_RADAR_ZONES) {
        return 999.0f;
    }
    return _zoneDistances[zone];
}

WarnLevel_t RadarFrontProcessor::getDangerLevel(FrontRadarZone_t zone) const
{
    if (zone < 0 || zone >= FRONT_RADAR_ZONES) {
        return WARN_LEVEL_SAFE;
    }
    return _zoneLevels[zone];
}

WarnLevel_t RadarFrontProcessor::getOverallDangerLevel(void) const
{
    return _overallLevel;
}

WarnDirection_t RadarFrontProcessor::getClosestDirection(void) const
{
    return _closestDir;
}

float RadarFrontProcessor::getClosestDistance(void) const
{
    return _closestDist;
}

WarnLevel_t RadarFrontProcessor::distanceToLevel(float distance) const
{
    if (distance <= _thresholdDanger) {
        return WARN_LEVEL_DANGER;
    } else if (distance <= _thresholdWarning) {
        return WARN_LEVEL_WARNING;
    } else if (distance <= _thresholdAttention) {
        return WARN_LEVEL_ATTENTION;
    }
    return WARN_LEVEL_SAFE;
}

void RadarFrontProcessor::setDangerThresholds(float attention, float warning, float danger)
{
    _thresholdAttention = attention;
    _thresholdWarning = warning;
    _thresholdDanger = danger;
}

void RadarFrontProcessor::setDebounceFrames(uint8_t frames)
{
    _debounceFrames = frames;
}

/* ---- 后雷达处理实现 ---- */
static const char* TAG_RadarRear = "RadarRear";

RadarRearProcessor::RadarRearProcessor()
{
    memset(&_filteredData, 0, sizeof(_filteredData));
    _nearestDistance = 999.0f;
    _approachSpeed = 0.0f;
    _isApproachingFast = false;
    _dangerLevel = WARN_LEVEL_SAFE;

    _fastApproachSpeed = -1.5f;
    _thresholdAttention = WARN_DISTANCE_ATTENTION;
    _thresholdWarning = WARN_DISTANCE_WARNING;
    _thresholdDanger = WARN_DISTANCE_DANGER;

    _debounceFrames = WARN_DEBOUNCE_FRAMES;
    _fastApproachCount = 0;
    _pendingFastApproach = false;
}

RadarRearProcessor::~RadarRearProcessor()
{
}

void RadarRearProcessor::init(void)
{
    ESP_LOGI(TAG_RadarRear, "Rear radar processor initialized");
    ESP_LOGI(TAG_RadarRear, "  Fast approach speed threshold: %.2f m/s", _fastApproachSpeed);
}

void RadarRearProcessor::update(const RadarData_t* rawData)
{
    if (!rawData) {
        return;
    }

    findNearestTarget(rawData);
    calculateDangerLevel();
    applyDebounce();

    ESP_LOGD(TAG_RadarRear, "Rear radar: level=%d, nearest=%.2fm, speed=%.2fm/s, fast_approach=%d",
             _dangerLevel, _nearestDistance, _approachSpeed, _isApproachingFast);
}

void RadarRearProcessor::findNearestTarget(const RadarData_t* rawData)
{
    memcpy(&_filteredData, rawData, sizeof(RadarData_t));

    float minDist = 999.0f;
    float maxApproachSpeed = 0.0f;

    for (uint8_t i = 0; i < rawData->targetCount && i < RADAR_MAX_TARGETS; i++) {
        float dist = rawData->targets[i].distance;
        float speed = rawData->targets[i].speed;

        if (dist < minDist) {
            minDist = dist;
        }

        if (speed < maxApproachSpeed) {
            maxApproachSpeed = speed;
        }
    }

    _nearestDistance = minDist;
    _approachSpeed = maxApproachSpeed;

    bool nowApproachingFast = (maxApproachSpeed <= _fastApproachSpeed);
    if (nowApproachingFast != _pendingFastApproach) {
        _pendingFastApproach = nowApproachingFast;
        _fastApproachCount = 0;
    } else {
        if (_fastApproachCount < 255) {
            _fastApproachCount++;
        }
    }
}

void RadarRearProcessor::calculateDangerLevel(void)
{
    _dangerLevel = distanceToLevel(_nearestDistance);
}

void RadarRearProcessor::applyDebounce(void)
{
    if (_fastApproachCount >= _debounceFrames) {
        if (_isApproachingFast != _pendingFastApproach) {
            _isApproachingFast = _pendingFastApproach;
            ESP_LOGI(TAG_RadarRear, "Fast approach %s, speed=%.2f m/s",
                     _isApproachingFast ? "detected" : "cleared",
                     _approachSpeed);
        }
    }
}

WarnLevel_t RadarRearProcessor::distanceToLevel(float distance) const
{
    if (distance <= _thresholdDanger) {
        return WARN_LEVEL_DANGER;
    } else if (distance <= _thresholdWarning) {
        return WARN_LEVEL_WARNING;
    } else if (distance <= _thresholdAttention) {
        return WARN_LEVEL_ATTENTION;
    }
    return WARN_LEVEL_SAFE;
}

bool RadarRearProcessor::isApproachingFast(void) const
{
    return _isApproachingFast;
}

float RadarRearProcessor::getApproachSpeed(void) const
{
    return _approachSpeed;
}

float RadarRearProcessor::getNearestDistance(void) const
{
    return _nearestDistance;
}

WarnLevel_t RadarRearProcessor::getDangerLevel(void) const
{
    return _dangerLevel;
}

void RadarRearProcessor::setFastApproachSpeed(float speedMps)
{
    _fastApproachSpeed = speedMps;
}

void RadarRearProcessor::setDangerThresholds(float attention, float warning, float danger)
{
    _thresholdAttention = attention;
    _thresholdWarning = warning;
    _thresholdDanger = danger;
}

void RadarRearProcessor::setDebounceFrames(uint8_t frames)
{
    _debounceFrames = frames;
}

/* ---- 盲道检测实现 ---- */
static const char* TAG_LaneDetect = "LaneDetect";

LaneDetector::LaneDetector()
{
    memset(&_result, 0, sizeof(_result));
    _result.position = LANE_LOST;
    _colorMode = LANE_COLOR_YELLOW;
    _roiTopRatio = LANE_ROI_TOP_RATIO;
    _confidenceThreshold = LANE_DETECT_CONFIDENCE;
    _debounceFrames = WARN_DEBOUNCE_FRAMES;

    _hsvYellowLow[0] = 20;
    _hsvYellowLow[1] = 100;
    _hsvYellowLow[2] = 100;
    _hsvYellowHigh[0] = 40;
    _hsvYellowHigh[1] = 255;
    _hsvYellowHigh[2] = 255;

    _stableCount = 0;
    _pendingPosition = LANE_LOST;
    _pendingDetected = false;
}

LaneDetector::~LaneDetector()
{
}

void LaneDetector::init(void)
{
    ESP_LOGI(TAG_LaneDetect, "Lane detector initialized");
    ESP_LOGI(TAG_LaneDetect, "  Color mode: %d", _colorMode);
    ESP_LOGI(TAG_LaneDetect, "  ROI top ratio: %.2f", _roiTopRatio);
    ESP_LOGI(TAG_LaneDetect, "  Confidence threshold: %.2f", _confidenceThreshold);
}

void LaneDetector::detect(uint8_t* image, int width, int height)
{
    if (!image || width <= 0 || height <= 0) {
        _result.detected = false;
        _result.position = LANE_LOST;
        _result.confidence = 0.0f;
        return;
    }

    LanePosition_t newPosition = LANE_LOST;
    float newConfidence = 0.0f;
    float newOffset = 0.0f;
    bool detected = false;

    ESP_LOGD(TAG_LaneDetect, "Detecting lane: %dx%d (TODO: implement actual algorithm)",
             width, height);

    if (_colorMode == LANE_COLOR_YELLOW) {
        ESP_LOGD(TAG_LaneDetect, "  Mode: Yellow lane detection");
    } else if (_colorMode == LANE_COLOR_GRAY) {
        ESP_LOGD(TAG_LaneDetect, "  Mode: Gray lane detection");
    }

    newPosition = LANE_CENTER;
    newConfidence = 0.7f;
    newOffset = 0.0f;
    detected = true;

    _pendingDetected = detected;
    _pendingPosition = newPosition;

    if (detected) {
        _result.offsetRatio = newOffset;
    }

    applyDebounce();

    ESP_LOGD(TAG_LaneDetect, "Lane result: detected=%d, position=%d, conf=%.2f",
             _result.detected, _result.position, _result.confidence);
}

void LaneDetector::applyDebounce(void)
{
    bool positionChanged = (_pendingPosition != _result.position);
    bool detectedChanged = (_pendingDetected != _result.detected);

    if (positionChanged || detectedChanged) {
        _stableCount = 0;
    } else {
        if (_stableCount < 255) {
            _stableCount++;
        }
    }

    if (_stableCount >= _debounceFrames) {
        if (_result.position != _pendingPosition ||
            _result.detected != _pendingDetected) {
            _result.position = _pendingPosition;
            _result.detected = _pendingDetected;
            _result.timestamp = (uint32_t)(esp_log_timestamp() / 1000);
            ESP_LOGI(TAG_LaneDetect, "Lane state changed: detected=%d, position=%d",
                     _result.detected, _result.position);
        }
    }
}

void LaneDetector::setColorMode(LaneColorMode_t mode)
{
    _colorMode = mode;
    ESP_LOGI(TAG_LaneDetect, "Color mode set to %d", mode);
}

void LaneDetector::setRoiTopRatio(float ratio)
{
    if (ratio >= 0.0f && ratio < 1.0f) {
        _roiTopRatio = ratio;
    }
}

void LaneDetector::setConfidenceThreshold(float threshold)
{
    _confidenceThreshold = threshold;
}

void LaneDetector::setDebounceFrames(uint8_t frames)
{
    _debounceFrames = frames;
}

void LaneDetector::setHsvYellowLow(uint8_t h, uint8_t s, uint8_t v)
{
    _hsvYellowLow[0] = h;
    _hsvYellowLow[1] = s;
    _hsvYellowLow[2] = v;
}

void LaneDetector::setHsvYellowHigh(uint8_t h, uint8_t s, uint8_t v)
{
    _hsvYellowHigh[0] = h;
    _hsvYellowHigh[1] = s;
    _hsvYellowHigh[2] = v;
}

void LaneDetector::extractRoi(uint8_t* image, int width, int height,
                               uint8_t** roiOut, int* roiW, int* roiH)
{
    if (!image || !roiOut || !roiW || !roiH) return;

    int roiTop = (int)(height * _roiTopRatio);
    int roiHeight = height - roiTop;

    *roiOut = image + roiTop * width * 2;
    *roiW = width;
    *roiH = roiHeight;
}

bool LaneDetector::convertToHsv(uint8_t* rgbImage, uint8_t* hsvImage, int w, int h)
{
    if (!rgbImage || !hsvImage || w <= 0 || h <= 0) return false;
    return false;
}

int LaneDetector::colorThreshold(uint8_t* hsvImage, uint8_t* binary, int w, int h)
{
    if (!hsvImage || !binary || w <= 0 || h <= 0) return 0;
    return 0;
}

void LaneDetector::edgeDetect(uint8_t* gray, uint8_t* edges, int w, int h)
{
    if (!gray || !edges || w <= 0 || h <= 0) return;
}

int LaneDetector::houghLines(uint8_t* edges, int w, int h, float* lines, int maxLines)
{
    if (!edges || !lines || w <= 0 || h <= 0) return 0;
    return 0;
}

LanePosition_t LaneDetector::calculatePosition(float* lines, int lineCount, int width)
{
    return LANE_LOST;
}

float LaneDetector::calculateConfidence(int lineCount, int validPairs)
{
    return 0.0f;
}

/* ---- 红绿灯检测实现 ---- */
static const char* TAG_TrafficLight = "TrafficLight";

TrafficLightDetector::TrafficLightDetector()
{
    memset(&_result, 0, sizeof(_result));
    _result.state = LIGHT_NONE;

    _roiTopRatio = TL_ROI_TOP_RATIO;
    _roiBottomRatio = TL_ROI_BOTTOM_RATIO;
    _confidenceThreshold = TL_DETECT_CONFIDENCE;
    _debounceFrames = WARN_DEBOUNCE_FRAMES;

    _redHlow1 = 0;   _redHhigh1 = 10;
    _redHlow2 = 160; _redHhigh2 = 180;
    _redSlow = 100;  _redVlow = 100;

    _yellowHlow = 20;  _yellowHhigh = 40;
    _yellowSlow = 100; _yellowVlow = 100;

    _greenHlow = 60;  _greenHhigh = 90;
    _greenSlow = 100; _greenVlow = 100;

    _minLightArea = 50;
    _circularityThreshold = 0.6f;

    _stableCount = 0;
    _pendingState = LIGHT_NONE;
    _pendingDetected = false;
}

TrafficLightDetector::~TrafficLightDetector()
{
}

void TrafficLightDetector::init(void)
{
    ESP_LOGI(TAG_TrafficLight, "Traffic light detector initialized");
    ESP_LOGI(TAG_TrafficLight, "  ROI: top=%.2f, bottom=%.2f", _roiTopRatio, _roiBottomRatio);
    ESP_LOGI(TAG_TrafficLight, "  Confidence threshold: %.2f", _confidenceThreshold);
    ESP_LOGI(TAG_TrafficLight, "  Min light area: %d", _minLightArea);
}

void TrafficLightDetector::detect(uint8_t* image, int width, int height)
{
    if (!image || width <= 0 || height <= 0) {
        _result.detected = false;
        _result.state = LIGHT_NONE;
        _result.confidence = 0.0f;
        return;
    }

    TrafficLightState_t newState = LIGHT_NONE;
    float newConfidence = 0.0f;
    bool detected = false;

    ESP_LOGD(TAG_TrafficLight, "Detecting traffic light: %dx%d (TODO: implement actual algorithm)",
             width, height);

    newState = LIGHT_RED;
    newConfidence = 0.6f;
    detected = true;

    _pendingDetected = detected;
    _pendingState = newState;

    applyDebounce();

    ESP_LOGD(TAG_TrafficLight, "Traffic light result: detected=%d, state=%d, conf=%.2f",
             _result.detected, _result.state, _result.confidence);
}

void TrafficLightDetector::applyDebounce(void)
{
    bool stateChanged = (_pendingState != _result.state);
    bool detectedChanged = (_pendingDetected != _result.detected);

    if (stateChanged || detectedChanged) {
        _stableCount = 0;
    } else {
        if (_stableCount < 255) {
            _stableCount++;
        }
    }

    if (_stableCount >= _debounceFrames) {
        if (_result.state != _pendingState ||
            _result.detected != _pendingDetected) {
            _result.state = _pendingState;
            _result.detected = _pendingDetected;
            _result.timestamp = (uint32_t)(esp_log_timestamp() / 1000);
            ESP_LOGI(TAG_TrafficLight, "Traffic light state changed: detected=%d, state=%d",
                     _result.detected, _result.state);
        }
    }
}

void TrafficLightDetector::setRoiRange(float topRatio, float bottomRatio)
{
    if (topRatio >= 0.0f && bottomRatio <= 1.0f && topRatio < bottomRatio) {
        _roiTopRatio = topRatio;
        _roiBottomRatio = bottomRatio;
    }
}

void TrafficLightDetector::setConfidenceThreshold(float threshold)
{
    _confidenceThreshold = threshold;
}

void TrafficLightDetector::setDebounceFrames(uint8_t frames)
{
    _debounceFrames = frames;
}

void TrafficLightDetector::setRedHsvRange(uint8_t hLow1, uint8_t hHigh1,
                                          uint8_t hLow2, uint8_t hHigh2,
                                          uint8_t sLow, uint8_t vLow)
{
    _redHlow1 = hLow1; _redHhigh1 = hHigh1;
    _redHlow2 = hLow2; _redHhigh2 = hHigh2;
    _redSlow = sLow;   _redVlow = vLow;
}

void TrafficLightDetector::setYellowHsvRange(uint8_t hLow, uint8_t hHigh,
                                             uint8_t sLow, uint8_t vLow)
{
    _yellowHlow = hLow; _yellowHhigh = hHigh;
    _yellowSlow = sLow; _yellowVlow = vLow;
}

void TrafficLightDetector::setGreenHsvRange(uint8_t hLow, uint8_t hHigh,
                                            uint8_t sLow, uint8_t vLow)
{
    _greenHlow = hLow; _greenHhigh = hHigh;
    _greenSlow = sLow; _greenVlow = vLow;
}

void TrafficLightDetector::setMinLightArea(int minArea)
{
    _minLightArea = minArea;
}

void TrafficLightDetector::setCircularityThreshold(float threshold)
{
    _circularityThreshold = threshold;
}

void TrafficLightDetector::extractRoi(uint8_t* image, int width, int height,
                                       uint8_t** roiOut, int* roiW, int* roiH,
                                       int* roiX, int* roiY)
{
    if (!image || !roiOut || !roiW || !roiH || !roiX || !roiY) return;

    int roiTop = (int)(height * _roiTopRatio);
    int roiBottom = (int)(height * _roiBottomRatio);
    int roiHeight = roiBottom - roiTop;

    *roiOut = image + roiTop * width * 2;
    *roiW = width;
    *roiH = roiHeight;
    *roiX = 0;
    *roiY = roiTop;
}

bool TrafficLightDetector::convertToHsv(uint8_t* rgbImage, uint8_t* hsvImage, int w, int h)
{
    if (!rgbImage || !hsvImage || w <= 0 || h <= 0) return false;
    return false;
}

int TrafficLightDetector::colorSegmentation(uint8_t* hsvImage, uint8_t* mask, int w, int h,
                                             uint8_t hLow, uint8_t hHigh,
                                             uint8_t sLow, uint8_t vLow)
{
    if (!hsvImage || !mask || w <= 0 || h <= 0) return 0;
    return 0;
}

void TrafficLightDetector::morphologicalOps(uint8_t* binary, int w, int h)
{
    if (!binary || w <= 0 || h <= 0) return;
}

int TrafficLightDetector::findLightContours(uint8_t* binary, int w, int h,
                                             int* centersX, int* centersY,
                                             int* areas, int maxLights)
{
    if (!binary || !centersX || !centersY || !areas) return 0;
    return 0;
}

float TrafficLightDetector::calculateCircularity(int area, float perimeter)
{
    if (perimeter <= 0) return 0.0f;
    return (4.0f * 3.14159f * area) / (perimeter * perimeter);
}

TrafficLightState_t TrafficLightDetector::determineState(
    int redCount, int yellowCount, int greenCount,
    float redConf, float yellowConf, float greenConf)
{
    return LIGHT_NONE;
}

/* ---- 斑马线检测实现 ---- */
static const char* TAG_Crosswalk = "Crosswalk";

CrosswalkDetector::CrosswalkDetector()
{
    memset(&_result, 0, sizeof(_result));

    _roiTopRatio = CW_ROI_TOP_RATIO;
    _roiBottomRatio = CW_ROI_BOTTOM_RATIO;
    _minStripes = CW_MIN_STRIPES;
    _confidenceThreshold = CW_DETECT_CONFIDENCE;
    _debounceFrames = WARN_DEBOUNCE_FRAMES;

    _minStripeWidth = 10;
    _maxStripeWidth = 100;
    _stripeSpacingTolerance = 0.3f;

    _stableCount = 0;
    _pendingDetected = false;
    _pendingStripeCount = 0;
}

CrosswalkDetector::~CrosswalkDetector()
{
}

void CrosswalkDetector::init(void)
{
    ESP_LOGI(TAG_Crosswalk, "Crosswalk detector initialized");
    ESP_LOGI(TAG_Crosswalk, "  ROI: top=%.2f, bottom=%.2f", _roiTopRatio, _roiBottomRatio);
    ESP_LOGI(TAG_Crosswalk, "  Min stripes: %u", _minStripes);
    ESP_LOGI(TAG_Crosswalk, "  Confidence threshold: %.2f", _confidenceThreshold);
}

void CrosswalkDetector::detect(uint8_t* image, int width, int height)
{
    if (!image || width <= 0 || height <= 0) {
        _result.detected = false;
        _result.stripeCount = 0;
        _result.confidence = 0.0f;
        return;
    }

    bool detected = false;
    uint8_t stripeCount = 0;
    float confidence = 0.0f;

    ESP_LOGD(TAG_Crosswalk, "Detecting crosswalk: %dx%d (TODO: implement actual algorithm)",
             width, height);

    detected = false;
    stripeCount = 0;
    confidence = 0.0f;

    _pendingDetected = detected;
    _pendingStripeCount = stripeCount;

    applyDebounce();

    ESP_LOGD(TAG_Crosswalk, "Crosswalk result: detected=%d, stripes=%u, conf=%.2f",
             _result.detected, _result.stripeCount, _result.confidence);
}

void CrosswalkDetector::applyDebounce(void)
{
    bool detectedChanged = (_pendingDetected != _result.detected);

    if (detectedChanged) {
        _stableCount = 0;
    } else {
        if (_stableCount < 255) {
            _stableCount++;
        }
    }

    if (_stableCount >= _debounceFrames) {
        if (_result.detected != _pendingDetected) {
            _result.detected = _pendingDetected;
            _result.stripeCount = _pendingStripeCount;
            _result.timestamp = (uint32_t)(esp_log_timestamp() / 1000);
            ESP_LOGI(TAG_Crosswalk, "Crosswalk state changed: detected=%d, stripes=%u",
                     _result.detected, _result.stripeCount);
        }
    }
}

void CrosswalkDetector::setRoiRange(float topRatio, float bottomRatio)
{
    if (topRatio >= 0.0f && bottomRatio <= 1.0f && topRatio < bottomRatio) {
        _roiTopRatio = topRatio;
        _roiBottomRatio = bottomRatio;
    }
}

void CrosswalkDetector::setMinStripes(uint8_t count)
{
    _minStripes = count;
}

void CrosswalkDetector::setConfidenceThreshold(float threshold)
{
    _confidenceThreshold = threshold;
}

void CrosswalkDetector::setDebounceFrames(uint8_t frames)
{
    _debounceFrames = frames;
}

void CrosswalkDetector::setStripeWidthRange(int minWidth, int maxWidth)
{
    _minStripeWidth = minWidth;
    _maxStripeWidth = maxWidth;
}

void CrosswalkDetector::setStripeSpacingTolerance(float tolerance)
{
    _stripeSpacingTolerance = tolerance;
}

void CrosswalkDetector::extractRoi(uint8_t* image, int width, int height,
                                    uint8_t** roiOut, int* roiW, int* roiH)
{
    if (!image || !roiOut || !roiW || !roiH) return;

    int roiTop = (int)(height * _roiTopRatio);
    int roiBottom = (int)(height * _roiBottomRatio);
    int roiHeight = roiBottom - roiTop;

    *roiOut = image + roiTop * width * 2;
    *roiW = width;
    *roiH = roiHeight;
}

void CrosswalkDetector::convertToGray(uint8_t* rgbImage, uint8_t* gray, int w, int h)
{
    if (!rgbImage || !gray || w <= 0 || h <= 0) return;
}

void CrosswalkDetector::edgeDetect(uint8_t* gray, uint8_t* edges, int w, int h)
{
    if (!gray || !edges || w <= 0 || h <= 0) return;
}

int CrosswalkDetector::houghHorizontalLines(uint8_t* edges, int w, int h,
                                              float* lines, int maxLines)
{
    if (!edges || !lines || w <= 0 || h <= 0) return 0;
    return 0;
}

int CrosswalkDetector::findParallelStripes(float* lines, int lineCount, int width,
                                            int* stripePositions, int* stripeWidths,
                                            int maxStripes)
{
    if (!lines || !stripePositions || !stripeWidths) return 0;
    return 0;
}

int CrosswalkDetector::countEquallySpacedStripes(int* positions, int count,
                                                  float tolerance)
{
    if (!positions || count < 2) return 0;
    return 0;
}

float CrosswalkDetector::calculateConfidence(int stripeCount, int validCount)
{
    return 0.0f;
}

/* ---- SPI 从机通信实现 ---- */
static const char* TAG_SpiSlave = "SpiSlave";

SpiSlave::SpiSlave()
{
    _mosiPin = -1;
    _misoPin = -1;
    _sclkPin = -1;
    _csPin = -1;
    _initialized = false;
    _running = false;

    _imageBuffer = NULL;
    _imageWidth = 0;
    _imageHeight = 0;
    _imageFormat = 0;
    _imageReady = false;

    _imgTotalSize = 0;
    _imgTotalBlocks = 0;
    _imgReceivedBlocks = 0;

    _frameQueue = NULL;
    _imageCallback = NULL;
    _imageCallbackArg = NULL;
}

SpiSlave::~SpiSlave()
{
    stop();
    if (_imageBuffer) {
        free(_imageBuffer);
        _imageBuffer = NULL;
    }
    if (_frameQueue) {
        vQueueDelete(_frameQueue);
        _frameQueue = NULL;
    }
    _initialized = false;
}

bool SpiSlave::init(int mosiPin, int misoPin, int sclkPin, int csPin)
{
    if (_initialized) {
        return false;
    }

    _mosiPin = mosiPin;
    _misoPin = misoPin;
    _sclkPin = sclkPin;
    _csPin = csPin;

    _imageBuffer = (uint8_t*)heap_caps_malloc(SPI_SLAVE_MAX_IMG_BUF_SIZE, MALLOC_CAP_SPIRAM);
    if (!_imageBuffer) {
        ESP_LOGE(TAG_SpiSlave, "Failed to allocate image buffer");
        return false;
    }
    memset(_imageBuffer, 0, SPI_SLAVE_MAX_IMG_BUF_SIZE);

    _frameQueue = xQueueCreate(5, sizeof(SpiFrame_t));
    if (!_frameQueue) {
        ESP_LOGE(TAG_SpiSlave, "Failed to create frame queue");
        return false;
    }

    spi_bus_config_t buscfg = {
        .mosi_io_num = (gpio_num_t)mosiPin,
        .miso_io_num = (gpio_num_t)misoPin,
        .sclk_io_num = (gpio_num_t)sclkPin,
        .quadwp_io_num = GPIO_NUM_NC,
        .quadhd_io_num = GPIO_NUM_NC,
        .data4_io_num = GPIO_NUM_NC,
        .data5_io_num = GPIO_NUM_NC,
        .data6_io_num = GPIO_NUM_NC,
        .data7_io_num = GPIO_NUM_NC,
        .max_transfer_sz = 4096,
        .flags = 0,
        .isr_cpu_id = INTR_CPU_ID_1,
        .intr_flags = 0,
    };

    spi_slave_interface_config_t slvcfg = {
        .spics_io_num = (gpio_num_t)csPin,
        .flags = 0,
        .queue_size = 4,
        .mode = SPI_MODE0,
        .post_setup_cb = NULL,
        .post_trans_cb = NULL,
    };

    esp_err_t ret = spi_slave_initialize(SPI2_HOST, &buscfg, &slvcfg, SPI_DMA_CH_AUTO);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG_SpiSlave, "SPI slave init failed: %s", esp_err_to_name(ret));
        return false;
    }

    _initialized = true;
    ESP_LOGI(TAG_SpiSlave, "SPI slave initialized: MOSI=%d MISO=%d SCLK=%d CS=%d",
             mosiPin, misoPin, sclkPin, csPin);
    return true;
}

void SpiSlave::start(void)
{
    if (!_initialized || _running) {
        return;
    }

    _running = true;

    xTaskCreatePinnedToCore(
        spiTaskFunc,
        "spi_slave_task",
        TASK_STACK_SPI_RECV,
        this,
        TASK_PRIORITY_SPI_RECV,
        NULL,
        1);

    ESP_LOGI(TAG_SpiSlave, "SPI slave started");
}

void SpiSlave::stop(void)
{
    _running = false;
    ESP_LOGI(TAG_SpiSlave, "SPI slave stopped");
}

bool SpiSlave::sendCommand(uint8_t cmd, const uint8_t* data, uint16_t len)
{
    if (!_initialized || !_running) {
        return false;
    }

    ESP_LOGD(TAG_SpiSlave, "Send command: 0x%02X, len=%u (TODO: implement)", cmd, len);
    return true;
}

bool SpiSlave::waitForImage(uint32_t timeoutMs)
{
    if (!_initialized) {
        return false;
    }

    uint32_t start = xTaskGetTickCount();
    while (!_imageReady) {
        if ((xTaskGetTickCount() - start) > pdMS_TO_TICKS(timeoutMs)) {
            return false;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    return true;
}

void SpiSlave::registerImageCallback(void (*callback)(void* arg), void* arg)
{
    _imageCallback = callback;
    _imageCallbackArg = arg;
}

bool SpiSlave::parseFrame(const uint8_t* data, size_t len)
{
    if (!data || len < SPI_FRAME_OVERHEAD) {
        return false;
    }

    if (data[0] != 0xAA || data[1] != 0x55) {
        return false;
    }

    uint8_t cmd = data[2];
    uint16_t dataLen = (data[3] << 8) | data[4];

    if (dataLen > SPI_MAX_DATA_LEN) {
        ESP_LOGW(TAG_SpiSlave, "Data length too large: %u", dataLen);
        return false;
    }

    if (len < SPI_FRAME_HEADER_LEN + SPI_FRAME_CMD_LEN + SPI_FRAME_LEN_LEN + dataLen + SPI_FRAME_CRC_LEN + SPI_FRAME_TAIL_LEN) {
        return false;
    }

    uint8_t crc = data[5 + dataLen];
    uint8_t calcCrc = crc8(data + 2, 1 + 2 + dataLen);
    if (crc != calcCrc) {
        ESP_LOGW(TAG_SpiSlave, "CRC mismatch: 0x%02X vs 0x%02X", crc, calcCrc);
        return false;
    }

    const uint8_t* payload = data + 5;

    switch (cmd) {
        case SPI_CMD_IMG_FRAME_START:
            return handleFrameStart(payload, dataLen);
        case SPI_CMD_IMG_FRAME_DATA:
            return handleFrameData(payload, dataLen);
        case SPI_CMD_IMG_FRAME_END:
            return handleFrameEnd(payload, dataLen);
        case SPI_CMD_ACK:
            return handleAck(payload, dataLen);
        case SPI_CMD_HEARTBEAT:
            return handleHeartbeat(payload, dataLen);
        default:
            ESP_LOGW(TAG_SpiSlave, "Unknown command: 0x%02X", cmd);
            return false;
    }
}

bool SpiSlave::handleFrameStart(const uint8_t* data, uint16_t len)
{
    if (len < 9) return false;

    _imageWidth = (data[0] << 8) | data[1];
    _imageHeight = (data[2] << 8) | data[3];
    _imageFormat = data[4];
    _imgTotalSize = (data[5] << 24) | (data[6] << 16) | (data[7] << 8) | data[8];
    _imgReceivedBlocks = 0;
    _imageReady = false;

    ESP_LOGI(TAG_SpiSlave, "Image frame start: %dx%d, fmt=%d, size=%lu",
             _imageWidth, _imageHeight, _imageFormat, (unsigned long)_imgTotalSize);

    return true;
}

bool SpiSlave::handleFrameData(const uint8_t* data, uint16_t len)
{
    if (len < 4) return false;

    uint16_t blockIndex = (data[0] << 8) | data[1];
    uint16_t blockSize = len - 2;

    uint32_t offset = blockIndex * SPI_SEND_BLOCK_SIZE;
    if (offset + blockSize > SPI_SLAVE_MAX_IMG_BUF_SIZE) {
        ESP_LOGW(TAG_SpiSlave, "Block %u exceeds buffer", blockIndex);
        return false;
    }

    memcpy(_imageBuffer + offset, data + 2, blockSize);
    _imgReceivedBlocks++;

    ESP_LOGD(TAG_SpiSlave, "Received block %u, size=%u", blockIndex, blockSize);
    return true;
}

bool SpiSlave::handleFrameEnd(const uint8_t* data, uint16_t len)
{
    if (len < 2) return false;

    _imgTotalBlocks = (data[0] << 8) | data[1];

    ESP_LOGI(TAG_SpiSlave, "Image frame end: total blocks=%u, received=%u",
             _imgTotalBlocks, _imgReceivedBlocks);

    if (_imgReceivedBlocks == _imgTotalBlocks) {
        _imageReady = true;
        if (_imageCallback) {
            _imageCallback(_imageCallbackArg);
        }
        return true;
    } else {
        ESP_LOGW(TAG_SpiSlave, "Block mismatch: expected %u, got %u",
                 _imgTotalBlocks, _imgReceivedBlocks);
        return false;
    }
}

bool SpiSlave::handleCommand(const uint8_t* data, uint16_t len)
{
    ESP_LOGD(TAG_SpiSlave, "Received command, len=%u", len);
    return true;
}

bool SpiSlave::handleAck(const uint8_t* data, uint16_t len)
{
    if (len < 2) return false;
    ESP_LOGD(TAG_SpiSlave, "ACK: cmd=0x%02X, status=%u", data[0], data[1]);
    return true;
}

bool SpiSlave::handleHeartbeat(const uint8_t* data, uint16_t len)
{
    if (len < 4) return false;
    uint32_t timestamp = (data[0] << 24) | (data[1] << 16) | (data[2] << 8) | data[3];
    ESP_LOGD(TAG_SpiSlave, "Heartbeat: timestamp=%lu", (unsigned long)timestamp);
    return true;
}

bool SpiSlave::sendAck(uint8_t originalCmd, uint8_t status)
{
    uint8_t ackData[2] = {originalCmd, status};
    ESP_LOGD(TAG_SpiSlave, "Send ACK: cmd=0x%02X, status=%u (TODO)", originalCmd, status);
    return true;
}

bool SpiSlave::buildFrame(uint8_t cmd, const uint8_t* data, uint16_t len,
                           uint8_t* outBuf, uint16_t* outLen)
{
    if (!outBuf || !outLen) return false;
    if (len > SPI_MAX_DATA_LEN) return false;

    uint16_t totalLen = SPI_FRAME_OVERHEAD + len;
    *outLen = totalLen;

    outBuf[0] = 0xAA;
    outBuf[1] = 0x55;
    outBuf[2] = cmd;
    outBuf[3] = (len >> 8) & 0xFF;
    outBuf[4] = len & 0xFF;

    if (data && len > 0) {
        memcpy(outBuf + 5, data, len);
    }

    outBuf[5 + len] = crc8(outBuf + 2, 1 + 2 + len);
    outBuf[6 + len] = 0x55;
    outBuf[7 + len] = 0xAA;

    return true;
}

void SpiSlave::spiTaskFunc(void* arg)
{
    SpiSlave* self = (SpiSlave*)arg;
    self->spiTask();
}

void SpiSlave::spiTask(void)
{
    ESP_LOGI(TAG_SpiSlave, "SPI slave task started");

    WORD_ALIGNED_ATTR uint8_t recvBuf[4096];
    WORD_ALIGNED_ATTR uint8_t sendBuf[4096];

    while (_running) {
        spi_slave_transaction_t t;
        memset(&t, 0, sizeof(t));
        t.length = 4096 * 8;
        t.tx_buffer = sendBuf;
        t.rx_buffer = recvBuf;

        esp_err_t ret = spi_slave_transmit(SPI2_HOST, &t, portMAX_DELAY);
        if (ret == ESP_OK) {
            int recvLen = t.trans_len / 8;
            if (recvLen > 0) {
                parseFrame(recvBuf, recvLen);
            }
        } else {
            ESP_LOGW(TAG_SpiSlave, "SPI transaction failed: %s", esp_err_to_name(ret));
        }
    }

    ESP_LOGI(TAG_SpiSlave, "SPI slave task ended");
    vTaskDelete(NULL);
}

/* ---- 预警服务实现 ---- */
static const char* TAG_WarningSvc = "WarningSvc";

WarningService::WarningService()
{
    _warningQueue = NULL;
    _currentLevel = WARN_LEVEL_SAFE;
    _topSource = WARN_SOURCE_RADAR_FRONT;
    _currentDirection = WARN_DIR_FRONT;
    _debounceFrames = WARN_DEBOUNCE_FRAMES;

    _pendingLevel = WARN_LEVEL_SAFE;
    _pendingSource = WARN_SOURCE_RADAR_FRONT;
    _pendingDirection = WARN_DIR_FRONT;
    _stableCount = 0;
}

WarningService::~WarningService()
{
    if (_warningQueue) {
        vQueueDelete(_warningQueue);
        _warningQueue = NULL;
    }
}

bool WarningService::init(void)
{
    _warningQueue = xQueueCreate(WARNING_QUEUE_SIZE, sizeof(Warning_t));
    if (!_warningQueue) {
        ESP_LOGE(TAG_WarningSvc, "Failed to create warning queue");
        return false;
    }

    ESP_LOGI(TAG_WarningSvc, "Warning service initialized");
    ESP_LOGI(TAG_WarningSvc, "  Debounce frames: %u", _debounceFrames);
    return true;
}

void WarningService::update(const RadarData_t* frontRadar, const RadarData_t* rearRadar,
                             const LaneResult_t* lane, const TrafficLightResult_t* trafficLight,
                             const CrosswalkResult_t* crosswalk)
{
    WarnLevel_t frontLevel = evaluateFrontRadar(frontRadar);
    WarnLevel_t rearLevel = evaluateRearRadar(rearRadar);
    WarnLevel_t laneLevel = evaluateLane(lane);
    WarnLevel_t tlLevel = evaluateTrafficLight(trafficLight);
    WarnLevel_t cwLevel = evaluateCrosswalk(crosswalk);

    WarnLevel_t highestLevel = WARN_LEVEL_SAFE;
    WarnSource_t topSource = WARN_SOURCE_RADAR_FRONT;
    WarnDirection_t topDirection = WARN_DIR_FRONT;

    if (frontLevel > highestLevel) {
        highestLevel = frontLevel;
        topSource = WARN_SOURCE_RADAR_FRONT;
        topDirection = WARN_DIR_FRONT;
    }
    if (rearLevel > highestLevel) {
        highestLevel = rearLevel;
        topSource = WARN_SOURCE_RADAR_REAR;
        topDirection = WARN_DIR_REAR;
    }
    if (laneLevel > highestLevel) {
        highestLevel = laneLevel;
        topSource = WARN_SOURCE_LANE_DEPARTURE;
        topDirection = WARN_DIR_FRONT;
    }
    if (tlLevel > highestLevel) {
        highestLevel = tlLevel;
        topSource = WARN_SOURCE_TRAFFIC_LIGHT;
        topDirection = WARN_DIR_FRONT;
    }
    if (cwLevel > highestLevel) {
        highestLevel = cwLevel;
        topSource = WARN_SOURCE_CROSSWALK;
        topDirection = WARN_DIR_FRONT;
    }

    applyDebounce(highestLevel, topSource, topDirection);

    if (_currentLevel > WARN_LEVEL_SAFE) {
        Warning_t warn;
        warn.source = _topSource;
        warn.level = _currentLevel;
        warn.direction = _currentDirection;
        warn.distance = 0.0f;
        warn.timestamp = (uint32_t)(esp_log_timestamp() / 1000);
        snprintf(warn.message, sizeof(warn.message),
                 "Warning: source=%d level=%d dir=%d",
                 _topSource, _currentLevel, _currentDirection);

        xQueueSend(_warningQueue, &warn, 0);
    }

    ESP_LOGD(TAG_WarningSvc, "Levels: front=%d rear=%d lane=%d tl=%d cw=%d, top=%d/%d",
             frontLevel, rearLevel, laneLevel, tlLevel, cwLevel,
             _topSource, _currentLevel);
}

WarnLevel_t WarningService::evaluateFrontRadar(const RadarData_t* radar)
{
    if (!radar || radar->targetCount == 0) {
        return WARN_LEVEL_SAFE;
    }

    float minDist = 999.0f;
    for (uint8_t i = 0; i < radar->targetCount; i++) {
        if (radar->targets[i].distance < minDist) {
            minDist = radar->targets[i].distance;
        }
    }

    if (minDist <= WARN_DISTANCE_DANGER) {
        return WARN_LEVEL_DANGER;
    } else if (minDist <= WARN_DISTANCE_WARNING) {
        return WARN_LEVEL_WARNING;
    } else if (minDist <= WARN_DISTANCE_ATTENTION) {
        return WARN_LEVEL_ATTENTION;
    }

    return WARN_LEVEL_SAFE;
}

WarnLevel_t WarningService::evaluateRearRadar(const RadarData_t* radar)
{
    if (!radar || radar->targetCount == 0) {
        return WARN_LEVEL_SAFE;
    }

    float minDist = 999.0f;
    float maxApproach = 0.0f;

    for (uint8_t i = 0; i < radar->targetCount; i++) {
        if (radar->targets[i].distance < minDist) {
            minDist = radar->targets[i].distance;
        }
        if (radar->targets[i].speed < maxApproach) {
            maxApproach = radar->targets[i].speed;
        }
    }

    if (maxApproach <= -2.0f && minDist < 5.0f) {
        return WARN_LEVEL_DANGER;
    } else if (maxApproach <= -1.5f && minDist < 5.0f) {
        return WARN_LEVEL_WARNING;
    } else if (minDist <= WARN_DISTANCE_ATTENTION) {
        return WARN_LEVEL_ATTENTION;
    }

    return WARN_LEVEL_SAFE;
}

WarnLevel_t WarningService::evaluateLane(const LaneResult_t* lane)
{
    if (!lane || !lane->detected) {
        return WARN_LEVEL_WARNING;
    }

    if (lane->position == LANE_LOST) {
        return WARN_LEVEL_WARNING;
    }

    if (lane->position == LANE_LEFT || lane->position == LANE_RIGHT) {
        if (fabsf(lane->offsetRatio) > 0.7f) {
            return WARN_LEVEL_WARNING;
        } else if (fabsf(lane->offsetRatio) > 0.4f) {
            return WARN_LEVEL_ATTENTION;
        }
    }

    return WARN_LEVEL_SAFE;
}

WarnLevel_t WarningService::evaluateTrafficLight(const TrafficLightResult_t* tl)
{
    if (!tl || !tl->detected) {
        return WARN_LEVEL_SAFE;
    }

    if (tl->state == LIGHT_RED) {
        return WARN_LEVEL_WARNING;
    } else if (tl->state == LIGHT_YELLOW) {
        return WARN_LEVEL_ATTENTION;
    }

    return WARN_LEVEL_SAFE;
}

WarnLevel_t WarningService::evaluateCrosswalk(const CrosswalkResult_t* cw)
{
    if (!cw || !cw->detected) {
        return WARN_LEVEL_SAFE;
    }

    return WARN_LEVEL_ATTENTION;
}

WarnLevel_t WarningService::getHigherLevel(WarnLevel_t a, WarnLevel_t b)
{
    return (a > b) ? a : b;
}

void WarningService::applyDebounce(WarnLevel_t newLevel, WarnSource_t newSource,
                                   WarnDirection_t newDir)
{
    if (newLevel != _pendingLevel || newSource != _pendingSource || newDir != _pendingDirection) {
        _pendingLevel = newLevel;
        _pendingSource = newSource;
        _pendingDirection = newDir;
        _stableCount = 0;
    } else {
        if (_stableCount < 255) {
            _stableCount++;
        }
    }

    if (_stableCount >= _debounceFrames) {
        if (_currentLevel != _pendingLevel ||
            _topSource != _pendingSource ||
            _currentDirection != _pendingDirection) {
            _currentLevel = _pendingLevel;
            _topSource = _pendingSource;
            _currentDirection = _pendingDirection;
            ESP_LOGI(TAG_WarningSvc, "Warning state changed: source=%d level=%d dir=%d",
                     _topSource, _currentLevel, _currentDirection);
        }
    }
}

void WarningService::trigger(WarnSource_t source, WarnLevel_t level,
                              WarnDirection_t direction, float distance, const char* message)
{
    Warning_t warn;
    warn.source = source;
    warn.level = level;
    warn.direction = direction;
    warn.distance = distance;
    warn.timestamp = (uint32_t)(esp_log_timestamp() / 1000);

    if (message) {
        strncpy(warn.message, message, sizeof(warn.message) - 1);
        warn.message[sizeof(warn.message) - 1] = '\0';
    } else {
        snprintf(warn.message, sizeof(warn.message),
                 "Triggered: source=%d level=%d", source, level);
    }

    if (_warningQueue) {
        xQueueSend(_warningQueue, &warn, 0);
    }
}

void WarningService::dispatchWarning(WarnSource_t source, WarnLevel_t level,
                                      WarnDirection_t direction, float distance)
{
    trigger(source, level, direction, distance, NULL);
}

void WarningService::dumpStatus(void)
{
    ESP_LOGI(TAG_WarningSvc, "=== Warning Service Status ===");
    ESP_LOGI(TAG_WarningSvc, "  Current level: %d", _currentLevel);
    ESP_LOGI(TAG_WarningSvc, "  Top source: %d", _topSource);
    ESP_LOGI(TAG_WarningSvc, "  Direction: %d", _currentDirection);
    ESP_LOGI(TAG_WarningSvc, "  Debounce frames: %u", _debounceFrames);
    ESP_LOGI(TAG_WarningSvc, "  Stable count: %u", _stableCount);
    ESP_LOGI(TAG_WarningSvc, "  Queue waiting: %u", (unsigned int)uxQueueMessagesWaiting(_warningQueue));
}

/* ---- 状态管理实现 ---- */
static const char* TAG_StateMgr = "StateMgr";

StateManager::StateManager()
{
    _currentState = SYS_STATE_INIT;
    _previousState = SYS_STATE_INIT;
    _radarFrontOnline = false;
    _radarRearOnline = false;
    _cameraOnline = false;
    _callback = NULL;
    _callbackArg = NULL;
    _stateEnterTime = 0;
}

StateManager::~StateManager()
{
}

void StateManager::init(void)
{
    _currentState = SYS_STATE_INIT;
    _previousState = SYS_STATE_INIT;
    _stateEnterTime = (uint32_t)(esp_log_timestamp() / 1000);

    ESP_LOGI(TAG_StateMgr, "State manager initialized, state: %s", getStateName(_currentState));
}

void StateManager::update(void)
{
    if (!_radarFrontOnline || !_radarRearOnline) {
        if (_currentState != SYS_STATE_FAULT && _currentState != SYS_STATE_INIT) {
            setState(SYS_STATE_FAULT);
            ESP_LOGW(TAG_StateMgr, "Radar offline, entering fault state");
        }
    }

    if (_currentState == SYS_STATE_INIT) {
        if (_radarFrontOnline && _radarRearOnline) {
            setState(SYS_STATE_STANDBY);
            ESP_LOGI(TAG_StateMgr, "All sensors online, entering standby");
        }
    }
}

bool StateManager::setState(SystemState_t newState)
{
    if (newState == _currentState) {
        return true;
    }

    if (!canTransition(_currentState, newState)) {
        ESP_LOGW(TAG_StateMgr, "Invalid state transition: %s -> %s",
                 getStateName(_currentState), getStateName(newState));
        return false;
    }

    SystemState_t oldState = _currentState;
    _previousState = _currentState;
    _currentState = newState;
    _stateEnterTime = (uint32_t)(esp_log_timestamp() / 1000);

    ESP_LOGI(TAG_StateMgr, "State changed: %s -> %s",
             getStateName(oldState), getStateName(newState));

    onStateChanged(oldState, newState);

    return true;
}

bool StateManager::canTransition(SystemState_t from, SystemState_t to)
{
    switch (from) {
        case SYS_STATE_INIT:
            return (to == SYS_STATE_STANDBY || to == SYS_STATE_FAULT);
        case SYS_STATE_STANDBY:
            return (to == SYS_STATE_WALKING || to == SYS_STATE_FAULT);
        case SYS_STATE_WALKING:
            return (to == SYS_STATE_STANDBY || to == SYS_STATE_WARNING || to == SYS_STATE_FAULT);
        case SYS_STATE_WARNING:
            return (to == SYS_STATE_WALKING || to == SYS_STATE_STANDBY || to == SYS_STATE_FAULT);
        case SYS_STATE_FAULT:
            return (to == SYS_STATE_STANDBY || to == SYS_STATE_INIT);
        default:
            return false;
    }
}

void StateManager::onStateChanged(SystemState_t oldState, SystemState_t newState)
{
    if (_callback) {
        _callback(oldState, newState, _callbackArg);
    }
}

void StateManager::registerCallback(StateChangeCallback callback, void* arg)
{
    _callback = callback;
    _callbackArg = arg;
}

void StateManager::setRadarOnline(bool front, bool rear)
{
    _radarFrontOnline = front;
    _radarRearOnline = rear;
}

void StateManager::setCameraOnline(bool online)
{
    _cameraOnline = online;
}

const char* StateManager::getStateName(SystemState_t state) const
{
    switch (state) {
        case SYS_STATE_INIT:     return "INIT";
        case SYS_STATE_STANDBY:  return "STANDBY";
        case SYS_STATE_WALKING:  return "WALKING";
        case SYS_STATE_WARNING:  return "WARNING";
        case SYS_STATE_FAULT:    return "FAULT";
        default:                 return "UNKNOWN";
    }
}

/* ---- 调试日志实现 ---- */
static const char* TAG_DebugLog = "DebugLog";

DebugLogger::DebugLogger()
{
    _logQueue = NULL;
    _initialized = false;
    _frontRadarPtr = NULL;
    _rearRadarPtr = NULL;
    _lanePtr = NULL;
    _tlPtr = NULL;
    _cwPtr = NULL;
}

DebugLogger::~DebugLogger()
{
    if (_logQueue) {
        vQueueDelete(_logQueue);
        _logQueue = NULL;
    }
}

bool DebugLogger::init(void)
{
    _logQueue = xQueueCreate(DEBUG_QUEUE_SIZE, sizeof(char) * 128);
    if (!_logQueue) {
        ESP_LOGE(TAG_DebugLog, "Failed to create log queue");
        return false;
    }

    _initialized = true;
    ESP_LOGI(TAG_DebugLog, "Debug logger initialized");
    return true;
}

void DebugLogger::processCommands(void)
{
    char cmdBuf[128];
    int cmdLen = 0;

    while (Serial.available() > 0 && cmdLen < (int)sizeof(cmdBuf) - 1) {
        char c = Serial.read();
        if (c == '\n' || c == '\r') {
            if (cmdLen > 0) {
                cmdBuf[cmdLen] = '\0';
                handleCommand(cmdBuf);
                cmdLen = 0;
            }
        } else {
            cmdBuf[cmdLen++] = c;
        }
    }
}

void DebugLogger::handleCommand(const char* cmdStr)
{
    if (!cmdStr || cmdStr[0] == '\0') return;

    DebugCommand_t cmd = parseCommand(cmdStr);

    switch (cmd) {
        case DEBUG_CMD_HELP:
            printHelp();
            break;
        case DEBUG_CMD_RADAR:
            printRadarStatus();
            break;
        case DEBUG_CMD_LANE:
            printLaneStatus();
            break;
        case DEBUG_CMD_LIGHT:
            printLightStatus();
            break;
        case DEBUG_CMD_WARN:
            printWarningStatus();
            break;
        case DEBUG_CMD_STATE:
            printState();
            break;
        case DEBUG_CMD_FPS:
            printFps();
            break;
        case DEBUG_CMD_BUZZER: {
            uint32_t freq = 1000;
            if (strlen(cmdStr) > 7) {
                freq = (uint32_t)atoi(cmdStr + 7);
            }
            testBuzzer(freq);
            break;
        }
        case DEBUG_CMD_VIB: {
            WarnDirection_t dir = WARN_DIR_FRONT;
            if (strstr(cmdStr, "front")) dir = WARN_DIR_FRONT;
            else if (strstr(cmdStr, "rear")) dir = WARN_DIR_REAR;
            else if (strstr(cmdStr, "left")) dir = WARN_DIR_LEFT;
            else if (strstr(cmdStr, "right")) dir = WARN_DIR_RIGHT;
            testVibrator(dir);
            break;
        }
        default:
            Serial.printf("Unknown command: %s\r\n", cmdStr);
            Serial.println("Type 'help' for available commands");
            break;
    }
}

DebugCommand_t DebugLogger::parseCommand(const char* cmdStr)
{
    if (!cmdStr) return DEBUG_CMD_UNKNOWN;

    if (strncmp(cmdStr, "help", 4) == 0) return DEBUG_CMD_HELP;
    if (strncmp(cmdStr, "radar", 5) == 0) return DEBUG_CMD_RADAR;
    if (strncmp(cmdStr, "lane", 4) == 0) return DEBUG_CMD_LANE;
    if (strncmp(cmdStr, "light", 5) == 0) return DEBUG_CMD_LIGHT;
    if (strncmp(cmdStr, "warn", 4) == 0) return DEBUG_CMD_WARN;
    if (strncmp(cmdStr, "buzzer", 6) == 0) return DEBUG_CMD_BUZZER;
    if (strncmp(cmdStr, "vib", 3) == 0) return DEBUG_CMD_VIB;
    if (strncmp(cmdStr, "fps", 3) == 0) return DEBUG_CMD_FPS;
    if (strncmp(cmdStr, "state", 5) == 0) return DEBUG_CMD_STATE;

    return DEBUG_CMD_UNKNOWN;
}

void DebugLogger::printHelp(void)
{
    Serial.println("\r\n=== Debug Commands ===");
    Serial.println("  help        - Show this help");
    Serial.println("  radar       - Show radar data");
    Serial.println("  lane        - Show lane detection result");
    Serial.println("  light       - Show traffic light result");
    Serial.println("  warn        - Show warning service status");
    Serial.println("  state       - Show system state");
    Serial.println("  fps         - Show FPS statistics");
    Serial.println("  buzzer <f>  - Test buzzer at frequency f Hz");
    Serial.println("  vib <dir>   - Test vibrator (front/rear/left/right)");
    Serial.println("======================\r\n");
}

void DebugLogger::printRadarStatus(void)
{
    Serial.println("\r\n=== Radar Status ===");
    if (_frontRadarPtr) {
        Serial.printf("  Front: %u targets\r\n", _frontRadarPtr->targetCount);
        for (uint8_t i = 0; i < _frontRadarPtr->targetCount; i++) {
            Serial.printf("    [%u] dist=%.2fm speed=%.2fm/s angle=%.1f deg\r\n",
                          i,
                          _frontRadarPtr->targets[i].distance,
                          _frontRadarPtr->targets[i].speed,
                          _frontRadarPtr->targets[i].angle);
        }
    } else {
        Serial.println("  Front: N/A");
    }
    if (_rearRadarPtr) {
        Serial.printf("  Rear: %u targets\r\n", _rearRadarPtr->targetCount);
        for (uint8_t i = 0; i < _rearRadarPtr->targetCount; i++) {
            Serial.printf("    [%u] dist=%.2fm speed=%.2fm/s angle=%.1f deg\r\n",
                          i,
                          _rearRadarPtr->targets[i].distance,
                          _rearRadarPtr->targets[i].speed,
                          _rearRadarPtr->targets[i].angle);
        }
    } else {
        Serial.println("  Rear: N/A");
    }
    Serial.println("====================\r\n");
}

void DebugLogger::printLaneStatus(void)
{
    Serial.println("\r\n=== Lane Detection ===");
    if (_lanePtr) {
        Serial.printf("  Detected: %s\r\n", _lanePtr->detected ? "YES" : "NO");
        const char* posStr = "UNKNOWN";
        switch (_lanePtr->position) {
            case LANE_CENTER: posStr = "CENTER"; break;
            case LANE_LEFT:   posStr = "LEFT"; break;
            case LANE_RIGHT:  posStr = "RIGHT"; break;
            case LANE_LOST:   posStr = "LOST"; break;
        }
        Serial.printf("  Position: %s\r\n", posStr);
        Serial.printf("  Offset: %.2f\r\n", _lanePtr->offsetRatio);
        Serial.printf("  Confidence: %.2f\r\n", _lanePtr->confidence);
    } else {
        Serial.println("  N/A");
    }
    Serial.println("=====================\r\n");
}

void DebugLogger::printLightStatus(void)
{
    Serial.println("\r\n=== Traffic Light ===");
    if (_tlPtr) {
        Serial.printf("  Detected: %s\r\n", _tlPtr->detected ? "YES" : "NO");
        const char* stateStr = "NONE";
        switch (_tlPtr->state) {
            case LIGHT_RED:    stateStr = "RED"; break;
            case LIGHT_YELLOW: stateStr = "YELLOW"; break;
            case LIGHT_GREEN:  stateStr = "GREEN"; break;
            default:           stateStr = "NONE"; break;
        }
        Serial.printf("  State: %s\r\n", stateStr);
        Serial.printf("  Confidence: %.2f\r\n", _tlPtr->confidence);
    } else {
        Serial.println("  N/A");
    }
    Serial.println("=====================\r\n");
}

void DebugLogger::printWarningStatus(void)
{
    Serial.println("\r\n=== Warning Service ===");
    Serial.println("  (TODO: implement full status dump)");
    Serial.println("=======================\r\n");
}

void DebugLogger::printState(void)
{
    Serial.println("\r\n=== System State ===");
    Serial.println("  (TODO: implement state display)");
    Serial.println("====================\r\n");
}

void DebugLogger::printFps(void)
{
    Serial.println("\r\n=== FPS Statistics ===");
    Serial.println("  (TODO: implement FPS tracking)");
    Serial.println("======================\r\n");
}

void DebugLogger::log(const char* tag, const char* fmt, ...)
{
    char buf[256];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    Serial.printf("[%s] %s\r\n", tag, buf);
}

void DebugLogger::logWarning(const Warning_t* warn)
{
    if (!warn) return;

    const char* levelStr = "SAFE";
    switch (warn->level) {
        case WARN_LEVEL_ATTENTION: levelStr = "ATTENTION"; break;
        case WARN_LEVEL_WARNING:   levelStr = "WARNING"; break;
        case WARN_LEVEL_DANGER:    levelStr = "DANGER"; break;
        default:                   levelStr = "SAFE"; break;
    }

    Serial.printf("[WARNING] source=%d level=%s dir=%d dist=%.2f msg=%s\r\n",
                  warn->source, levelStr, warn->direction,
                  warn->distance, warn->message);
}

void DebugLogger::testBuzzer(uint32_t freq)
{
    Serial.printf("Testing buzzer at %lu Hz\r\n", (unsigned long)freq);
    Serial.println("(TODO: buzzer test requires buzzer instance)");
}

void DebugLogger::testVibrator(WarnDirection_t dir)
{
    Serial.printf("Testing vibrator direction: %d\r\n", dir);
    Serial.println("(TODO: vibrator test requires vibrator instance)");
}

void DebugLogger::setRadarDataPtr(const RadarData_t* front, const RadarData_t* rear)
{
    _frontRadarPtr = front;
    _rearRadarPtr = rear;
}

void DebugLogger::setLaneResultPtr(const LaneResult_t* lane)
{
    _lanePtr = lane;
}

void DebugLogger::setTrafficLightPtr(const TrafficLightResult_t* tl)
{
    _tlPtr = tl;
}

void DebugLogger::setCrosswalkPtr(const CrosswalkResult_t* cw)
{
    _cwPtr = cw;
}

/* ============================================================
 *  八、主程序 (来自 main.cpp)
 * ============================================================ */

static const char* TAG_Main = "Main";

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
    ESP_LOGI(TAG_Main, "Front radar task started");

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
    ESP_LOGI(TAG_Main, "Rear radar task started");

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
    ESP_LOGI(TAG_Main, "Vision process task started");

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
    ESP_LOGI(TAG_Main, "Warning decision task started");

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
    ESP_LOGI(TAG_Main, "Feedback output task started");

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
    ESP_LOGI(TAG_Main, "State manager task started");

    while (1) {
        g_stateManager.update();
        vTaskDelay(pdMS_TO_TICKS(STATE_UPDATE_PERIOD_MS));
    }
}

static void taskDebugLogFunc(void* arg)
{
    ESP_LOGI(TAG_Main, "Debug log task started");

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

    ESP_LOGI(TAG_Main, "========================================");
    ESP_LOGI(TAG_Main, "  智慧之眼导盲头环 - 主控板启动");
    ESP_LOGI(TAG_Main, "========================================");

    g_radarFrontMutex = xSemaphoreCreateMutex();
    g_radarRearMutex = xSemaphoreCreateMutex();
    g_visionMutex = xSemaphoreCreateMutex();

    ESP_LOGI(TAG_Main, "Initializing drivers...");
    g_statusLed.init(PIN_STATUS_LED);
    g_statusLed.blink(100, 100, 3);

    g_radarFront.init(RADAR_FRONT_UART, PIN_RADAR_FRONT_TX, PIN_RADAR_FRONT_RX, RADAR_BAUDRATE);
    g_radarRear.init(RADAR_REAR_UART, PIN_RADAR_REAR_TX, PIN_RADAR_REAR_RX, RADAR_BAUDRATE);

    g_buzzer.init(PIN_BUZZER, BUZZER_LEDC_CHANNEL, BUZZER_LEDC_TIMER);
    g_vibrator.init(PIN_VIB_FRONT, PIN_VIB_REAR, PIN_VIB_LEFT, PIN_VIB_RIGHT);

    ESP_LOGI(TAG_Main, "Initializing perception modules...");
    g_radarFrontProc.init();
    g_radarRearProc.init();
    g_laneDetector.init();
    g_trafficLightDetector.init();
    g_crosswalkDetector.init();

    ESP_LOGI(TAG_Main, "Initializing SPI slave...");
    g_spiSlave.init(PIN_SPI_MOSI, PIN_SPI_MISO, PIN_SPI_SCLK, PIN_SPI_CS);
    g_spiSlave.registerImageCallback(onImageReceived, NULL);
    g_spiSlave.start();

    ESP_LOGI(TAG_Main, "Initializing services...");
    g_warningService.init();
    g_stateManager.init();
    g_debugLogger.init();

    g_debugLogger.setRadarDataPtr(&g_frontRadarData, &g_rearRadarData);
    g_debugLogger.setLaneResultPtr(&g_laneResult);
    g_debugLogger.setTrafficLightPtr(&g_trafficLightResult);
    g_debugLogger.setCrosswalkPtr(&g_crosswalkResult);

    ESP_LOGI(TAG_Main, "Creating FreeRTOS tasks...");

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

    ESP_LOGI(TAG_Main, "System initialization complete!");
    ESP_LOGI(TAG_Main, "Type 'help' in serial monitor for debug commands");
}

void loop()
{
    if (!g_systemReady) {
        delay(100);
        return;
    }

    vTaskDelay(pdMS_TO_TICKS(1000));
}
