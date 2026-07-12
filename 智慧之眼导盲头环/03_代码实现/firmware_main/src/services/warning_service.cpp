#include "services/warning_service.h"
#include "config.h"
#include "esp_log.h"
#include <string.h>
#include <stdio.h>

static const char* TAG = "WarningSvc";

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
        ESP_LOGE(TAG, "Failed to create warning queue");
        return false;
    }

    ESP_LOGI(TAG, "Warning service initialized");
    ESP_LOGI(TAG, "  Debounce frames: %u", _debounceFrames);
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

    ESP_LOGD(TAG, "Levels: front=%d rear=%d lane=%d tl=%d cw=%d, top=%d/%d",
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
            ESP_LOGI(TAG, "Warning state changed: source=%d level=%d dir=%d",
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
    ESP_LOGI(TAG, "=== Warning Service Status ===");
    ESP_LOGI(TAG, "  Current level: %d", _currentLevel);
    ESP_LOGI(TAG, "  Top source: %d", _topSource);
    ESP_LOGI(TAG, "  Direction: %d", _currentDirection);
    ESP_LOGI(TAG, "  Debounce frames: %u", _debounceFrames);
    ESP_LOGI(TAG, "  Stable count: %u", _stableCount);
    ESP_LOGI(TAG, "  Queue waiting: %u", (unsigned int)uxQueueMessagesWaiting(_warningQueue));
}
