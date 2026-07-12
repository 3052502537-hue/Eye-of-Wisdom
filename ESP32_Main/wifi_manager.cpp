/* ============================================================
 * 文件名: wifi_manager.cpp
 * 功能描述: WiFi 管理实现
 *           实现 AP 热点启动、TCP/UDP 服务器、客户端管理、
 *           传感器JSON广播、JPEG图像UDP分片发送、命令接收
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
      _apStarted(false), _tcpStarted(false), _udpStarted(false), _rxLen(0)
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
    if (_udpStarted) _udp.stop();
    if (_apStarted) WiFi.softAPdisconnect(true);
}

/* begin - 启动 AP + TCP + UDP */
bool WifiManager::begin()
{
    if (!startAP()) return false;
    startTcpServer();
    startUdpServer();
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

/* startUdpServer - 启动 UDP 服务器 */
void WifiManager::startUdpServer()
{
    if (_udpStarted) return;
    _udp.begin(UDP_PORT);
    _udpStarted = true;
    DBG_PRINTF("[WIFI] UDP server on port %d\n", UDP_PORT);
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

/* sendImageUdp - 发送JPEG图像(UDP分片) */
uint16_t WifiManager::sendImageUdp(const uint8_t* data, size_t size, uint32_t frameId)
{
    if (!_udpStarted || !data || size == 0) return 0;

    /* 计算分片数 */
    uint16_t sliceTotal = (uint16_t)((size + UDP_IMG_MAX_PAYLOAD - 1) / UDP_IMG_MAX_PAYLOAD);
    if (sliceTotal == 0) sliceTotal = 1;

    /* 获取第一个客户端的IP/端口(广播到AP子网) */
    IPAddress bcast = IPAddress(_apIp[0], _apIp[1], _apIp[2], 255);

    uint16_t sentSlices = 0;
    size_t offset = 0;
    for (uint16_t s = 0; s < sliceTotal; s++) {
        uint16_t chunkLen = UDP_IMG_MAX_PAYLOAD;
        if (offset + chunkLen > size) {
            chunkLen = (uint16_t)(size - offset);
        }

        /* 构造UDP包: 头部 + JPEG数据 */
        uint8_t packet[UDP_PACKET_MAX_SIZE];
        UdpImgHeader_t* hdr = (UdpImgHeader_t*)packet;
        hdr->frameId    = frameId;
        hdr->sliceIndex = s;
        hdr->sliceTotal = sliceTotal;
        hdr->dataLen    = chunkLen;
        memcpy(packet + UDP_IMG_HEADER_LEN, data + offset, chunkLen);

        /* 发送到广播地址(所有连接的App均可收到) */
        _udp.beginPacket(bcast, UDP_PORT);
        _udp.write(packet, UDP_IMG_HEADER_LEN + chunkLen);
        _udp.endPacket();

        offset += chunkLen;
        sentSlices++;
    }

    DBG_PRINTF("[WIFI] img sent: frame=%lu size=%lu slices=%u\n",
               (unsigned long)frameId, (unsigned long)size, sentSlices);
    return sentSlices;
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
    else if (strstr(line, "set_img"))    cmd = CMD_SET_IMG;
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
