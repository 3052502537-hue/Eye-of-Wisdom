#include "perception/lane_detection.h"
#include "config.h"
#include "esp_log.h"
#include <string.h>
#include <math.h>

static const char* TAG = "LaneDetect";

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
    ESP_LOGI(TAG, "Lane detector initialized");
    ESP_LOGI(TAG, "  Color mode: %d", _colorMode);
    ESP_LOGI(TAG, "  ROI top ratio: %.2f", _roiTopRatio);
    ESP_LOGI(TAG, "  Confidence threshold: %.2f", _confidenceThreshold);
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

    ESP_LOGD(TAG, "Detecting lane: %dx%d (TODO: implement actual algorithm)",
             width, height);

    if (_colorMode == LANE_COLOR_YELLOW) {
        ESP_LOGD(TAG, "  Mode: Yellow lane detection");
    } else if (_colorMode == LANE_COLOR_GRAY) {
        ESP_LOGD(TAG, "  Mode: Gray lane detection");
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

    ESP_LOGD(TAG, "Lane result: detected=%d, position=%d, conf=%.2f",
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
            ESP_LOGI(TAG, "Lane state changed: detected=%d, position=%d",
                     _result.detected, _result.position);
        }
    }
}

void LaneDetector::setColorMode(LaneColorMode_t mode)
{
    _colorMode = mode;
    ESP_LOGI(TAG, "Color mode set to %d", mode);
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
