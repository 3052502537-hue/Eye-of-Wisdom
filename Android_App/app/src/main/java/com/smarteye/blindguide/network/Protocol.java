/**
 * ============================================================================
 * 文件名: Protocol.java
 * 功能描述:
 *   - 定义 ESP32 与 Android APP 之间的通信协议数据结构 v3.0
 *   - v3.0: 移除雷达(RadarData)，新增超声波(ultrasonic)和摄像板IP(camera_ip)
 *          图像改为HTTP MJPEG直连摄像板，不再通过UDP
 *   - 解析传感器 JSON 数据、控制命令 JSON 数据
 *   - 封装图像数据帧结构
 * 依赖关系:
 *   - 依赖 Gson 库进行 JSON 解析
 *   - 被 TCPClient 调用解析接收到的传感器数据
 *   - 被 ObstacleAnalyzer 引用数据结构
 * 接口说明:
 *   - SensorData: 传感器数据结构类 (v3.0: 激光+超声波+camera_ip)
 *   - Command: 控制命令结构类
 *   - ImageFrame: 图像帧结构类
 *   - parseSensorData(String): 解析 JSON 为 SensorData 对象
 *   - buildCommand(String, int): 构建控制命令 JSON 字符串
 * ============================================================================
 */
package com.smarteye.blindguide.network;

import com.google.gson.Gson;

/**
 * 通信协议数据结构与解析工具类 v3.0
 */
public class Protocol {

    /** 数据类型常量：传感器数据 */
    public static final String TYPE_SENSOR = "sensor";

    /** 数据类型常量：控制命令 */
    public static final String TYPE_CMD = "cmd";

    /** 数据类型常量：状态响应 */
    public static final String TYPE_STATUS = "status";

    // ==================== 命令动作常量 ====================

    /** 设置工作模式 */
    public static final String ACTION_SET_MODE = "set_mode";

    /** 请求传感器数据 */
    public static final String ACTION_REQ_SENSOR = "req_sensor";

    /** 心跳检测 */
    public static final String ACTION_PING = "ping";

    /** 重启设备 */
    public static final String ACTION_REBOOT = "reboot";

    /**
     * 传感器数据结构 v3.0
     * 对应 JSON:
     * {"type":"sensor","ts":123456,"laser_front":1.23,"ultrasonic":2.10,
     *  "camera_ip":"192.168.4.10","battery":85,"mode":2,"level":0}
     */
    public static class SensorData {
        /** 数据类型，固定为 "sensor" */
        public String type;

        /** ESP32 时间戳(ms) */
        public long ts;

        /** 前方激光测距数据 (SDM10, 米)，-1表示无效 */
        public float laser_front;

        /** 前方超声波测距数据 (HC-SR04, 米)，-1表示无效 */
        public float ultrasonic;

        /** 摄像板ESP32的IP地址 (手机直连拉流) */
        public String camera_ip;

        /** 电池电量百分比（暂未接入，固定-1） */
        public int battery;

        /** 当前工作模式（1=传感器,2=自动,3=风险播报,4=调试） */
        public int mode;

        /** ESP32 决策任务输出的危险等级 (0=SAFE, 1=CAUTION, 2=DANGER) */
        public int level;

        /** 接收时间戳（毫秒，本地赋值） */
        public transient long timestamp;

        public SensorData() {
            this.type = TYPE_SENSOR;
            this.timestamp = System.currentTimeMillis();
        }

        /**
         * 获取前方最近距离 (激光和超声波中取最近值)
         * @return 最近距离(米)，-1表示都无效
         */
        public float getMinDistance() {
            float min = Float.MAX_VALUE;
            if (laser_front > 0) min = laser_front;
            if (ultrasonic > 0 && ultrasonic < min) min = ultrasonic;
            return (min < Float.MAX_VALUE) ? min : -1.0f;
        }

