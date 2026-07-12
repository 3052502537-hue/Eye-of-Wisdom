/* ============================================================
 *  文件名: camera_driver.cpp
 *  功能描述:
 *    导盲头环项目 - OV2640 摄像头驱动实现文件
 *    实现 CameraDriver 类的所有方法，包括:
 *    - 摄像头硬件初始化 (DVP引脚、VGA、JPEG、PSRAM双缓冲)
 *    - 图像采集与帧缓冲区管理
 *    - 图像参数动态调整 (分辨率、质量、亮度、对比度等)
 *    - 图像翻转设置 (适配头环安装方向)
 *
 *  依赖关系:
 *    - camera_driver.h (本文件头文件)
 *    - config.h (引脚和参数宏定义)
 *    - esp_camera.h (ESP32 官方摄像头库)
 *    - Arduino.h (Arduino 核心，Serial 等)
 *
 *  接口说明:
 *    本文件实现 camera_driver.h 中声明的所有方法
 *    外部通过 CameraDriver 类对象调用
 *
 *  采集流程:
 *    1. init() 初始化摄像头硬件
 *    2. capture() 采集一帧 JPEG 图像
 *    3. 处理图像数据 (fb->buf, fb->len)
 *    4. returnFrame() 释放缓冲区
 *    5. 循环步骤 2-4
 * ============================================================ */

#include "camera_driver.h"
#include "config.h"

/* ============================================================
 *  构造函数: 初始化成员变量
 * ============================================================ */
CameraDriver::CameraDriver()
{
    _initialized  = false;              /* 初始化状态: 未初始化 */
    _width        = CAMERA_FRAME_WIDTH; /* 默认宽度 */
    _height       = CAMERA_FRAME_HEIGHT;/* 默认高度 */
    _pixelFormat  = PIXFORMAT_JPEG;     /* 默认像素格式: JPEG */
    _frameCount   = 0;                  /* 帧计数器清零 */
}

/* ============================================================
 *  析构函数: 释放摄像头资源
 * ============================================================ */
CameraDriver::~CameraDriver()
{
    if (_initialized) {
        esp_camera_deinit();            /* 反初始化摄像头 */
        _initialized = false;
    }
}

/* ============================================================
 *  初始化 OV2640 摄像头
 *  配置 DVP 引脚、VGA 分辨率、JPEG 输出、PSRAM 双缓冲
 * ============================================================ */
bool CameraDriver::init(void)
{
    if (_initialized) {
        DBG_PRINTLN("[Camera] 已初始化，跳过");
        return true;                    /* 避免重复初始化 */
    }

    DBG_PRINTLN("[Camera] 开始初始化 OV2640...");

    /* 调用内部函数配置硬件参数 */
    esp_err_t ret = _configHardware();
    if (ret != ESP_OK) {
        DBG_PRINTF("[Camera] 硬件配置失败: 0x%x\n", ret);
        return false;
    }

    _initialized = true;
    DBG_PRINTF("[Camera] 初始化成功: %dx%d, JPEG, 质量=%d\n",
               _width, _height, CAMERA_JPEG_QUALITY);
    return true;
}

/* ============================================================
 *  内部: 配置摄像头硬件参数
 *  填充 camera_config_t 结构并调用 esp_camera_init()
 * ============================================================ */
