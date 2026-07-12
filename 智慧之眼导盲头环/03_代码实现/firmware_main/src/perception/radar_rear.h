#ifndef RADAR_REAR_H
#define RADAR_REAR_H

#include <stdint.h>
#include <stdbool.h>
#include "types.h"

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

#endif
