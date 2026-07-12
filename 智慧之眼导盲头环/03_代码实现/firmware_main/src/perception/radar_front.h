#ifndef RADAR_FRONT_H
#define RADAR_FRONT_H

#include <stdint.h>
#include <stdbool.h>
#include "types.h"

#define FRONT_RADAR_ZONES 3

typedef enum {
    ZONE_FRONT_LEFT = 0,
    ZONE_FRONT_CENTER,
    ZONE_FRONT_RIGHT
} FrontRadarZone_t;

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

#endif
