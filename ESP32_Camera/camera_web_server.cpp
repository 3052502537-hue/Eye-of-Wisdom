/* ============================================================
 *  文件名: camera_web_server.cpp
 *  功能描述:
 *    导盲头环项目 - 摄像板 HTTP 服务器实现 v1.0
 *    实现 MJPEG 视频流推送、单帧捕获、状态查询
 *    手机App直连摄像板获取图像，不经主控板中转
 *
 *  MJPEG 流格式:
 *    HTTP/1.1 200 OK
 *    Content-Type: multipart/x-mixed-replace; boundary=frame
 *
 *    --frame\r\n
 *    Content-Type: image/jpeg\r\n
 *    Content-Length: <N>\r\n
 *    \r\n
 *    <N bytes JPEG>\r\n
 *
 *  依赖关系:
 *    - camera_web_server.h (本文件头文件)
 *    - config.h (配置宏定义)
 *    - WiFi.h (网络)
 *    - esp_camera.h (帧缓冲)
 *
 *  接口说明:
 *    本文件实现 camera_web_server.h 中声明的所有方法
 * ============================================================ */

#include "camera_web_server.h"
#include "config.h"
#include <string.h>
#include <stdio.h>

/* 构造函数 */
CameraWebServer::CameraWebServer()
    : _server(nullptr), _serverRunning(false),
      _frameData(nullptr), _frameSize(0),
      _frameWidth(0), _frameHeight(0),
      _frameMutex(nullptr),
      _frameCount(0), _captureCount(0)
{
    /* 初始化MJPEG客户端数组 */
    for (int i = 0; i < MJPEG_MAX_CLIENTS; i++) {
        _mjpegClients[i] = WiFiClient();
    }

    /* 分配帧缓存 */
    _frameData = (uint8_t*)malloc(JPEG_MAX_SIZE);
    if (_frameData) {
        memset(_frameData, 0, JPEG_MAX_SIZE);
    }
}

/* 析构函数 */
CameraWebServer::~CameraWebServer()
{
    stop();
    if (_frameData) {
        free(_frameData);
        _frameData = nullptr;
    }
    if (_frameMutex) {
        vSemaphoreDelete(_frameMutex);
        _frameMutex = nullptr;
    }
}

/* begin - 启动HTTP服务器 */
bool CameraWebServer::begin(uint16_t port)
{
    if (_serverRunning) return true;

    /* 创建帧互斥锁 */
    _frameMutex = xSemaphoreCreateMutex();
    if (!_frameMutex) {
        DBG_PRINTLN("[HTTP] mutex create FAIL");
        return false;
    }

    /* 启动WiFi服务器 */
    _server = new WiFiServer(port);
    if (!_server) {
        DBG_PRINTLN("[HTTP] server create FAIL");
        return false;
    }

    _server->begin();
    _serverRunning = true;

    DBG_PRINTF("[HTTP] server started on port %d\n", port);
    DBG_PRINTF("[HTTP] endpoints: /video /capture /status\n");
    return true;
}

/* stop - 停止服务器 */
void CameraWebServer::stop()
{
    _serverRunning = false;

    /* 断开所有MJPEG客户端 */
    for (int i = 0; i < MJPEG_MAX_CLIENTS; i++) {
        if (_mjpegClients[i] && _mjpegClients[i].connected()) {
            _mjpegClients[i].stop();
        }
    }

    if (_server) {
        _server->stop();
        delete _server;
        _server = nullptr;
    }

    DBG_PRINTLN("[HTTP] server stopped");
}

