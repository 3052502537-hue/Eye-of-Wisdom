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

    // ==================== UDP 图像包协议 ====================

    /** UDP 图像包头部长度（字节）：4字节帧号 + 4字节总长度 + 4字节当前包长度 */
    public static final int UDP_IMAGE_HEADER_SIZE = 12;

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

        /** 当前工作模式（1-3） */
        public int mode;

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
     * 构建控制命令 JSON 字符串
     * @param action 命令动作常量（如 ACTION_SET_MODE）
     * @param value 命令值
     * @return JSON 字符串
     */
    public static String buildCommand(String action, int value) {
        Command cmd = new Command(action, value);
        return gson.toJson(cmd);
    }

    /**
     * 构建心跳命令 JSON 字符串
     * @return 心跳命令 JSON
     */
    public static String buildPingCommand() {
        return buildCommand(ACTION_PING, 0);
    }

    /**
     * 解析 UDP 图像数据包
     * 协议格式: [4字节帧号][4字节JPEG总长度][4字节当前包长度][JPEG数据]
     * @param buffer 接收到的字节数组
     * @param length 实际数据长度
     * @return ImageFrame 对象，解析失败返回 null
     */
    public static ImageFrame parseImagePacket(byte[] buffer, int length) {
        try {
            if (length < UDP_IMAGE_HEADER_SIZE) {
                // 数据长度不足头部大小，解析失败
                return null;
            }

            // 解析帧号（小端序）
            int frameNumber = bytesToInt(buffer, 0);

            // 解析 JPEG 总长度（小端序，预留字段，当前未使用分片）
            int totalLength = bytesToInt(buffer, 4);

            // 解析当前包长度（小端序）
            int currentLength = bytesToInt(buffer, 8);

            // 校验数据长度
            int expectedLength = UDP_IMAGE_HEADER_SIZE + currentLength;
            if (length < expectedLength) {
                // 数据不完整
                return null;
            }

            // 提取 JPEG 数据
            byte[] jpegData = new byte[currentLength];
            System.arraycopy(buffer, UDP_IMAGE_HEADER_SIZE, jpegData, 0, currentLength);

            return new ImageFrame(frameNumber, jpegData);
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
