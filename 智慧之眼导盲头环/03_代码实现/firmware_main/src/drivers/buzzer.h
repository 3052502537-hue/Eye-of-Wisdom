#ifndef BUZZER_H
#define BUZZER_H

#include <stdint.h>
#include <stdbool.h>

class Buzzer {
public:
    Buzzer();
    ~Buzzer();

    bool init(int pin, int ledcChannel = 0, int ledcTimer = 0);
    void beep(uint32_t frequency, uint32_t durationMs);
    void startTone(uint32_t frequency);
    void stopTone(void);
    void setVolume(uint8_t volume);
    void playPattern(const uint16_t* pattern, uint8_t count);

    bool isBeeping(void) const { return _beeping; }

private:
    int     _pin;
    int     _ledcChannel;
    int     _ledcTimer;
    bool    _initialized;
    bool    _beeping;
    uint8_t _volume;
};

#endif
