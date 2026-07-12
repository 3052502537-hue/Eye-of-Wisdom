#include "perception/radar_front.h"
#include "config.h"
#include "esp_log.h"
#include <string.h>
#include <math.h>

static const char* TAG = "RadarFront";

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
    ESP_LOGI(TAG, "Front radar processor initialized");
    ESP_LOGI(TAG, "  Attention: %.2fm, Warning: %.2fm, Danger: %.2fm",
             _thresholdAttention, _thresholdWarning, _thresholdDanger);
    ESP_LOGI(TAG, "  Debounce frames: %u", _debounceFrames);
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

    ESP_LOGD(TAG, "Front radar: level=%d, closest_dist=%.2fm, dir=%d",
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
                ESP_LOGD(TAG, "Zone %d level changed to %d", i, _zoneLevels[i]);
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
