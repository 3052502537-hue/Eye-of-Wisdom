#ifndef DEBUG_LOG_H
#define DEBUG_LOG_H

#include <stdint.h>
#include <stdbool.h>
#include <stdarg.h>
#include "types.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

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

#endif
