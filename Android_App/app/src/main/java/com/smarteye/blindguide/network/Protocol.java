/**
 * ============================================================================
 * 文件名: Protocol.java
 * 功能描述:
 *   - 定义 ESP32 与 Android APP 之间的通信协议数据结构
 *   - 解析传感器 JSON 数据、控制命令 JSON 数据
 *   - 封装图像 UDP 数据包格式（帧号 + JPEG 数据）
 * 依赖关系:
 *   - 依赖 Gson 库进行 JSON 解析
 *   - 被 TCPClient 调用解析接收到的传感器数据
 *   - 被 UDPReceiver 调用解析接收到的图像数据
 *   - 被 ObstacleAnalyzer 引用数据结构
 * 接口说明:
 *   - SensorData: 传感器数据结构类
 *   - RadarData: 雷达数据结构类
 *   - Command: 控制命令结构类
 *   - ImageFrame: 图像帧结构类
 *   - parseSensorData(String): 解析 JSON 为 SensorData 对象
 *   - buildCommand(String, int): 构建控制命令 JSON 字符串
 * ============================================================================
 */
package com.smarteye.blindguide.network;

import com.google.gson.Gson;

/**
 * 通信协议数据结构与解析工具类
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

    /** 请求图像数据 */
    public static final String ACTION_REQ_IMAGE = "req_image";

    /** 心跳检测 */
    public static final String ACTION_PING = "ping";

    /** 重启设备 */
    public static final String ACTION_REBOOT = "reboot";

    // ==================== UDP 图像包协议(与ESP32固件 protocol.h UdpImgHeader_t 一致) ====================

    /** UDP 图像包头部长度（字节）：4B帧号 + 2B分片序号 + 2B总分片数 + 2B数据长度 = 10字节 */
    public static final int UDP_IMAGE_HEADER_SIZE = 10;

    /**
     * 雷达数据结构
     * 包含距离、速度、角度信息
     */
    public static class RadarData {
        /** 距离（米） */
        public float dist;

        /** 相对速度（米/秒），正值表示靠近 */
        public float speed;

        /** 角度（度），0为正前方，正值偏右，负值偏左 */
        public float angle;

        public RadarData() {}

        /**
         * 构造雷达数据
         * @param dist 距离（米）
         * @param speed 相对速度（米/秒）
         * @param angle 角度（度）
         */
        public RadarData(float dist, float speed, float angle) {
            this.dist = dist;
            this.speed = speed;
            this.angle = angle;
        }

        @Override
        public String toString() {
            return String.format("雷达[距离=%.2fm, 速度=%.2fm/s, 角度=%.1f°]", dist, speed, angle);
        }
    }

    /**
     * 传感器数据结构
     * 对应 JSON: {"type":"sensor","laser_front":2.5,"radar_front":{...},...}
     */
    public static class SensorData {
        /** 数据类型，固定为 "sensor" */
        public String type;

        /** 前方激光测距数据（米） */
        public float laser_front;

        /** 前方雷达数据 */
        public RadarData radar_front;

        /** 后方雷达数据 */
        public RadarData radar_back;

        /** 电池电量百分比（0-100） */
        public int battery;

        /** 当前工作模式（1-4） */
        public int mode;

        /** ESP32 决策任务输出的危险等级 (0=SAFE, 1=CAUTION, 2=DANGER) */
        public int level;

        /** ESP32 是否有新图像帧可用 (0/1) */
        public int img;

        /** 接收时间戳（毫秒） */
        public transient long timestamp;

        public SensorData() {
            this.type = TYPE_SENSOR;
            this.timestamp = System.currentTimeMillis();
        }

        @Override
        public String toString() {
            return String.format("传感器[激光=%.2fm, 电量=%d%%, 模式=%d]",
                    laser_front, battery, mode);
        }
    }

    /**
     * 控制命令结构
     * 对应 JSON: {"type":"cmd","action":"set_mode","value":2}
     */
    public static class Command {
        /** 数据类型，固定为 "cmd" */
        public String type;

        /** 命令动作 */
        public String action;

        /** 命令值 */
        public int value;

        public Command() {
            this.type = TYPE_CMD;
        }

        /**
         * 构造控制命令
         * @param action 命令动作常量
         * @param value 命令值
         */
        public Command(String action, int value) {
            this.type = TYPE_CMD;
            this.action = action;
            this.value = value;
        }
    }

    /**
     * 图像帧数据结构
     * UDP 接收的图像数据包
     */
    public static class ImageFrame {
        /** 帧号（用于丢包检测和排序） */
        public int frameNumber;

        /** JPEG 图像二进制数据 */
        public byte[] jpegData;

        /** 接收时间戳（毫秒） */
        public long timestamp;

        public ImageFrame(int frameNumber, byte[] jpegData) {
            this.frameNumber = frameNumber;
            this.jpegData = jpegData;
            this.timestamp = System.currentTimeMillis();
        }
    }

    // ==================== 解析与构建方法 ====================

    /** Gson 实例（线程安全） */
    private static final Gson gson = new Gson();

    /**
     * 解析传感器 JSON 数据
     * @param json JSON 字符串，格式如 {"type":"sensor","laser_front":2.5,...}
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
            // 解析失败时返回 null，由调用方处理
            return null;
        }
    }

    /**
     * 构建控制命令 JSON 字符串（通用格式，兼容旧代码）
     * @param action 命令动作常量（如 ACTION_SET_MODE）
     * @param value 命令值
     * @return JSON 字符串
     */
    public static String buildCommand(String action, int value) {
        Command cmd = new Command(action, value);
        return gson.toJson(cmd);
    }

    // ==================== ESP32 兼容命令构建方法 ====================
    // ESP32 wifi_manager.cpp 使用 strstr() 简单关键字匹配解析命令，
    // 以下方法生成 ESP32 可直接解析的 JSON 格式。

    /**
     * 构建设置模式命令（ESP32 兼容格式）
     * ESP32 解析: strstr("set_mode") 匹配类型, strstr("\"mode\":") 取值
     * @param mode 模式值 (1-4)
     * @return JSON 字符串 "{\"cmd\":\"set_mode\",\"mode\":N}"
     */
    public static String buildSetModeCommand(int mode) {
        return "{\"cmd\":\"set_mode\",\"mode\":" + mode + "}";
    }

    /**
     * 构建设置预警阈值命令（ESP32 兼容格式）
     * @param dAtt 注意距离（米）
     * @param dWar 警告距离（米）
     * @param dDan 危险距离（米）
     * @return JSON 字符串
     */
    public static String buildSetWarnCommand(float dAtt, float dWar, float dDan) {
        return "{\"cmd\":\"set_warn\",\"d_att\":" + dAtt
                + ",\"d_war\":" + dWar + ",\"d_dan\":" + dDan + "}";
    }

    /**
     * 构建设置图像参数命令（ESP32 兼容格式）
     * @param res 分辨率 (0=QVGA, 1=VGA)
     * @param quality JPEG 质量 (1-31)
     * @return JSON 字符串
     */
    public static String buildSetImgCommand(int res, int quality) {
        return "{\"cmd\":\"set_img\",\"res\":" + res + ",\"q\":" + quality + "}";
    }

    /**
     * 构建蜂鸣器控制命令（ESP32 兼容格式）
     * @param on true=开启, false=关闭
     * @return JSON 字符串
     */
    public static String buildSetBuzzerCommand(boolean on) {
        return "{\"cmd\":\"set_buzzer\",\"on\":" + (on ? 1 : 0) + "}";
    }

    /**
     * 构建重启命令（ESP32 兼容格式）
     * @return JSON 字符串 "{\"cmd\":\"reboot\"}"
     */
    public static String buildRebootCommand() {
        return "{\"cmd\":\"reboot\"}";
    }

    /**
     * 构建校准命令（ESP32 兼容格式）
     * @return JSON 字符串 "{\"cmd\":\"calibrate\"}"
     */
    public static String buildCalibrateCommand() {
        return "{\"cmd\":\"calibrate\"}";
    }

    /**
     * 构建查询状态命令（ESP32 兼容格式）
     * @return JSON 字符串 "{\"cmd\":\"query\"}"
     */
    public static String buildQueryStatusCommand() {
        return "{\"cmd\":\"query\"}";
    }

    /**
     * 构建心跳命令 JSON 字符串
     * @return 心跳命令 JSON
     */
    public static String buildPingCommand() {
        return buildCommand(ACTION_PING, 0);
    }

    /**
     * 解析 UDP 图像数据包(与ESP32固件 UdpImgHeader_t 一致)
     * 协议格式: [4B帧号LE][2B分片序号LE][2B总分片数LE][2B数据长度LE][JPEG数据]
     * @param buffer 接收到的字节数组
     * @param length 实际数据长度
     * @return ImageFrame 对象，解析失败返回 null
     */
    public static ImageFrame parseImagePacket(byte[] buffer, int length) {
        try {
            if (length < UDP_IMAGE_HEADER_SIZE) {
                return null;
            }

            // 帧号(4B LE)
            int frameNumber = bytesToInt(buffer, 0);

            // 分片序号(2B LE)
            int sliceIndex = ((buffer[4] & 0xFF) | ((buffer[5] & 0xFF) << 8));

            // 总分片数(2B LE)
            int sliceTotal = ((buffer[6] & 0xFF) | ((buffer[7] & 0xFF) << 8));

            // 当前包JPEG数据长度(2B LE)
            int dataLen = ((buffer[8] & 0xFF) | ((buffer[9] & 0xFF) << 8));

            // 校验数据长度
            if (length < UDP_IMAGE_HEADER_SIZE + dataLen) {
                return null;
            }

            // 提取 JPEG 数据
            byte[] jpegData = new byte[dataLen];
            System.arraycopy(buffer, UDP_IMAGE_HEADER_SIZE, jpegData, 0, dataLen);

            ImageFrame frame = new ImageFrame(frameNumber, jpegData);
            // 如果是分片传输，标记分片信息(供后续重组使用)
            // frame.sliceIndex = sliceIndex; frame.sliceTotal = sliceTotal;
            // 当前单包模式: 大部分JPEG < MTU, sliceTotal通常为1

            return frame;
        } catch (Exception e) {
            return null;
        }
    }

    /**
     * 小端序字节数组转 int
     * @param buffer 字节数组
     * @param offset 起始偏移
     * @return 转换后的 int 值
     */
    private static int bytesToInt(byte[] buffer, int offset) {
        return (buffer[offset] & 0xFF)
                | ((buffer[offset + 1] & 0xFF) << 8)
                | ((buffer[offset + 2] & 0xFF) << 16)
                | ((buffer[offset + 3] & 0xFF) << 24);
    }

    /**
     * int 转小端序字节数组
     * @param value int 值
     * @return 4 字节数组
     */
    public static byte[] intToBytes(int value) {
        return new byte[] {
                (byte) (value & 0xFF),
                (byte) ((value >> 8) & 0xFF),
                (byte) ((value >> 16) & 0xFF),
                (byte) ((value >> 24) & 0xFF)
        };
    }
}
