/* ============================================================
 * 文件名: wifi_manager.h
 * 功能描述: WiFi 管理头文件
 *           ESP32 作为 AP 热点，手机连接 ESP32
 *           - TCP 服务器: 传输传感器 JSON 数据 + 接收手机控制命令
 *           - UDP 服务器: 发送图像 JPEG 二进制数据(分片)
 *           - 客户端连接管理
 * 依赖关系: Arduino WiFi 库、config.h、protocol.h
 * 接口说明:
 *   WifiManager()            - 构造函数
 *   begin()                  - 启动AP+TCP服务器+UDP服务器
 *   startAP()                - 仅启动AP热点
 *   startTcpServer()         - 启动TCP服务器
 *   startUdpServer()         - 启动UDP服务器
 *   sendSensorJson()         - 向所有TCP客户端发送传感器JSON
 *   sendImageUdp()           - 向所有UDP客户端发送JPEG图像(分片)
 *   processTcpClients()      - 处理TCP客户端连接与命令接收
 *   isClientConnected()      - 查询是否有客户端连接
 *   getApIp()                - 获取AP的IP地址
 * ============================================================ */

#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiAP.h>
#include "config.h"
#include "protocol.h"

/* 命令回调函数类型: 收到App命令时回调主控 */
typedef void (*CommandCallback)(AppCommand_t cmd, const char* json, size_t len);

class WifiManager {
public:
    WifiManager();
    ~WifiManager();

    /* --------------------------------------------------------
     * begin - 启动 AP 热点 + TCP/UDP 服务器
     * 参数: 无
     * 返回: true=成功
     * -------------------------------------------------------- */
    bool begin();

    /* --------------------------------------------------------
     * startAP - 启动 AP 热点
     * 返回: true=成功
     * -------------------------------------------------------- */
    bool startAP();

    /* --------------------------------------------------------
     * startTcpServer - 启动 TCP 服务器
     * -------------------------------------------------------- */
    void startTcpServer();

    /* --------------------------------------------------------
     * startUdpServer - 启动 UDP 服务器
     * -------------------------------------------------------- */
    void startUdpServer();

    /* --------------------------------------------------------
     * sendSensorJson - 向所有TCP客户端发送传感器JSON字符串
     * 参数: jsonStr - JSON字符串, len - 长度
     * 返回: 实际发送的客户端数
     * -------------------------------------------------------- */
    uint8_t sendSensorJson(const char* jsonStr, size_t len);

    /* --------------------------------------------------------
     * sendImageUdp - 向所有UDP客户端发送JPEG图像(自动分片)
     * 参数: data     - JPEG数据指针
     *       size     - JPEG字节数
     *       frameId  - 帧ID(用于接收端重组)
     * 返回: 发送分片总数
     * -------------------------------------------------------- */
    uint16_t sendImageUdp(const uint8_t* data, size_t size, uint32_t frameId);

    /* --------------------------------------------------------
     * processTcpClients - 处理TCP客户端连接与命令接收
     * 参数: 无
     * 返回: 无
     * 说明: 应周期调用; 接受新连接/清理断开/读取命令并回调
     * -------------------------------------------------------- */
    void processTcpClients();

    /* --------------------------------------------------------
     * setCommandCallback - 注册命令回调
     * 参数: cb - 回调函数指针
     * -------------------------------------------------------- */
    void setCommandCallback(CommandCallback cb) { _cmdCb = cb; }

    /* --------------------------------------------------------
     * isClientConnected - 是否有TCP客户端连接
     * -------------------------------------------------------- */
    bool isClientConnected() const;

    /* --------------------------------------------------------
     * getApIp - 获取AP的IP地址(默认192.168.4.1)
     * -------------------------------------------------------- */
    IPAddress getApIp() const { return _apIp; }

    /* 获取连接客户端数量 */
    uint8_t getClientCount() const { return _clientCount; }

private:
    /* 处理单行JSON命令 */
    void handleCommand(const char* line, size_t len);

    WiFiServer* _tcpServer;                 // TCP服务器
    WiFiClient  _clients[TCP_MAX_CLIENTS];  // TCP客户端数组
    WiFiUDP     _udp;                       // UDP实例
    IPAddress   _apIp;                      // AP IP地址
    uint8_t     _clientCount;               // 当前连接数
    CommandCallback _cmdCb;                 // 命令回调
    bool        _apStarted;                 // AP已启动
    bool        _tcpStarted;                // TCP已启动
    bool        _udpStarted;                // UDP已启动

    /* TCP接收行缓冲(命令以'\n'分隔) */
    char     _rxBuf[TCP_SEND_BUFFER_SIZE];
    size_t   _rxLen;
};

#endif /* WIFI_MANAGER_H */
