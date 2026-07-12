#include "perception/radar_rear.h"
#include "config.h"
#include "esp_log.h"
#include <string.h>
#include <math.h>

static const char* TAG = "RadarRear";

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
    ESP_LOGI(TAG, "Rear radar processor initialized");
    ESP_LOGI(TAG, "  Fast approach speed threshold: %.2f m/s", _fastApproachSpeed);
}

void RadarRearProcessor::update(const RadarData_t* rawData)
{
    if (!rawData) {
        return;
    }

    findNearestTarget(rawData);
    calculateDangerLevel();
    applyDebounce();

    ESP_LOGD(TAG, "Rear radar: level=%d, nearest=%.2fm, speed=%.2fm/s, fast_approach=%d",
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
            ESP_LOGI(TAG, "Fast approach %s, speed=%.2f m/s",
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