        /**
         * 获取最近距离的来源描述
         */
        public String getMinDistanceSource() {
            float min = Float.MAX_VALUE;
            String src = "无";
            if (laser_front > 0) { min = laser_front; src = "激光(SDM10)"; }
            if (ultrasonic > 0 && ultrasonic < min) { src = "超声波(HC-SR04)"; }
            return src;
        }

        @Override
        public String toString() {
            return String.format("传感器[激光=%.2fm, 超声波=%.2fm, 摄像板=%s, 电量=%d%%, 模式=%d, 等级=%d]",
                    laser_front, ultrasonic, camera_ip, battery, mode, level);
        }
    }

    /**
     * 控制命令结构
     * 对应 JSON: {"type":"cmd","action":"set_mode","value":2}
     */
    public static class Command {
        public String type;
        public String action;
        public int value;

        public Command() {
            this.type = TYPE_CMD;
        }

        public Command(String action, int value) {
            this.type = TYPE_CMD;
            this.action = action;
            this.value = value;
        }
    }

    /**
     * 图像帧数据结构
     * 来自 HTTP MJPEG 流或单帧捕获
     */
    public static class ImageFrame {
        /** 帧号 */
        public int frameNumber;

        /** JPEG 图像二进制数据 */
        public byte[] jpegData;

        /** 图像宽度 */
        public int width;

        /** 图像高度 */
        public int height;

        /** 接收时间戳（毫秒） */
        public long timestamp;

        public ImageFrame(int frameNumber, byte[] jpegData, int width, int height) {
            this.frameNumber = frameNumber;
            this.jpegData = jpegData;
            this.width = width;
            this.height = height;
            this.timestamp = System.currentTimeMillis();
        }
    }

    // ==================== 解析与构建方法 ====================

    /** Gson 实例（线程安全） */
    private static final Gson gson = new Gson();

    /**
     * 解析传感器 JSON 数据
     * @param json JSON 字符串
     * @return SensorData 对象，解析失败返回 null
     */
    public static SensorData parseSensorData(String json) {
        try {
            SensorData data = gson.fromJson(json, SensorData.class);
            if (data != null) {
                data.timestamp = System.currentTimeMillis();
            }
            return data;
        } catch (Exception e) {
            return null;
        }
    }

    /**
     * 构建控制命令 JSON 字符串（通用格式）
     * @param action 命令动作常量
     * @param value 命令值
     * @return JSON 字符串
     */
    public static String buildCommand(String action, int value) {
        Command cmd = new Command(action, value);
        return gson.toJson(cmd);
    }

    // ==================== ESP32 兼容命令构建方法 ====================

    /** 构建设置模式命令（ESP32 兼容格式） */
    public static String buildSetModeCommand(int mode) {
        return "{\"cmd\":\"set_mode\",\"mode\":" + mode + "}";
    }

    /** 构建设置预警阈值命令（ESP32 兼容格式） */
    public static String buildSetWarnCommand(float dAtt, float dWar, float dDan) {
        return "{\"cmd\":\"set_warn\",\"d_att\":" + dAtt
                + ",\"d_war\":" + dWar + ",\"d_dan\":" + dDan + "}";
    }

    /** 构建蜂鸣器控制命令（ESP32 兼容格式） */
    public static String buildSetBuzzerCommand(boolean on) {
        return "{\"cmd\":\"set_buzzer\",\"on\":" + (on ? 1 : 0) + "}";
    }

    /** 构建重启命令 */
    public static String buildRebootCommand() {
        return "{\"cmd\":\"reboot\"}";
    }

    /** 构建校准命令 */
    public static String buildCalibrateCommand() {
        return "{\"cmd\":\"calibrate\"}";
    }

    /** 构建查询状态命令 */
    public static String buildQueryStatusCommand() {
        return "{\"cmd\":\"query\"}";
    }

    /** 构建心跳命令 */
    public static String buildPingCommand() {
        return buildCommand(ACTION_PING, 0);
    }
}
