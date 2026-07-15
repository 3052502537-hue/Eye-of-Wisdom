/* ============================================================
 * 文件名: wifi_manager.cpp
 * 功能描述: WiFi 管理实现 v3.0
 *           v3.0: 删除UDP图像发送功能（摄像板直连手机，主控不再中转图像）
 *           实现 AP 热点启动、TCP 服务器、客户端管理、
 *           传感器JSON广播、命令接收
 * 依赖关系: Arduino WiFi 库、config.h、protocol.h、wifi_manager.h
 * 接口说明: 见头文件
 * ============================================================ */

#include "wifi_manager.h"

#ifdef DEBUG
  #define DBG_PRINT(x)     Serial.print(x)
  #define DBG_PRINTLN(x)   Serial.println(x)
  #define DBG_PRINTF(fmt, ...) Serial.printf(fmt, ##__VA_ARGS__)
#else
  #define DBG_PRINT(x)
  #define DBG_PRINTLN(x)
  #define DBG_PRINTF(fmt, ...)
#endif

/* 构造函数 */
WifiManager::WifiManager()
    : _tcpServer(nullptr), _apIp(IPAddress(192, 168, 4, 1)),
      _clientCount(0), _cmdCb(nullptr),
      _apStarted(false), _tcpStarted(false), _rxLen(0)
{
    memset(_rxBuf, 0, sizeof(_rxBuf));
}

/* 析构函数 */
WifiManager::~WifiManager()
{
    if (_tcpServer) {
        _tcpServer->stop();
        delete _tcpServer;
        _tcpServer = nullptr;
    }
    if (_apStarted) WiFi.softAPdisconnect(true);
}

/* begin - 启动 AP + TCP */
bool WifiManager::begin()
{
    if (!startAP()) return false;
    startTcpServer();
    return true;
}

/* startAP - 启动 AP 热点 */
bool WifiManager::startAP()
{
    if (_apStarted) return true;

    /* 配置AP */
    WiFi.mode(WIFI_AP);
    bool ok = WiFi.softAP(WIFI_AP_SSID, WIFI_AP_PASSWORD,
                          WIFI_AP_CHANNEL, WIFI_AP_HIDDEN,
                          WIFI_AP_MAX_CONNECTIONS);
    if (!ok) {
        DBG_PRINTLN("[WIFI] AP start FAIL");
        return false;
    }

    _apIp = WiFi.softAPIP();
    _apStarted = true;
    DBG_PRINTF("[WIFI] AP started: SSID=%s IP=%s\n",
               WIFI_AP_SSID, _apIp.toString().c_str());
    return true;
}

/* startTcpServer - 启动 TCP 服务器 */
void WifiManager::startTcpServer()
{
    if (_tcpStarted) return;
    _tcpServer = new WiFiServer(TCP_PORT);
    _tcpServer->begin();
    _tcpStarted = true;
    DBG_PRINTF("[WIFI] TCP server on port %d\n", TCP_PORT);
}

/* sendSensorJson - 向所有TCP客户端发送JSON */
uint8_t WifiManager::sendSensorJson(const char* jsonStr, size_t len)
{
    if (!_tcpStarted || !jsonStr || len == 0) return 0;

    uint8_t sent = 0;
    for (int i = 0; i < TCP_MAX_CLIENTS; i++) {
        if (_clients[i] && _clients[i].connected()) {
            _clients[i].write((const uint8_t*)jsonStr, len);
            _clients[i].write('\n');   // 以换行分隔消息
            sent++;
        }
    }
    return sent;
}

/* processTcpClients - 处理客户端连接与命令 */
void WifiManager::processTcpClients()
{
    if (!_tcpStarted || !_tcpServer) return;

    /* 1. 接受新连接 */
    if (_tcpServer->hasClient()) {
        for (int i = 0; i < TCP_MAX_CLIENTS; i++) {
            if (!_clients[i] || !_clients[i].connected()) {
                _clients[i] = _tcpServer->accept();
                if (_clients[i]) {
                    DBG_PRINTF("[WIFI] client connected slot=%d ip=%s\n",
                               i, _clients[i].remoteIP().toString().c_str());
                }
                break;
            }
        }
    }

    /* 2. 统计连接数 + 读取命令 */
    uint8_t count = 0;
    for (int i = 0; i < TCP_MAX_CLIENTS; i++) {
        if (_clients[i] && _clients[i].connected()) {
            count++;
            /* 读取可用数据 */
            while (_clients[i].available()) {
                char c = _clients[i].read();
                if (c == '\n' || _rxLen >= sizeof(_rxBuf) - 1) {
                    /* 一行命令结束 */
                    _rxBuf[_rxLen] = '\0';
                    if (_rxLen > 0) {
                        handleCommand(_rxBuf, _rxLen);
                    }
                    _rxLen = 0;
                } else if (c != '\r') {
                    _rxBuf[_rxLen++] = c;
                }
            }
        } else if (_clients[i]) {
            /* 客户端断开 */
            _clients[i].stop();
        }
    }

    if (count != _clientCount) {
        DBG_PRINTF("[WIFI] client count: %u -> %u\n", _clientCount, count);
        _clientCount = count;
    }
}

/* handleCommand - 处理一行JSON命令 */
void WifiManager::handleCommand(const char* line, size_t len)
{
    DBG_PRINTF("[WIFI] cmd: %s\n", line);

    /* 简单关键字匹配命令类型 */
    AppCommand_t cmd = CMD_QUERY_STATUS;
    if (strstr(line, "set_mode"))        cmd = CMD_SET_MODE;
    else if (strstr(line, "set_warn"))   cmd = CMD_SET_WARN;
    else if (strstr(line, "set_buzzer")) cmd = CMD_SET_BUZZER;
    else if (strstr(line, "reboot"))     cmd = CMD_REBOOT;
    else if (strstr(line, "calibrate"))  cmd = CMD_CALIBRATE;
    else if (strstr(line, "query"))      cmd = CMD_QUERY_STATUS;

    /* 回调上层处理 */
    if (_cmdCb) {
        _cmdCb(cmd, line, len);
    }
}

/* isClientConnected - 是否有客户端连接 */
bool WifiManager::isClientConnected() const
{
    return _clientCount > 0;
}
