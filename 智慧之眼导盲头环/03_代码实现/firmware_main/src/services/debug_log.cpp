#include "services/debug_log.h"
#include "config.h"
#include "esp_log.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

static const char* TAG = "DebugLog";

DebugLogger::DebugLogger()
{
    _logQueue = NULL;
    _initialized = false;
    _frontRadarPtr = NULL;
    _rearRadarPtr = NULL;
    _lanePtr = NULL;
    _tlPtr = NULL;
    _cwPtr = NULL;
}

DebugLogger::~DebugLogger()
{
    if (_logQueue) {
        vQueueDelete(_logQueue);
        _logQueue = NULL;
    }
}

bool DebugLogger::init(void)
{
    _logQueue = xQueueCreate(DEBUG_QUEUE_SIZE, sizeof(char) * 128);
    if (!_logQueue) {
        ESP_LOGE(TAG, "Failed to create log queue");
        return false;
    }

    _initialized = true;
    ESP_LOGI(TAG, "Debug logger initialized");
    return true;
}

void DebugLogger::processCommands(void)
{
    char cmdBuf[128];
    int cmdLen = 0;

    while (Serial.available() > 0 && cmdLen < (int)sizeof(cmdBuf) - 1) {
        char c = Serial.read();
        if (c == '\n' || c == '\r') {
            if (cmdLen > 0) {
                cmdBuf[cmdLen] = '\0';
                handleCommand(cmdBuf);
                cmdLen = 0;
            }
        } else {
            cmdBuf[cmdLen++] = c;
        }
    }
}

void DebugLogger::handleCommand(const char* cmdStr)
{
    if (!cmdStr || cmdStr[0] == '\0') return;

    DebugCommand_t cmd = parseCommand(cmdStr);

    switch (cmd) {
        case DEBUG_CMD_HELP:
            printHelp();
            break;
        case DEBUG_CMD_RADAR:
            printRadarStatus();
            break;
        case DEBUG_CMD_LANE:
            printLaneStatus();
            break;
        case DEBUG_CMD_LIGHT:
            printLightStatus();
            break;
        case DEBUG_CMD_WARN:
            printWarningStatus();
            break;
        case DEBUG_CMD_STATE:
            printState();
            break;
        case DEBUG_CMD_FPS:
            printFps();
            break;
        case DEBUG_CMD_BUZZER: {
            uint32_t freq = 1000;
            if (strlen(cmdStr) > 7) {
                freq = (uint32_t)atoi(cmdStr + 7);
            }
            testBuzzer(freq);
            break;
        }
        case DEBUG_CMD_VIB: {
            WarnDirection_t dir = WARN_DIR_FRONT;
            if (strstr(cmdStr, "front")) dir = WARN_DIR_FRONT;
            else if (strstr(cmdStr, "rear")) dir = WARN_DIR_REAR;
            else if (strstr(cmdStr, "left")) dir = WARN_DIR_LEFT;
            else if (strstr(cmdStr, "right")) dir = WARN_DIR_RIGHT;
            testVibrator(dir);
            break;
        }
        default:
            Serial.printf("Unknown command: %s\r\n", cmdStr);
            Serial.println("Type 'help' for available commands");
            break;
    }
}

DebugCommand_t DebugLogger::parseCommand(const char* cmdStr)
{
    if (!cmdStr) return DEBUG_CMD_UNKNOWN;

    if (strncmp(cmdStr, "help", 4) == 0) return DEBUG_CMD_HELP;
    if (strncmp(cmdStr, "radar", 5) == 0) return DEBUG_CMD_RADAR;
    if (strncmp(cmdStr, "lane", 4) == 0) return DEBUG_CMD_LANE;
    if (strncmp(cmdStr, "light", 5) == 0) return DEBUG_CMD_LIGHT;
    if (strncmp(cmdStr, "warn", 4) == 0) return DEBUG_CMD_WARN;
    if (strncmp(cmdStr, "buzzer", 6) == 0) return DEBUG_CMD_BUZZER;
    if (strncmp(cmdStr, "vib", 3) == 0) return DEBUG_CMD_VIB;
    if (strncmp(cmdStr, "fps", 3) == 0) return DEBUG_CMD_FPS;
    if (strncmp(cmdStr, "state", 5) == 0) return DEBUG_CMD_STATE;

    return DEBUG_CMD_UNKNOWN;
}

void DebugLogger::printHelp(void)
{
    Serial.println("\r\n=== Debug Commands ===");
    Serial.println("  help        - Show this help");
    Serial.println("  radar       - Show radar data");
    Serial.println("  lane        - Show lane detection result");
    Serial.println("  light       - Show traffic light result");
    Serial.println("  warn        - Show warning service status");
    Serial.println("  state       - Show system state");
    Serial.println("  fps         - Show FPS statistics");
    Serial.println("  buzzer <f>  - Test buzzer at frequency f Hz");
    Serial.println("  vib <dir>   - Test vibrator (front/rear/left/right)");
    Serial.println("======================\r\n");
}

