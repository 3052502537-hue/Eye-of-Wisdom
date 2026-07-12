#ifndef TRAFFIC_LIGHT_H
#define TRAFFIC_LIGHT_H

#include <stdint.h>
#include <stdbool.h>
#include "types.h"

class TrafficLightDetector {
public:
    TrafficLightDetector();
    ~TrafficLightDetector();

    void init(void);
    void detect(uint8_t* image, int width, int height);

    TrafficLightResult_t getResult(void) const { return _result; }

    void setRoiRange(float topRatio, float bottomRatio);
    void setConfidenceThreshold(float threshold);
    void setDebounceFrames(uint8_t frames);

    void setRedHsvRange(uint8_t hLow1, uint8_t hHigh1, uint8_t hLow2, uint8_t hHigh2,
                        uint8_t sLow, uint8_t vLow);
    void setYellowHsvRange(uint8_t hLow, uint8_t hHigh, uint8_t sLow, uint8_t vLow);
    void setGreenHsvRange(uint8_t hLow, uint8_t hHigh, uint8_t sLow, uint8_t vLow);

    void setMinLightArea(int minArea);
    void setCircularityThreshold(float threshold);

private:
    TrafficLightResult_t _result;

    float _roiTopRatio;
    float _roiBottomRatio;
    float _confidenceThreshold;
    uint8_t _debounceFrames;

    uint8_t _redHlow1, _redHhigh1;
    uint8_t _redHlow2, _redHhigh2;
    uint8_t _redSlow, _redVlow;

    uint8_t _yellowHlow, _yellowHhigh;
    uint8_t _yellowSlow, _yellowVlow;

    uint8_t _greenHlow, _greenHhigh;
    uint8_t _greenSlow, _greenVlow;

    int   _minLightArea;
    float _circularityThreshold;

    uint8_t _stableCount;
    TrafficLightState_t _pendingState;
    bool _pendingDetected;

    void extractRoi(uint8_t* image, int width, int height,
                    uint8_t** roiOut, int* roiW, int* roiH, int* roiX, int* roiY);
    bool convertToHsv(uint8_t* rgbImage, uint8_t* hsvImage, int w, int h);
    int  colorSegmentation(uint8_t* hsvImage, uint8_t* mask, int w, int h,
                           uint8_t hLow, uint8_t hHigh,
                           uint8_t sLow, uint8_t vLow);
    void morphologicalOps(uint8_t* binary, int w, int h);
    int  findLightContours(uint8_t* binary, int w, int h,
                           int* centersX, int* centersY, int* areas, int maxLights);
    float calculateCircularity(int area, float perimeter);
    TrafficLightState_t determineState(int redCount, int yellowCount, int greenCount,
                                       float redConf, float yellowConf, float greenConf);
    void applyDebounce(void);
};

#endif
