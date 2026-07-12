#include "services/state_manager.h"
#include "esp_log.h"
#include <string.h>

static const char* TAG = "StateMgr";

StateManager::StateManager()
{
    _currentState = SYS_STATE_INIT;
    _previousState = SYS_STATE_INIT;
    _radarFrontOnline = false;
    _radarRearOnline = false;
    _cameraOnline = false;
    _callback = NULL;
    _callbackArg = NULL;
    _stateEnterTime = 0;
}

StateManager::~StateManager()
{
}

void StateManager::init(void)
{
    _currentState = SYS_STATE_INIT;
    _previousState = SYS_STATE_INIT;
    _stateEnterTime = (uint32_t)(esp_log_timestamp() / 1000);

    ESP_LOGI(TAG, "State manager initialized, state: %s", getStateName(_currentState));
}

void StateManager::update(void)
{
    if (!_radarFrontOnline || !_radarRearOnline) {
        if (_currentState != SYS_STATE_FAULT && _currentState != SYS_STATE_INIT) {
            setState(SYS_STATE_FAULT);
            ESP_LOGW(TAG, "Radar offline, entering fault state");
        }
    }

    if (_currentState == SYS_STATE_INIT) {
        if (_radarFrontOnline && _radarRearOnline) {
            setState(SYS_STATE_STANDBY);
            ESP_LOGI(TAG, "All sensors online, entering standby");
        }
    }
}

bool StateManager::setState(SystemState_t newState)
{
    if (newState == _currentState) {
        return true;
    }

    if (!canTransition(_currentState, newState)) {
        ESP_LOGW(TAG, "Invalid state transition: %s -> %s",
                 getStateName(_currentState), getStateName(newState));
        return false;
    }

    SystemState_t oldState = _currentState;
    _previousState = _currentState;
    _currentState = newState;
    _stateEnterTime = (uint32_t)(esp_log_timestamp() / 1000);

    ESP_LOGI(TAG, "State changed: %s -> %s",
             getStateName(oldState), getStateName(newState));

    onStateChanged(oldState, newState);

    return true;
}

bool StateManager::canTransition(SystemState_t from, SystemState_t to)
{
    switch (from) {
        case SYS_STATE_INIT:
            return (to == SYS_STATE_STANDBY || to == SYS_STATE_FAULT);
        case SYS_STATE_STANDBY:
            return (to == SYS_STATE_WALKING || to == SYS_STATE_FAULT);
        case SYS_STATE_WALKING:
            return (to == SYS_STATE_STANDBY || to == SYS_STATE_WARNING || to == SYS_STATE_FAULT);
        case SYS_STATE_WARNING:
            return (to == SYS_STATE_WALKING || to == SYS_STATE_STANDBY || to == SYS_STATE_FAULT);
        case SYS_STATE_FAULT:
            return (to == SYS_STATE_STANDBY || to == SYS_STATE_INIT);
        default:
            return false;
    }
}

void StateManager::onStateChanged(SystemState_t oldState, SystemState_t newState)
{
    if (_callback) {
        _callback(oldState, newState, _callbackArg);
    }
}

void StateManager::registerCallback(StateChangeCallback callback, void* arg)
{
    _callback = callback;
    _callbackArg = arg;
}

void StateManager::setRadarOnline(bool front, bool rear)
{
    _radarFrontOnline = front;
    _radarRearOnline = rear;
}

void StateManager::setCameraOnline(bool online)
{
    _cameraOnline = online;
}

const char* StateManager::getStateName(SystemState_t state) const
{
    switch (state) {
        case SYS_STATE_INIT:     return "INIT";
        case SYS_STATE_STANDBY:  return "STANDBY";
        case SYS_STATE_WALKING:  return "WALKING";
        case SYS_STATE_WARNING:  return "WARNING";
        case SYS_STATE_FAULT:    return "FAULT";
        default:                 return "UNKNOWN";
    }
}
