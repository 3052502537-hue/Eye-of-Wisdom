/* ============================================================
 * 文件名: web_server.cpp
 * 功能描述: Web 配置页面实现
 *           实现 HTTP 路由、HTML 页面生成、参数解析与保存、
 *           状态JSON输出
 * 依赖关系: Arduino WebServer 库、config.h、protocol.h、web_server.h
 * 接口说明: 见头文件
 * ============================================================ */

#include "web_server.h"

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
WebServerManager::WebServerManager()
    : _server(nullptr), _calibReq(false), _rebootReq(false)
{
    /* 默认参数取自 config.h */
    _warn.attention = WARN_DISTANCE_ATTENTION;
    _warn.warning   = WARN_DISTANCE_WARNING;
    _warn.danger    = WARN_DISTANCE_DANGER;
    _img.resolution = RESOLUTION_QVGA;
    _img.fps        = 15;
    _img.quality    = IMG_JPEG_QUALITY;
    memset(&_sensor, 0, sizeof(_sensor));
}

/* 析构函数 */
WebServerManager::~WebServerManager()
{
    if (_server) {
        _server->stop();
        delete _server;
        _server = nullptr;
    }
}

/* begin - 启动Web服务器 */
void WebServerManager::begin()
{
    _server = new WebServer(WEB_SERVER_PORT);

    /* 注册路由 */
    _server->on("/",              HTTP_GET,  [this](){ handleRoot(); });
    _server->on("/config",        HTTP_GET,  [this](){ handleConfig(); });
    _server->on("/status",        HTTP_GET,  [this](){ handleStatus(); });
    _server->on("/calibrate",     HTTP_GET,  [this](){ handleCalibrate(); });
    _server->on("/api/warn",      HTTP_POST, [this](){ handleApiWarn(); });
    _server->on("/api/img",       HTTP_POST, [this](){ handleApiImg(); });
    _server->on("/api/calib",     HTTP_POST, [this](){ handleApiCalib(); });
    _server->on("/api/reboot",    HTTP_POST, [this](){ handleApiReboot(); });
    _server->onNotFound([this](){ handleNotFound(); });

    _server->begin();
    DBG_PRINTF("[WEB] server on port %d\n", WEB_SERVER_PORT);
}

/* handleClient - 处理请求 */
void WebServerManager::handleClient()
{
    if (_server) _server->handleClient();
}

/* updateSensorData - 更新传感器数据 */
void WebServerManager::updateSensorData(const SensorFrame_t* frame)
{
    if (frame) memcpy(&_sensor, frame, sizeof(_sensor));
}

/* ============================================================
 * 页面处理函数
 * ============================================================ */

/* handleRoot - 首页(状态总览) */
void WebServerManager::handleRoot()
{
    String html = F(
        "<!DOCTYPE html><html><head><meta charset='utf-8'>"
        "<meta name='viewport' content='width=device-width,initial-scale=1'>"
        "<title>导盲头环</title>"
        "<style>body{font-family:sans-serif;margin:20px;background:#f0f0f0}"
        "h1{color:#333}a{display:inline-block;margin:10px;padding:10px 20px;"
        "background:#4CAF50;color:#fff;text-decoration:none;border-radius:5px}</style>"
        "</head><body>"
        "<h1>智慧之眼导盲头环</h1>"
        "<p>主控板: ESP32-S3-N16R8</p>"
        "<p>激光距离: ");
    html += String(_sensor.laser.distance, 2);
    html += F(" m</p><p>危险等级: ");
    html += String(_sensor.level);
    html += F("</p>"
        "<a href='/config'>参数配置</a>"
        "<a href='/status'>状态JSON</a>"
        "<a href='/calibrate'>传感器校准</a>"
        "</body></html>");
    _server->send(200, "text/html", html);
}