esp_err_t CameraDriver::_configHardware(void)
{
    camera_config_t config;

    /* --- DVP 引脚配置 (来自 config.h) --- */
    config.pin_pwdn     = PIN_CAMERA_PWDN;    /* 掉电控制引脚 */
    config.pin_reset    = PIN_CAMERA_RESET;   /* 复位引脚 */
    config.pin_xclk     = PIN_CAMERA_XCLK;    /* 主时钟输出引脚 */
    config.pin_sccb_sda = PIN_CAMERA_SIOD;    /* SCCB 数据线 (I2C SDA) */
    config.pin_sccb_scl = PIN_CAMERA_SIOC;    /* SCCB 时钟线 (I2C SCL) */

    /* 8位数据总线引脚 (D0-D7) */
    config.pin_d7       = PIN_CAMERA_D7;
    config.pin_d6       = PIN_CAMERA_D6;
    config.pin_d5       = PIN_CAMERA_D5;
    config.pin_d4       = PIN_CAMERA_D4;
    config.pin_d3       = PIN_CAMERA_D3;
    config.pin_d2       = PIN_CAMERA_D2;
    config.pin_d1       = PIN_CAMERA_D1;
    config.pin_d0       = PIN_CAMERA_D0;

    /* 同步信号引脚 */
    config.pin_vsync    = PIN_CAMERA_VSYNC;   /* 帧同步 */
    config.pin_href     = PIN_CAMERA_HREF;    /* 行同步 */
    config.pin_pclk     = PIN_CAMERA_PCLK;    /* 像素时钟 */

    /* --- 摄像头参数配置 --- */
    config.xclk_freq_hz = CAMERA_XCLK_FREQ;   /* 主时钟频率 20MHz */
    config.ledc_timer   = LEDC_TIMER_0;       /* LEDC 定时器 (用于生成 XCLK) */
    config.ledc_channel = LEDC_CHANNEL_0;     /* LEDC 通道 */
    config.pixel_format = PIXFORMAT_JPEG;     /* 像素格式: JPEG 压缩 */
    config.frame_size   = FRAMESIZE_VGA;      /* 帧尺寸: VGA 640x480 */
    config.jpeg_quality = CAMERA_JPEG_QUALITY;/* JPEG 质量 */
    config.fb_count     = CAMERA_FB_COUNT;    /* 帧缓冲数量: 2 (双缓冲) */
    config.fb_location  = CAMERA_FB_IN_PSRAM; /* 帧缓冲存放于 PSRAM */
    config.grab_mode    = CAMERA_GRAB_LATEST; /* 抓取模式: 总是取最新帧 */

    /* 调用 ESP32 官方库初始化摄像头 */
    esp_err_t ret = esp_camera_init(&config);
    if (ret != ESP_OK) {
        DBG_PRINTF("[Camera] esp_camera_init 失败: %s\n", esp_err_to_name(ret));
        return ret;
    }

    /* 获取传感器句柄，设置默认参数 */
    sensor_t* s = esp_camera_sensor_get();
    if (s) {
        s->set_brightness(s, 0);   /* 亮度: 0 (默认) */
        s->set_contrast(s, 0);     /* 对比度: 0 (默认) */
        s->set_saturation(s, 0);   /* 饱和度: 0 (默认) */
        /* 导盲头环可能需要根据安装方向翻转图像，此处先设为正常 */
        // s->set_hmirror(s, 1);   /* 水平翻转 (1=开启) */
        // s->set_vflip(s, 1);     /* 垂直翻转 (1=开启) */
    }

    _width       = CAMERA_FRAME_WIDTH;
    _height      = CAMERA_FRAME_HEIGHT;
    _pixelFormat = PIXFORMAT_JPEG;

    return ESP_OK;
}

/* ============================================================
 *  采集一帧图像
 *  调用 esp_camera_fb_get() 获取帧缓冲区
 * ============================================================ */
bool CameraDriver::capture(camera_fb_t** fb)
{
    /* 检查初始化状态和参数有效性 */
    if (!_initialized || !fb) {
        return false;
    }

    /* 从摄像头获取一帧 */
    *fb = esp_camera_fb_get();
    if (!*fb) {
        DBG_PRINTLN("[Camera] 采集失败: 无法获取帧缓冲区");
        return false;
    }

    /* 校验数据有效性 */
    if ((*fb)->len == 0 || !(*fb)->buf) {
        DBG_PRINTLN("[Camera] 采集失败: 帧数据为空");
        esp_camera_fb_return(*fb);
        *fb = NULL;
        return false;
    }

    _frameCount++;      /* 帧计数递增 */

#ifdef DEBUG
    /* 调试模式下打印帧信息 */
    DBG_PRINTF("[Camera] 采集成功 #%lu: %dx%d, %u字节, 格式=%d\n",
               (unsigned long)_frameCount,
               (*fb)->width, (*fb)->height,
               (unsigned)(*fb)->len,
               (*fb)->format);
#endif

    return true;
}

/* ============================================================
 *  释放帧缓冲区
 *  将缓冲区归还给摄像头驱动，供下次采集使用
 * ============================================================ */
void CameraDriver::returnFrame(camera_fb_t* fb)
{
    if (fb) {
        esp_camera_fb_return(fb);
    }
}

