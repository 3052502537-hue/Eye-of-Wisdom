#ifndef STATE_MANAGER_H
#define STATE_MANAGER_H

#include <stdint.h>
#include <stdbool.h>
#include "types.h"

typedef void (*StateChangeCallback)(SystemState_t oldState, SystemState_t newState, void* arg);

class StateManager {
public:
    StateManager();
    ~StateManager();

    void init(void);
    void update(void);

    SystemState_t getState(void) const { return _currentState; }
    bool setState(SystemState_t newState);

    void registerCallback(StateChangeCallback callback, void* arg);

    const char* getStateName(SystemState_t state) const;

    bool isWalking(void) const { return _currentState == SYS_STATE_WALKING; }
    bool isWarning(void) const { return _currentState == SYS_STATE_WARNING; }
    bool isFault(void) const { return _currentState == SYS_STATE_FAULT; }

    void setRadarOnline(bool front, bool rear);
    void setCameraOnline(bool online);
    bool getRadarFrontOnline(void) const { return _radarFrontOnline; }
    bool getRadarRearOnline(void) const { return _radarRearOnline; }
    bool getCameraOnline(void) const { return _cameraOnline; }

private:
    SystemState_t _currentState;
    SystemState_t _previousState;

    bool _radarFrontOnline;
    bool _radarRearOnline;
    bool _cameraOnline;

    StateChangeCallback _callback;
    void* _callbackArg;

    uint32_t _stateEnterTime;

    bool canTransition(SystemState_t from, SystemState_t to);
    void onStateChanged(SystemState_t oldState, SystemState_t newState);
};

#endif
