#ifndef TYPES_H
#define TYPES_H

#include <stdint.h>
#include <stdbool.h>

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

#endif
