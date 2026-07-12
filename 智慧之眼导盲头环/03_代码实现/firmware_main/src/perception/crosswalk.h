#ifndef CROSSWALK_H
#define CROSSWALK_H

#include <stdint.h>
#include <stdbool.h>
#include "types.h"

class CrosswalkDetector {
public:
    CrosswalkDetector();
    ~CrosswalkDetector();

    void init(void);
    void detect(uint8_t* image, int width, int height);

    CrosswalkResult_t getResult(void) const { return _result; }

    void setRoiRange(float topRatio, float bottomRatio);
    void setMinStripes(uint8_t count);
    void setConfidenceThreshold(float threshold);
    void setDebounceFrames(uint8_t frames);

    void setStripeWidthRange(int minWidth, int maxWidth);
    void setStripeSpacingTolerance(float tolerance);

private:
    CrosswalkResult_t _result;

    float _roiTopRatio;
    float _roiBottomRatio;
    uint8_t _minStripes;
    float _confidenceThreshold;
    uint8_t _debounceFrames;

    int   _minStripeWidth;
    int   _maxStripeWidth;
    float _stripeSpacingTolerance;

    uint8_t _stableCount;
    bool _pendingDetected;
    uint8_t _pendingStripeCount;

    void extractRoi(uint8_t* image, int width, int height,
                    uint8_t** roiOut, int* roiW, int* roiH);
    void convertToGray(uint8_t* rgbImage, uint8_t* gray, int w, int h);
    void edgeDetect(uint8_t* gray, uint8_t* edges, int w, int h);
    int houghHorizontalLines(uint8_t* edges, int w, int h,
                             float* lines, int maxLines);
    int findParallelStripes(float* lines, int lineCount, int width,
                            int* stripePositions, int* stripeWidths, int maxStripes);
    int countEquallySpacedStripes(int* positions, int count, float tolerance);
    float calculateConfidence(int stripeCount, int validCount);
    void applyDebounce(void);
};

#endif
