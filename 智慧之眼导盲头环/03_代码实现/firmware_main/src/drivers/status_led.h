#ifndef STATUS_LED_H
#define STATUS_LED_H

#include <stdint.h>
#include <stdbool.h>

class StatusLED {
public:
    StatusLED();
    ~StatusLED();

    bool init(int pin);

    void on(void);
    void off(void);
    void toggle(void);
    void blink(uint32_t onMs, uint32_t offMs, uint8_t count);
    void setPattern(uint8_t pattern);

    bool getState(void) const { return _state; }

private:
    int  _pin;
    bool _initialized;
    bool _state;
};

#endif
