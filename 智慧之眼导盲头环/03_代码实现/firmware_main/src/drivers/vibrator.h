#ifndef VIBRATOR_H
#define VIBRATOR_H

#include <stdint.h>
#include <stdbool.h>
#include "types.h"

class Vibrator {
public:
    Vibrator();
    ~Vibrator();

    bool init(int frontPin, int rearPin, int leftPin, int rightPin);

    void vibrate(WarnDirection_t direction, uint32_t durationMs);
    void vibrateFront(uint32_t durationMs);
    void vibrateRear(uint32_t durationMs);
    void vibrateLeft(uint32_t durationMs);
    void vibrateRight(uint32_t durationMs);
    void vibrateAll(uint32_t durationMs);
    void stopAll(void);

    void setIntensity(uint8_t intensity);
    uint8_t getIntensity(void) const { return _intensity; }

private:
    int  _frontPin;
    int  _rearPin;
    int  _leftPin;
    int  _rightPin;
    bool _initialized;
    uint8_t _intensity;

    void setPin(int pin, bool state);
};

#endif
