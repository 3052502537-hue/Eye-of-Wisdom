#include "perception/traffic_light.h"
#include "config.h"
#include "esp_log.h"
#include <string.h>
#include <math.h>

static const char* TAG = "TrafficLight";

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
    ESP_LOGI(TAG, "Traffic light detector initialized");
    ESP_LOGI(TAG, "  ROI: top=%.2f, bottom=%.2f", _roiTopRatio, _roiBottomRatio);
    ESP_LOGI(TAG, "  Confidence threshold: %.2f", _confidenceThreshold);
    ESP_LOGI(TAG, "  Min light area: %d", _minLightArea);
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

    ESP_LOGD(TAG, "Detecting traffic light: %dx%d (TODO: implement actual algorithm)",
             width, height);

    newState = LIGHT_RED;
    newConfidence = 0.6f;
    detected = true;

    _pendingDetected = detected;
    _pendingState = newState;

    applyDebounce();

    ESP_LOGD(TAG, "Traffic light result: detected=%d, state=%d, conf=%.2f",
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
            ESP_LOGI(TAG, "Traffic light state changed: detected=%d, state=%d",
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