/* updateFrame - 更新最新帧 (由采集任务调用, 线程安全) */
void CameraWebServer::updateFrame(const uint8_t* data, size_t size,
                                   uint16_t width, uint16_t height)
{
    if (!data || size == 0 || !_frameData || !_frameMutex) return;

    if (xSemaphoreTake(_frameMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        size_t copySize = (size > JPEG_MAX_SIZE) ? JPEG_MAX_SIZE : size;
        memcpy(_frameData, data, copySize);
        _frameSize = copySize;
        _frameWidth = width;
        _frameHeight = height;
        xSemaphoreGive(_frameMutex);
    }
}

/* handleClients - 处理HTTP请求 (周期调用) */
void CameraWebServer::handleClients()
{
    if (!_serverRunning || !_server) return;

    /* 1. 接受新客户端连接 */
    WiFiClient newClient = _server->accept();
    if (newClient) {
        DBG_PRINTF("[HTTP] new client: %s\n",
                   newClient.remoteIP().toString().c_str());

        /* 读取HTTP请求行(超时1秒) */
        uint32_t timeout = millis() + 1000;
        char request[256] = {0};
        int idx = 0;

        while (millis() < timeout && idx < (int)sizeof(request) - 1) {
            if (newClient.available()) {
                char c = newClient.read();
                request[idx++] = c;
                /* 读到空行(\r\n\r\n)或单\n\n时结束请求头 */
                if (idx >= 4 && request[idx-4] == '\r' && request[idx-3] == '\n'
                            && request[idx-2] == '\r' && request[idx-1] == '\n') {
                    break;
                }
                if (idx >= 2 && request[idx-2] == '\n' && request[idx-1] == '\n') {
                    break;
                }
            } else {
                delay(1);
            }
        }
        request[idx] = '\0';

        /* 解析并处理请求 */
        if (idx > 0) {
            handleRequest(newClient, request);
        }

        /* 非MJPEG客户端在此关闭 */
        newClient.stop();
    }

    /* 2. 向所有MJPEG客户端推送当前帧 */
    /* 先复制帧数据(加锁, 最小锁时间) */
    uint8_t frameCopy[JPEG_MAX_SIZE];
    size_t frameSize = 0;

    if (_frameMutex && xSemaphoreTake(_frameMutex, pdMS_TO_TICKS(5)) == pdTRUE) {
        if (_frameSize > 0 && _frameSize <= JPEG_MAX_SIZE) {
            memcpy(frameCopy, _frameData, _frameSize);
            frameSize = _frameSize;
        }
        xSemaphoreGive(_frameMutex);
    }

    /* 推送帧到MJPEG客户端(无锁操作) */
    if (frameSize > 0) {
        for (int i = 0; i < MJPEG_MAX_CLIENTS; i++) {
            if (_mjpegClients[i] && _mjpegClients[i].connected()) {
                sendJpegFrame(_mjpegClients[i], frameCopy, frameSize);
                _frameCount++;
            }
        }
    }

    /* 3. 清理断开的MJPEG客户端 */
    for (int i = 0; i < MJPEG_MAX_CLIENTS; i++) {
        if (_mjpegClients[i] && !_mjpegClients[i].connected()) {
            _mjpegClients[i].stop();
            DBG_PRINTF("[HTTP] MJPEG client %d disconnected\n", i);
        }
    }
}

/* handleRequest - 路由HTTP请求 */
void CameraWebServer::handleRequest(WiFiClient& client, const char* request)
{
    char method[8] = {0};
    char path[64] = {0};

    if (!parseRequest(request, method, sizeof(method), path, sizeof(path))) {
        send404(client);
        return;
    }

    DBG_PRINTF("[HTTP] %s %s\n", method, path);

    /* 路由分发 */
    if (strcmp(path, "/video") == 0 || strcmp(path, "/stream") == 0) {
        /* MJPEG 视频流 — 保持连接，持续推送帧 */
        /* 找一个空闲的MJPEG客户端槽位 */
        int slot = -1;
        for (int i = 0; i < MJPEG_MAX_CLIENTS; i++) {
            if (!_mjpegClients[i] || !_mjpegClients[i].connected()) {
                slot = i;
                break;
            }
        }

        if (slot >= 0) {
            _mjpegClients[slot] = client;
            sendMjpegHeaders(_mjpegClients[slot]);
            DBG_PRINTF("[HTTP] MJPEG stream started for client %d\n", slot);
            /* 注意: 不在此处stop client, 由handleClients持续推送 */
        } else {
            /* 无可用槽位, 返回503 */
            client.println("HTTP/1.1 503 Service Unavailable");
            client.println("Content-Type: text/plain");
            client.println("Connection: close");
            client.println();
            client.println("Too many MJPEG clients");
            DBG_PRINTLN("[HTTP] MJPEG client rejected: max clients reached");
        }
        return;  // 不关闭连接

    } else if (strcmp(path, "/capture") == 0 || strcmp(path, "/snapshot") == 0) {
        /* 单帧JPEG捕获 — 用于AI视觉推理 */
        uint8_t snapBuf[JPEG_MAX_SIZE];
        size_t snapSize = 0;

        if (_frameMutex && xSemaphoreTake(_frameMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            if (_frameSize > 0 && _frameSize <= JPEG_MAX_SIZE) {
                memcpy(snapBuf, _frameData, _frameSize);
                snapSize = _frameSize;
            }
            xSemaphoreGive(_frameMutex);
        }

        if (snapSize > 0) {
            client.println("HTTP/1.1 200 OK");
            client.println("Content-Type: image/jpeg");
            client.printf("Content-Length: %d\r\n", (int)snapSize);
            client.println("Cache-Control: no-cache");
            client.println("Connection: close");
            client.println();
            client.write(snapBuf, snapSize);
            _captureCount++;
        } else {
            client.println("HTTP/1.1 503 Service Unavailable");
            client.println("Content-Type: text/plain");
            client.println("Connection: close");
            client.println();
            client.println("No frame available");
        }

    } else if (strcmp(path, "/status") == 0) {
        /* JSON状态 */
        sendStatusJson(client);

    } else if (strcmp(path, "/") == 0) {
        /* 首页 — 简单HTML */
        client.println("HTTP/1.1 200 OK");
        client.println("Content-Type: text/html; charset=utf-8");
        client.println("Connection: close");
        client.println();
        client.println("<!DOCTYPE html><html><head><meta charset='utf-8'>");
        client.println("<meta name='viewport' content='width=device-width,initial-scale=1'>");
        client.println("<title>导盲头环 - 摄像板</title>");
        client.println("<style>body{font-family:sans-serif;margin:20px;text-align:center}");
        client.println("img{max-width:100%;border:2px solid #333;border-radius:10px}</style>");
        client.println("</head><body>");
        client.println("<h1>📷 导盲头环摄像板</h1>");
        client.println("<p>OV2640 VGA JPEG | ESP32-S3-WROOM</p>");
        client.println("<p><a href='/video'>MJPEG视频流</a> | ");
        client.println("<a href='/capture'>单帧快照</a> | ");
        client.println("<a href='/status'>状态JSON</a></p>");
        client.println("<p><img src='/capture' alt='Camera Snapshot'></p>");
        client.println("<p><small>刷新页面获取新快照</small></p>");
        client.println("</body></html>");

    } else {
        send404(client);
    }
}

/* sendMjpegHeaders - 发送MJPEG流HTTP头 */
void CameraWebServer::sendMjpegHeaders(WiFiClient& client)
{
    client.println("HTTP/1.1 200 OK");
    client.printf("Content-Type: multipart/x-mixed-replace; boundary=%s\r\n",
                  MJPEG_BOUNDARY);
    client.println("Cache-Control: no-cache");
    client.println("Connection: close");
    client.println("Access-Control-Allow-Origin: *");
    client.println();
}

/* sendJpegFrame - 发送单帧JPEG (带MJPEG boundary) */
void CameraWebServer::sendJpegFrame(WiFiClient& client,
                                     const uint8_t* data, size_t size)
{
    if (!client || !client.connected() || !data || size == 0) return;

    char boundaryHeader[128];
    snprintf(boundaryHeader, sizeof(boundaryHeader),
             "--%s\r\n"
             "Content-Type: image/jpeg\r\n"
             "Content-Length: %u\r\n"
             "\r\n",
             MJPEG_BOUNDARY, (unsigned)size);

    client.write((const uint8_t*)boundaryHeader, strlen(boundaryHeader));
    client.write(data, size);
    client.write("\r\n", 2);
}

/* sendStatusJson - 发送JSON状态 */
void CameraWebServer::sendStatusJson(WiFiClient& client)
{
    size_t frameSize = 0;
    uint16_t w = 0, h = 0;

    if (_frameMutex && xSemaphoreTake(_frameMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        frameSize = _frameSize;
        w = _frameWidth;
        h = _frameHeight;
        xSemaphoreGive(_frameMutex);
    }

    char json[256];
    snprintf(json, sizeof(json),
        "{"
        "\"type\":\"camera_status\","
        "\"ip\":\"%s\","
        "\"resolution\":\"%ux%u\","
        "\"frame_size\":%u,"
        "\"frames_sent\":%lu,"
        "\"captures\":%lu,"
        "\"clients\":%d,"
        "\"uptime\":%lu"
        "}",
        WiFi.localIP().toString().c_str(),
        (unsigned)w, (unsigned)h,
        (unsigned)frameSize,
        (unsigned long)_frameCount,
        (unsigned long)_captureCount,
        getClientCount(),
        (unsigned long)millis()
    );

    client.println("HTTP/1.1 200 OK");
    client.println("Content-Type: application/json; charset=utf-8");
    client.println("Cache-Control: no-cache");
    client.println("Connection: close");
    client.println("Access-Control-Allow-Origin: *");
    client.printf("Content-Length: %d\r\n", (int)strlen(json));
    client.println();
    client.print(json);
}

/* send404 - 404响应 */
void CameraWebServer::send404(WiFiClient& client)
{
    client.println("HTTP/1.1 404 Not Found");
    client.println("Content-Type: text/plain");
    client.println("Connection: close");
    client.println();
    client.println("404: Not Found");
}

/* parseRequest - 解析HTTP请求行 */
bool CameraWebServer::parseRequest(const char* req, char* method, size_t methodSize,
                                    char* path, size_t pathSize)
{
    if (!req || !method || !path) return false;

    /* 解析: GET /path HTTP/1.1 */
    const char* p = req;

    /* 提取方法 */
    while (*p == ' ') p++;
    size_t i = 0;
    while (*p && *p != ' ' && i < methodSize - 1) {
        method[i++] = *p++;
    }
    method[i] = '\0';
    if (i == 0) return false;

    /* 跳过空格 */
    while (*p == ' ') p++;

    /* 提取路径 */
    i = 0;
    while (*p && *p != ' ' && *p != '\r' && *p != '\n' && i < pathSize - 1) {
        path[i++] = *p++;
    }
    path[i] = '\0';
    if (i == 0) return false;

    return true;
}

/* getClientCount - 获取MJPEG客户端数量 */
int CameraWebServer::getClientCount() const
{
    int count = 0;
    for (int i = 0; i < MJPEG_MAX_CLIENTS; i++) {
        if (_mjpegClients[i] && _mjpegClients[i].connected()) {
            count++;
        }
    }
    return count;
}
