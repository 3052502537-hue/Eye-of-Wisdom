#ifndef LANE_DETECTION_H
#define LANE_DETECTION_H

#include <stdint.h>
#include <stdbool.h>
#include "types.h"

typedef enum {
    LANE_COLOR_YELLOW = 0,
    LANE_COLOR_GRAY,
    LANE_COLOR_AUTO
} LaneColorMode_t;

class LaneDetector {
public:
    LaneDetector();
    ~LaneDetector();

    void init(void);
    void detect(uint8_t* image, int width, int height);

    LaneResult_t getResult(void) const { return _result; }

    void setColorMode(LaneColorMode_t mode);
    LaneColorMode_t getColorMode(void) const { return _colorMode; }

    void setRoiTopRatio(float ratio);
    void setConfidenceThreshold(float threshold);
    void setDebounceFrames(uint8_t frames);

    void setHsvYellowLow(uint8_t h, uint8_t s, uint8_t v);
    void setHsvYellowHigh(uint8_t h, uint8_t s, uint8_t v);

private:
    LaneResult_t _result;
    LaneColorMode_t _colorMode;
    float _roiTopRatio;
    float _confidenceThreshold;
    uint8_t _debounceFrames;

    uint8_t _hsvYellowLow[3];
    uint8_t _hsvYellowHigh[3];

    uint8_t _stableCount;
    LanePosition_t _pendingPosition;
    bool _pendingDetected;

    void extractRoi(uint8_t* image, int width, int height,
                    uint8_t** roiOut, int* roiW, int* roiH);

    bool convertToHsv(uint8_t* rgbImage, uint8_t* hsvImage, int w, int h);
    int colorThreshold(uint8_t* hsvImage, uint8_t* binary, int w, int h);
    void edgeDetect(uint8_t* gray, uint8_t* edges, int w, int h);
    int houghLines(uint8_t* edges, int w, int h, float* lines, int maxLines);
    LanePosition_t calculatePosition(float* lines, int lineCount, int width);
    float calculateConfidence(int lineCount, int validPairs);
    void applyDebounce(void);
};

#endif