/* ============================================================
 *  设置分辨率
 *  通过传感器句柄设置帧尺寸
 * ============================================================ */
bool CameraDriver::setFrameSize(framesize_t size)
{
    if (!_initialized) return false;

    sensor_t* s = esp_camera_sensor_get();
    if (!s) return false;

    esp_err_t ret = s->set_framesize(s, size);
    if (ret == ESP_OK) {
        /* 更新内部记录的分辨率 */
        switch (size) {
            case FRAMESIZE_QQVGA:  _width = 160; _height = 120; break;  /* 160x120 */
            case FRAMESIZE_QVGA:   _width = 320; _height = 240; break;  /* 320x240 */
            case FRAMESIZE_VGA:    _width = 640; _height = 480; break;  /* 640x480 */
            default: break;
        }
        DBG_PRINTF("[Camera] 分辨率设置为 %dx%d\n", _width, _height);
        return true;
    }

    DBG_PRINTF("[Camera] 设置分辨率失败: %s\n", esp_err_to_name(ret));
    return false;
}

/* ============================================================
 *  设置 JPEG 压缩质量
 *  数值越小质量越高 (5-63)
 * ============================================================ */
bool CameraDriver::setQuality(int quality)
{
    if (!_initialized) return false;

    /* 限制质量范围 */
    if (quality < 5)  quality = 5;
    if (quality > 63) quality = 63;

    sensor_t* s = esp_camera_sensor_get();
    if (!s) return false;

    esp_err_t ret = s->set_quality(s, quality);
    if (ret == ESP_OK) {
        DBG_PRINTF("[Camera] JPEG质量设置为 %d\n", quality);
        return true;
    }
    return false;
}

/* ============================================================
 *  设置亮度
 *  level 范围: -2 (最暗) 到 +2 (最亮)
 * ============================================================ */
bool CameraDriver::setBrightness(int level)
{
    if (!_initialized) return false;

    if (level < -2) level = -2;
    if (level >  2) level =  2;

    sensor_t* s = esp_camera_sensor_get();
    if (!s) return false;

    return (s->set_brightness(s, level) == ESP_OK);
}

/* ============================================================
 *  设置对比度
 *  level 范围: -2 到 +2
 * ============================================================ */
bool CameraDriver::setContrast(int level)
{
    if (!_initialized) return false;

    if (level < -2) level = -2;
    if (level >  2) level =  2;

    sensor_t* s = esp_camera_sensor_get();
    if (!s) return false;

    return (s->set_contrast(s, level) == ESP_OK);
}

/* ============================================================
 *  设置饱和度
 *  level 范围: -2 到 +2
 * ============================================================ */
bool CameraDriver::setSaturation(int level)
{
    if (!_initialized) return false;

    if (level < -2) level = -2;
    if (level >  2) level =  2;

    sensor_t* s = esp_camera_sensor_get();
    if (!s) return false;

    return (s->set_saturation(s, level) == ESP_OK);
}

/* ============================================================
 *  设置图像翻转 (水平镜像/垂直翻转)
 *  适配导盲头环的摄像头安装方向
 * ============================================================ */
bool CameraDriver::setFlip(bool hmirror, bool vflip)
{
    if (!_initialized) return false;

    sensor_t* s = esp_camera_sensor_get();
    if (!s) return false;

    bool ok = true;
    /* 设置水平镜像 */
    if (s->set_hmirror(s, hmirror ? 1 : 0) != ESP_OK) ok = false;
    /* 设置垂直翻转 */
    if (s->set_vflip(s, vflip ? 1 : 0) != ESP_OK) ok = false;

    DBG_PRINTF("[Camera] 翻转设置: hmirror=%d, vflip=%d\n", hmirror, vflip);
    return ok;
}

/* ============================================================
 *  获取当前帧信息
 *  返回图像宽度、高度、格式等元数据
 * ============================================================ */
bool CameraDriver::getFrameInfo(FrameInfo_t* info)
{
    if (!_initialized || !info) return false;

    info->width     = (uint16_t)_width;
    info->height    = (uint16_t)_height;
    info->format    = (uint8_t)_pixelFormat;
    info->size      = 0;                /* 实际大小在采集后由 fb->len 获取 */
    info->timestamp = millis();         /* 当前时间戳 */
    return true;
}
