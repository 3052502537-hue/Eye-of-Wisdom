/**
 * ============================================================================
 * 文件名: AppConfig.java
 * 功能描述:
 *   - 应用全局配置常量类，集中管理所有可配置参数
 *   - 包含网络通信参数、模式定义、风险等级、UI参数等
 *   - 采用单例模式管理运行时动态配置
 * 依赖关系:
 *   - 被 TCPClient、UDPReceiver、ObstacleAnalyzer 等模块引用
 *   - 被 SettingsFragment 读取和修改
 * 接口说明:
 *   - getInstance(): 获取单例实例
 *   - get/set 方法用于运行时配置项
 *   - 静态常量用于编译期确定的配置
 * ============================================================================
 */
package com.smarteye.blindguide.data;

/**
 * 全局应用配置类
 * 包含静态常量配置和单例运行时配置
 */
public class AppConfig {

    // ==================== 网络通信配置 ====================

    /** ESP32 AP 热点 IP 地址（AP模式下网关通常为 192.168.4.1） */
    public static final String ESP32_HOST = "192.168.4.1";

    /** TCP 传感器数据接收端口 */
    public static final int TCP_PORT = 8888;

    /** UDP 图像数据接收端口 */
    public static final int UDP_PORT = 8889;

    /** TCP 连接超时时间（毫秒） */
    public static final int TCP_TIMEOUT_MS = 5000;

    /** TCP 接收缓冲区大小（字节） */
    public static final int TCP_BUFFER_SIZE = 4096;

    /** UDP 接收缓冲区大小（字节），单帧 JPEG 最大长度 */
    public static final int UDP_BUFFER_SIZE = 65536;

    /** UDP 图像接收超时（毫秒），超过此时间无数据认为断连 */
    public static final int UDP_TIMEOUT_MS = 3000;

    // ==================== 工作模式定义 ====================

    /** 传感器模式：仅处理传感器数据，低能耗 */
    public static final int MODE_SENSOR_ONLY = 1;

    /** 自动模式：语音指令引导 + 障碍物信息播报 */
    public static final int MODE_AUTO = 2;

    /** 风险播报模式：仅播报风险等级 */
    public static final int MODE_RISK_ONLY = 3;

    // ==================== 风险等级定义 ====================

    /** 安全：前方无障碍或距离较远 */
    public static final int RISK_SAFE = 0;

    /** 注意：前方存在中等距离障碍物 */
    public static final int RISK_CAUTION = 1;

    /** 危险：前方存在近距离障碍物，需立即停止 */
    public static final int RISK_DANGER = 2;

    // ==================== 距离阈值（米）====================

    /** 危险距离阈值（米），小于此距离触发危险告警 */
    public static final float DISTANCE_DANGER = 1.0f;

    /** 注意距离阈值（米），小于此距离触发注意提示 */
    public static final float DISTANCE_CAUTION = 2.5f;

    /** 安全距离阈值（米），大于此距离认为安全 */
    public static final float DISTANCE_SAFE = 3.0f;

    // ==================== TTS 语音配置 ====================

    /** TTS 默认语速（1.0 为正常速度） */
    public static final float TTS_DEFAULT_RATE = 0.9f;

    /** TTS 最小播报间隔（毫秒），避免频繁播报 */
    public static final long TTS_MIN_INTERVAL_MS = 2000;

    // ==================== UI 视障友好配置 ====================

    /** 主界面大字体大小（sp） */
    public static final float UI_TITLE_TEXT_SIZE = 36f;

    /** 主界面内容字体大小（sp） */
    public static final float UI_CONTENT_TEXT_SIZE = 24f;

    /** 底部导航按钮高度（dp） */
    public static final int UI_NAV_BUTTON_HEIGHT = 80;

    // ==================== TFLite 模型配置 ====================

    /** TFLite 模型文件名（位于 assets 目录） */
    public static final String TFLITE_MODEL_FILE = "blind_guide_model.tflite";

    /** 模型输入图像尺寸（像素） */
    public static final int TFLITE_INPUT_SIZE = 224;

    // ==================== 开发者模式 ====================

    /** 是否启用调试页面（5次连续点击主界面标题激活） */
    public static final int DEBUG_ACTIVATE_CLICK_COUNT = 5;