void DebugLogger::printRadarStatus(void)
{
    Serial.println("\r\n=== Radar Status ===");
    if (_frontRadarPtr) {
        Serial.printf("  Front: %u targets\r\n", _frontRadarPtr->targetCount);
        for (uint8_t i = 0; i < _frontRadarPtr->targetCount; i++) {
            Serial.printf("    [%u] dist=%.2fm speed=%.2fm/s angle=%.1f deg\r\n",
                          i,
                          _frontRadarPtr->targets[i].distance,
                          _frontRadarPtr->targets[i].speed,
                          _frontRadarPtr->targets[i].angle);
        }
    } else {
        Serial.println("  Front: N/A");
    }
    if (_rearRadarPtr) {
        Serial.printf("  Rear: %u targets\r\n", _rearRadarPtr->targetCount);
        for (uint8_t i = 0; i < _rearRadarPtr->targetCount; i++) {
            Serial.printf("    [%u] dist=%.2fm speed=%.2fm/s angle=%.1f deg\r\n",
                          i,
                          _rearRadarPtr->targets[i].distance,
                          _rearRadarPtr->targets[i].speed,
                          _rearRadarPtr->targets[i].angle);
        }
    } else {
        Serial.println("  Rear: N/A");
    }
    Serial.println("====================\r\n");
}

void DebugLogger::printLaneStatus(void)
{
    Serial.println("\r\n=== Lane Detection ===");
    if (_lanePtr) {
        Serial.printf("  Detected: %s\r\n", _lanePtr->detected ? "YES" : "NO");
        const char* posStr = "UNKNOWN";
        switch (_lanePtr->position) {
            case LANE_CENTER: posStr = "CENTER"; break;
            case LANE_LEFT:   posStr = "LEFT"; break;
            case LANE_RIGHT:  posStr = "RIGHT"; break;
            case LANE_LOST:   posStr = "LOST"; break;
        }
        Serial.printf("  Position: %s\r\n", posStr);
        Serial.printf("  Offset: %.2f\r\n", _lanePtr->offsetRatio);
        Serial.printf("  Confidence: %.2f\r\n", _lanePtr->confidence);
    } else {
        Serial.println("  N/A");
    }
    Serial.println("=====================\r\n");
}

void DebugLogger::printLightStatus(void)
{
    Serial.println("\r\n=== Traffic Light ===");
    if (_tlPtr) {
        Serial.printf("  Detected: %s\r\n", _tlPtr->detected ? "YES" : "NO");
        const char* stateStr = "NONE";
        switch (_tlPtr->state) {
            case LIGHT_RED:    stateStr = "RED"; break;
            case LIGHT_YELLOW: stateStr = "YELLOW"; break;
            case LIGHT_GREEN:  stateStr = "GREEN"; break;
            default:           stateStr = "NONE"; break;
        }
        Serial.printf("  State: %s\r\n", stateStr);
        Serial.printf("  Confidence: %.2f\r\n", _tlPtr->confidence);
    } else {
        Serial.println("  N/A");
    }
    Serial.println("=====================\r\n");
}

void DebugLogger::printWarningStatus(void)
{
    Serial.println("\r\n=== Warning Service ===");
    Serial.println("  (TODO: implement full status dump)");
    Serial.println("=======================\r\n");
}

void DebugLogger::printState(void)
{
    Serial.println("\r\n=== System State ===");
    Serial.println("  (TODO: implement state display)");
    Serial.println("====================\r\n");
}

void DebugLogger::printFps(void)
{
    Serial.println("\r\n=== FPS Statistics ===");
    Serial.println("  (TODO: implement FPS tracking)");
    Serial.println("======================\r\n");
}

void DebugLogger::log(const char* tag, const char* fmt, ...)
{
    char buf[256];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    Serial.printf("[%s] %s\r\n", tag, buf);
}

void DebugLogger::logWarning(const Warning_t* warn)
{
    if (!warn) return;

    const char* levelStr = "SAFE";
    switch (warn->level) {
        case WARN_LEVEL_ATTENTION: levelStr = "ATTENTION"; break;
        case WARN_LEVEL_WARNING:   levelStr = "WARNING"; break;
        case WARN_LEVEL_DANGER:    levelStr = "DANGER"; break;
        default:                   levelStr = "SAFE"; break;
    }

    Serial.printf("[WARNING] source=%d level=%s dir=%d dist=%.2f msg=%s\r\n",
                  warn->source, levelStr, warn->direction,
                  warn->distance, warn->message);
}

void DebugLogger::testBuzzer(uint32_t freq)
{
    Serial.printf("Testing buzzer at %lu Hz\r\n", (unsigned long)freq);
    Serial.println("(TODO: buzzer test requires buzzer instance)");
}

void DebugLogger::testVibrator(WarnDirection_t dir)
{
    Serial.printf("Testing vibrator direction: %d\r\n", dir);
    Serial.println("(TODO: vibrator test requires vibrator instance)");
}

void DebugLogger::setRadarDataPtr(const RadarData_t* front, const RadarData_t* rear)
{
    _frontRadarPtr = front;
    _rearRadarPtr = rear;
}

void DebugLogger::setLaneResultPtr(const LaneResult_t* lane)
{
    _lanePtr = lane;
}

void DebugLogger::setTrafficLightPtr(const TrafficLightResult_t* tl)
{
    _tlPtr = tl;
}

void DebugLogger::setCrosswalkPtr(const CrosswalkResult_t* cw)
{
    _cwPtr = cw;
}
