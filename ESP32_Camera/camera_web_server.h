/* ============================================================
 *  文件名: camera_web_server.h
 *  功能描述:
 *    导盲头环项目 - 摄像板 HTTP 服务器头文件 v1.0
 *    提供 MJPEG 视频流、单帧捕获、状态查询等HTTP端点
 *    手机App通过WiFi直接连接摄像板拉取图像数据
 *
 *  端点:
 *    GET /video    - MJPEG multipart/x-mixed-replace 视频流
 *    GET /capture  - 单帧 JPEG 图像 (用于AI推理快照)
 *    GET /status   - JSON 状态信息
 *
 *  依赖关系:
 *    - config.h (引脚和参数定义)
 *    - WiFi.h / WiFiClient.h (网络)
 *    - esp_camera.h (摄像头帧缓冲)
 *    - FreeRTOS (任务与队列)
 *
 *  接口说明:
 *    CameraWebServer 类提供以下接口:
 *      begin()           - 启动HTTP服务器
 *      updateFrame()     - 更新最新帧数据(由采集任务调用)
 *      handleClients()   - 处理HTTP请求(周期调用)
 *      getClientCount()  - 获取当前MJPEG客户端数
 * ============================================================ */

#ifndef CAMERA_WEB_SERVER_H
#define CAMERA_WEB_SERVER_H

#include <stdint.h>
#include <stdbool.h>
#include "Arduino.h"
#include <WiFi.h>
#include <WiFiClient.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

/* MJPEG 客户端最大数量 */
#define MJPEG_MAX_CLIENTS  3

/**
 * MJPEG HTTP 服务器类
 * 封装 HTTP 服务器、MJPEG 流推送、帧缓存管理
 */
class CameraWebServer {
public:
    CameraWebServer();
    ~CameraWebServer();

    /* --------------------------------------------------------
     *  begin - 启动HTTP服务器
     *  参数: port - 监听端口 (默认80)
     *  返回: true=成功, false=失败
     * -------------------------------------------------------- */
    bool begin(uint16_t port = 80);

    /* --------------------------------------------------------
     *  updateFrame - 更新最新帧数据
     *  参数: data   - JPEG数据指针
     *        size   - 数据字节数
     *        width  - 图像宽度
     *        height - 图像高度
     *  说明: 由摄像头采集任务每次采集完后调用
     * -------------------------------------------------------- */
    void updateFrame(const uint8_t* data, size_t size, uint16_t width, uint16_t height);

    /* --------------------------------------------------------
     *  handleClients - 处理HTTP请求 (周期调用)
     *  说明: 接受新连接、推送MJPEG流、处理/capture和/status请求
     *        应在HTTP服务任务中周期调用
     * -------------------------------------------------------- */
    void handleClients();

    /* 获取当前MJPEG客户端数量 */
    int getClientCount() const;

    /* 获取累计发送帧数(调试用) */
    uint32_t getFrameCount() const { return _frameCount; }

    /* 获取服务器运行状态 */
    bool isRunning() const { return _serverRunning; }

    /* 停止服务器 */
    void stop();

private:
    /* 处理单个HTTP请求 */
    void handleRequest(WiFiClient& client, const char* request);

    /* 发送MJPEG流响应头 */
    void sendMjpegHeaders(WiFiClient& client);

    /* 发送单帧JPEG */
    void sendJpegFrame(WiFiClient& client, const uint8_t* data, size_t size);

    /* 发送JSON状态 */
    void sendStatusJson(WiFiClient& client);

    /* 发送404响应 */
    void send404(WiFiClient& client);

    /* 发送MJPEG边界分隔符 */
    void sendMjpegBoundary(WiFiClient& client);

    /* 解析HTTP请求行 */
    bool parseRequest(const char* req, char* method, size_t methodSize,
                      char* path, size_t pathSize);

    WiFiServer* _server;                // WiFi服务器实例
    bool        _serverRunning;

    /* MJPEG 客户端管理 */
    WiFiClient  _mjpegClients[MJPEG_MAX_CLIENTS];

    /* 帧缓存 (MJPEG流共享) */
    uint8_t*    _frameData;             // JPEG帧数据缓存
    size_t      _frameSize;             // 当前帧大小
    uint16_t    _frameWidth;            // 帧宽度
    uint16_t    _frameHeight;           // 帧高度
    SemaphoreHandle_t _frameMutex;      // 帧缓存互斥锁

    /* 统计 */
    uint32_t    _frameCount;            // 累计发送帧数
    uint32_t    _captureCount;          // 累计单帧请求数
};

#endif /* CAMERA_WEB_SERVER_H */
