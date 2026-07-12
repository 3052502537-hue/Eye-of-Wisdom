/* ============================================================
 * 文件名: web_server.h
 * 功能描述: Web 配置页面头文件
 *           提供 HTTP 页面用于: 参数配置(预警阈值/图像参数)、
 *           状态查看(传感器数据/系统状态)、传感器校准
 *           手机连接AP后浏览器访问 http://192.168.4.1 使用
 * 依赖关系: Arduino WebServer 库、config.h、protocol.h
 * 接口说明:
 *   WebServerManager()      - 构造函数
 *   begin()                 - 启动Web服务器并注册路由
 *   handleClient()          - 处理HTTP请求(周期调用)
 *   updateSensorData()      - 更新页面展示的传感器数据
 *   getWarnParams()         - 获取当前预警阈值
 *   getImgParams()          - 获取当前图像参数
 *
 * 路由:
 *   GET /            -> 首页(状态总览)
 *   GET /config      -> 参数配置页
 *   GET /status      -> 状态JSON
 *   GET /calibrate   -> 校准页
 *   POST /api/warn   -> 设置预警阈值
 *   POST /api/img    -> 设置图像参数
 *   POST /api/calib  -> 触发校准
 *   POST /api/reboot -> 重启
 * ============================================================ */

#ifndef WEB_SERVER_H
#define WEB_SERVER_H

#include <Arduino.h>
#include <WebServer.h>
#include "config.h"
#include "protocol.h"

/* 预警阈值参数（可被Web页面修改） */
typedef struct {
    float attention;    // 注意距离(m)
    float warning;      // 警告距离(m)
    float danger;       // 危险距离(m)
} WarnParams_t;

/* 图像参数（可被Web页面修改） */
typedef struct {
    uint8_t  resolution;   // 0=QVGA 1=VGA
    uint8_t  fps;          // 帧率
    uint8_t  quality;      // JPEG质量
} ImgParams_t;

class WebServerManager {
public:
    WebServerManager();
    ~WebServerManager();

    /* --------------------------------------------------------
     * begin - 启动Web服务器并注册路由
     * -------------------------------------------------------- */
    void begin();

    /* --------------------------------------------------------
     * handleClient - 处理HTTP请求
     * 说明: 应在Web任务中周期调用
     * -------------------------------------------------------- */
    void handleClient();

    /* --------------------------------------------------------
     * updateSensorData - 更新页面展示的最新传感器数据
     * 参数: frame - 传感器数据帧
     * -------------------------------------------------------- */
    void updateSensorData(const SensorFrame_t* frame);

    /* 获取/设置参数 */
    const WarnParams_t& getWarnParams() const { return _warn; }
    const ImgParams_t&  getImgParams()  const { return _img; }

    /* 校准请求标志(主控检测后执行校准) */
    bool isCalibrateRequested() const { return _calibReq; }
    void clearCalibrateRequest() { _calibReq = false; }

    /* 重启请求标志 */
    bool isRebootRequested() const { return _rebootReq; }
    void clearRebootRequest() { _rebootReq = false; }

private:
    /* 路由处理函数 */
    void handleRoot();          // 首页
    void handleConfig();        // 配置页
    void handleStatus();        // 状态JSON
    void handleCalibrate();     // 校准页
    void handleApiWarn();       // 设置预警阈值
    void handleApiImg();        // 设置图像参数
    void handleApiCalib();      // 触发校准
    void handleApiReboot();     // 重启
    void handleNotFound();      // 404

    WebServer* _server;         // WebServer实例
    WarnParams_t _warn;         // 预警阈值
    ImgParams_t  _img;          // 图像参数
    SensorFrame_t _sensor;      // 最新传感器数据(供页面展示)
    bool _calibReq;             // 校准请求标志
    bool _rebootReq;            // 重启请求标志
};

#endif /* WEB_SERVER_H */