/* handleConfig - 配置页 */
void WebServerManager::handleConfig()
{
    String html = F(
        "<!DOCTYPE html><html><head><meta charset='utf-8'>"
        "<meta name='viewport' content='width=device-width,initial-scale=1'>"
        "<title>参数配置</title>"
        "<style>body{font-family:sans-serif;margin:20px}"
        "input{display:block;margin:8px 0;padding:6px;width:200px}"
        "button{padding:10px 20px;background:#4CAF50;color:#fff;border:none;border-radius:5px}</style>"
        "</head><body><h2>参数配置</h2>"
        "<h3>预警阈值(m)</h3>"
        "<form action='/api/warn' method='post'>"
        "注意距离: <input name='att' value='");
    html += String(_warn.attention, 2);
    html += F("'>警告距离: <input name='war' value='");
    html += String(_warn.warning, 2);
    html += F("'>危险距离: <input name='dan' value='");
    html += String(_warn.danger, 2);
    html += F("'><button type='submit'>保存</button></form>"
        "<h3>图像参数</h3>"
        "<form action='/api/img' method='post'>"
        "分辨率(0=QVGA,1=VGA): <input name='res' value='");
    html += String(_img.resolution);
    html += F("'>帧率: <input name='fps' value='");
    html += String(_img.fps);
    html += F("'>JPEG质量: <input name='q' value='");
    html += String(_img.quality);
    html += F("'><button type='submit'>保存</button></form>"
        "<p><a href='/'>返回首页</a></p>"
        "</body></html>");
    _server->send(200, "text/html", html);
}

/* handleStatus - 状态JSON */
void WebServerManager::handleStatus()
{
    String json = "{";
    json += "\"type\":\"status\",";
    json += "\"laser\":{\"dist\":" + String(_sensor.laser.distance, 2) +
            ",\"valid\":" + String(_sensor.laser.valid) + "},";
    json += "\"ultrasonic\":{\"dist\":" + String(_sensor.ultrasonicDist, 2) + "},";
    json += "\"camera_ip\":\"" + String(CAMERA_ESP32_STATIC_IP) + "\",";
    json += "\"level\":" + String(_sensor.level) + ",";
    json += "\"warn\":{\"att\":" + String(_warn.attention, 2) +
            ",\"war\":" + String(_warn.warning, 2) +
            ",\"dan\":" + String(_warn.danger, 2) + "},";
    json += "\"img\":{\"res\":" + String(_img.resolution) +
            ",\"fps\":" + String(_img.fps) +
            ",\"q\":" + String(_img.quality) + "}";
    json += "}";
    _server->send(200, "application/json", json);
}

/* handleCalibrate - 校准页 */
void WebServerManager::handleCalibrate()
{
    String html = F(
        "<!DOCTYPE html><html><head><meta charset='utf-8'>"
        "<meta name='viewport' content='width=device-width,initial-scale=1'>"
        "<title>传感器校准</title>"
        "<style>body{font-family:sans-serif;margin:20px}"
        "button{padding:10px 20px;background:#FF9800;color:#fff;border:none;border-radius:5px}</style>"
        "</head><body><h2>传感器校准</h2>"
        "<p>点击下方按钮触发传感器校准流程</p>"
        "<form action='/api/calib' method='post'>"
        "<button type='submit'>开始校准</button></form>"
        "<p><a href='/'>返回首页</a></p>"
        "</body></html>");
    _server->send(200, "text/html", html);
}

/* handleApiWarn - 设置预警阈值 */
void WebServerManager::handleApiWarn()
{
    if (_server->hasArg("att")) _warn.attention = _server->arg("att").toFloat();
    if (_server->hasArg("war")) _warn.warning   = _server->arg("war").toFloat();
    if (_server->hasArg("dan")) _warn.danger    = _server->arg("dan").toFloat();
    DBG_PRINTF("[WEB] warn set: att=%.2f war=%.2f dan=%.2f\n",
               _warn.attention, _warn.warning, _warn.danger);
    _server->send(200, "text/plain", "OK");
}

/* handleApiImg - 设置图像参数 */
void WebServerManager::handleApiImg()
{
    if (_server->hasArg("res")) _img.resolution = (uint8_t)_server->arg("res").toInt();
    if (_server->hasArg("fps")) _img.fps        = (uint8_t)_server->arg("fps").toInt();
    if (_server->hasArg("q"))   _img.quality    = (uint8_t)_server->arg("q").toInt();
    DBG_PRINTF("[WEB] img set: res=%u fps=%u q=%u\n",
               _img.resolution, _img.fps, _img.quality);
    _server->send(200, "text/plain", "OK");
}

/* handleApiCalib - 触发校准 */
void WebServerManager::handleApiCalib()
{
    _calibReq = true;
    DBG_PRINTLN("[WEB] calibrate requested");
    _server->send(200, "text/plain", "校准已触发");
}

/* handleApiReboot - 重启 */
void WebServerManager::handleApiReboot()
{
    _rebootReq = true;
    DBG_PRINTLN("[WEB] reboot requested");
    _server->send(200, "text/plain", "即将重启");
}

/* handleNotFound - 404 */
void WebServerManager::handleNotFound()
{
    _server->send(404, "text/plain", "404: Not Found");
}
