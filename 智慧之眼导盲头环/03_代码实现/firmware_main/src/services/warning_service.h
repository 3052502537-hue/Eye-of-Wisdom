#ifndef WARNING_SERVICE_H
#define WARNING_SERVICE_H

#include <stdint.h>
#include <stdbool.h>
#include "types.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

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

#endif