    // ==================== 单例运行时配置 ====================

    private static AppConfig instance;

    /** 当前工作模式（运行时可切换） */
    private int currentMode = MODE_AUTO;

    /** TTS 语速（运行时可调节） */
    private float ttsRate = TTS_DEFAULT_RATE;

    /** 是否启用语音控制 */
    private boolean voiceControlEnabled = false;

    /** 是否启用 GPS 定位 */
    private boolean gpsEnabled = true;

    /** 是否启用图像识别 */
    private boolean visionEnabled = true;

    /** 是否启用震动反馈 */
    private boolean vibrationEnabled = true;

    /** 是否已激活开发者模式 */
    private boolean debugModeActivated = false;

    /** WiFi SSID（ESP32 AP 热点名称） */
    private String wifiSSID = "BlindGuide_AP";

    /** WiFi 密码 */
    private String wifiPassword = "12345678";

    /**
     * 私有构造方法，禁止外部实例化
     */
    private AppConfig() {
        // 初始化默认配置
    }

    /**
     * 获取单例实例
     * @return AppConfig 单例对象
     */
    public static synchronized AppConfig getInstance() {
        if (instance == null) {
            instance = new AppConfig();
        }
        return instance;
    }

    // ==================== Getter / Setter 方法 ====================

    /**
     * 获取当前工作模式
     * @return 工作模式常量（MODE_SENSOR_ONLY / MODE_AUTO / MODE_RISK_ONLY）
     */
    public int getCurrentMode() {
        return currentMode;
    }

    /**
     * 设置当前工作模式
     * @param currentMode 工作模式常量
     */
    public void setCurrentMode(int currentMode) {
        this.currentMode = currentMode;
    }

    /**
     * 获取 TTS 语速
     * @return 语速值（0.5~2.0）
     */
    public float getTtsRate() {
        return ttsRate;
    }

    /**
     * 设置 TTS 语速
     * @param ttsRate 语速值（0.5~2.0）
     */
    public void setTtsRate(float ttsRate) {
        this.ttsRate = Math.max(0.5f, Math.min(2.0f, ttsRate));
    }

    public boolean isVoiceControlEnabled() {
        return voiceControlEnabled;
    }

    public void setVoiceControlEnabled(boolean voiceControlEnabled) {
        this.voiceControlEnabled = voiceControlEnabled;
    }

    public boolean isGpsEnabled() {
        return gpsEnabled;
    }

    public void setGpsEnabled(boolean gpsEnabled) {
        this.gpsEnabled = gpsEnabled;
    }

    public boolean isVisionEnabled() {
        return visionEnabled;
    }

    public void setVisionEnabled(boolean visionEnabled) {
        this.visionEnabled = visionEnabled;
    }

    public boolean isVibrationEnabled() {
        return vibrationEnabled;
    }

    public void setVibrationEnabled(boolean vibrationEnabled) {
        this.vibrationEnabled = vibrationEnabled;
    }

    public boolean isDebugModeActivated() {
        return debugModeActivated;
    }

    public void setDebugModeActivated(boolean debugModeActivated) {
        this.debugModeActivated = debugModeActivated;
    }

    public String getWifiSSID() {
        return wifiSSID;
    }

    public void setWifiSSID(String wifiSSID) {
        this.wifiSSID = wifiSSID;
    }

    public String getWifiPassword() {
        return wifiPassword;
    }

    public void setWifiPassword(String wifiPassword) {
        this.wifiPassword = wifiPassword;
    }

    /**
     * 获取工作模式的中文名称
     * @param mode 模式常量
     * @return 模式中文名称
     */
    public static String getModeName(int mode) {
        switch (mode) {
            case MODE_SENSOR_ONLY:
                return "传感器模式";
            case MODE_AUTO:
                return "自动模式";
            case MODE_RISK_ONLY:
                return "风险播报模式";
            default:
                return "未知模式";
        }
    }

    /**
     * 获取风险等级的中文名称
     * @param risk 风险等级常量
     * @return 风险等级中文名称
     */
    public static String getRiskName(int risk) {
        switch (risk) {
            case RISK_SAFE:
                return "安全";
            case RISK_CAUTION:
                return "注意";
            case RISK_DANGER:
                return "危险";
            default:
                return "未知";
        }
    }
}
