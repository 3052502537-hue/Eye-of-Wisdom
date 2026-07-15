/**
 * ============================================================================
 * 文件名: TCPClient.java
 * 功能描述:
 *   - TCP 客户端，连接 ESP32 AP 热点
 *   - 持续接收 ESP32 发送的传感器 JSON 数据
 *   - 支持发送控制命令到 ESP32
 *   - 自动重连机制，断线后定时重试
 * 依赖关系:
 *   - 依赖 AppConfig 获取服务器地址和端口配置
 *   - 依赖 Protocol 解析接收到的 JSON 数据
 *   - 被 MainActivity 创建和管理生命周期
 *   - 通过 OnSensorDataListener 回调通知上层
 * 接口说明:
 *   - connect(): 启动连接
 *   - disconnect(): 断开连接
 *   - sendCommand(String): 发送命令到 ESP32
 *   - setOnSensorDataListener(OnSensorDataListener): 设置数据回调监听器
 * ============================================================================
 */
package com.smarteye.blindguide.network;

import com.smarteye.blindguide.data.AppConfig;

import java.io.BufferedReader;
import java.io.IOException;
import java.io.InputStreamReader;
import java.io.OutputStream;
import java.net.InetSocketAddress;
import java.net.Socket;
import java.net.SocketException;
import java.util.List;
import java.util.concurrent.CopyOnWriteArrayList;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.atomic.AtomicBoolean;

/**
 * TCP 客户端
 * 负责与 ESP32 建立 TCP 连接，接收传感器数据
 */
public class TCPClient {

    /** TCP 连接状态枚举 */
    public static final int STATE_DISCONNECTED = 0;
    public static final int STATE_CONNECTING = 1;
    public static final int STATE_CONNECTED = 2;

    /** 重连间隔（毫秒） */
    private static final long RECONNECT_INTERVAL_MS = 3000;

    /** 心跳发送间隔（毫秒） */
    private static final long HEARTBEAT_INTERVAL_MS = 10000;

    private Socket socket;
    private BufferedReader reader;
    private OutputStream outputStream;

    /** 当前连接状态 */
    private final AtomicBoolean isRunning = new AtomicBoolean(false);
    private final AtomicBoolean isConnected = new AtomicBoolean(false);

    /** 心跳是否正在运行（防止重复启动导致线程泄露） */
    private final AtomicBoolean isHeartbeatRunning = new AtomicBoolean(false);

    /** 线程池，避免阻塞主线程 */
    private final ExecutorService executor = Executors.newFixedThreadPool(2);

    /** 传感器数据回调监听器列表（线程安全，支持多监听器） */
    private final List<OnSensorDataListener> dataListeners = new CopyOnWriteArrayList<>();

    /** 连接状态回调监听器列表（线程安全） */
    private final List<OnConnectionStateListener> stateListeners = new CopyOnWriteArrayList<>();

    /** ESP32 主机地址 */
    private final String host;

    /** TCP 端口 */
    private final int port;

    /**
     * 传感器数据回调接口
     */
    public interface OnSensorDataListener {
        /**
         * 收到传感器数据时回调
         * @param data 解析后的传感器数据
         */
        void onSensorData(Protocol.SensorData data);

        /**
         * 收到原始数据时回调（调试用）
         * @param rawJson 原始 JSON 字符串
         */
        void onRawData(String rawJson);
    }

    /**
     * 连接状态回调接口
     */
    public interface OnConnectionStateListener {
        /**
         * 连接状态变化时回调
         * @param state 新的连接状态
         * @param message 状态描述信息
         */
        void onStateChanged(int state, String message);
    }

    /**
     * 构造 TCP 客户端
     * 使用 AppConfig 中的默认配置
     */
    public TCPClient() {
        this(AppConfig.ESP32_HOST, AppConfig.TCP_PORT);
    }

    /**
     * 构造 TCP 客户端
     * @param host ESP32 主机地址
     * @param port TCP 端口
     */
    public TCPClient(String host, int port) {
        this.host = host;
        this.port = port;
    }

    /**
     * 设置传感器数据回调监听器（替换所有现有监听器）
     * @param listener 监听器实例
     */
    public void setOnSensorDataListener(OnSensorDataListener listener) {
        dataListeners.clear();
        if (listener != null) {
            dataListeners.add(listener);
        }
    }

    /**
     * 添加传感器数据回调监听器（不影响现有监听器）
     * @param listener 监听器实例
     */
    public void addOnSensorDataListener(OnSensorDataListener listener) {
        if (listener != null && !dataListeners.contains(listener)) {
            dataListeners.add(listener);
        }
    }

    /**
     * 移除传感器数据回调监听器
     * @param listener 监听器实例
     */
    public void removeOnSensorDataListener(OnSensorDataListener listener) {
        dataListeners.remove(listener);
    }

    /**
     * 设置连接状态回调监听器（替换所有现有监听器）
     * @param listener 监听器实例
     */
    public void setOnConnectionStateListener(OnConnectionStateListener listener) {
        stateListeners.clear();
        if (listener != null) {
            stateListeners.add(listener);
        }
    }

    /**
     * 启动 TCP 连接
     * 在后台线程执行连接和接收循环
     */
    public void connect() {
        if (isRunning.get()) {
            // 已在运行，避免重复启动
            return;
        }
        isRunning.set(true);
        executor.execute(this::connectionLoop);
    }

    /**
     * 断开 TCP 连接
     * 释放所有资源
     */
    public void disconnect() {
        isRunning.set(false);
        isConnected.set(false);
        isHeartbeatRunning.set(false);
        closeSocket();
        notifyStateChanged(STATE_DISCONNECTED, "已断开连接");
    }

    /**
     * 发送控制命令到 ESP32
     * @param commandJson 命令 JSON 字符串
     * @return true 发送成功，false 发送失败
     */
    public boolean sendCommand(String commandJson) {
        if (!isConnected.get() || outputStream == null) {
            return false;
        }
        try {
            // 命令以换行符结尾，便于 ESP32 解析
            outputStream.write((commandJson + "\n").getBytes("UTF-8"));
            outputStream.flush();
            return true;
        } catch (IOException e) {
            // 发送失败，标记连接异常
            isConnected.set(false);
            return false;
        }
    }

    /**
     * 连接循环：自动重连机制
     * 断线后定时重试，直到调用 disconnect()
     */
    private void connectionLoop() {
        while (isRunning.get()) {
            try {
                notifyStateChanged(STATE_CONNECTING, "正在连接 " + host + ":" + port);

                // 创建 Socket 并连接
                socket = new Socket();
                socket.connect(new InetSocketAddress(host, port), AppConfig.TCP_TIMEOUT_MS);
                socket.setKeepAlive(true);
                socket.setSoTimeout(0); // 接收不超时，持续阻塞读取

                reader = new BufferedReader(new InputStreamReader(socket.getInputStream(), "UTF-8"));
                outputStream = socket.getOutputStream();

                isConnected.set(true);
                notifyStateChanged(STATE_CONNECTED, "连接成功");

                // 启动心跳线程（防止重复启动导致线程泄露）
                if (isHeartbeatRunning.compareAndSet(false, true)) {
                    executor.execute(this::heartbeatLoop);
                }

                // 接收数据循环
                receiveLoop();

            } catch (SocketException e) {
                notifyStateChanged(STATE_DISCONNECTED, "连接异常: " + e.getMessage());
            } catch (IOException e) {
                notifyStateChanged(STATE_DISCONNECTED, "IO错误: " + e.getMessage());
            } catch (Exception e) {
                notifyStateChanged(STATE_DISCONNECTED, "错误: " + e.getMessage());
            } finally {
                isConnected.set(false);
                closeSocket();
            }

            // 等待重连
            if (isRunning.get()) {
                notifyStateChanged(STATE_CONNECTING, "等待重连...");
                try {
                    Thread.sleep(RECONNECT_INTERVAL_MS);
                } catch (InterruptedException e) {
                    Thread.currentThread().interrupt();
                    break;
                }
            }
        }
    }

    /**
     * 接收数据循环
     * 持续读取 ESP32 发送的数据行
     */
    private void receiveLoop() throws IOException {
        String line;
        while (isRunning.get() && isConnected.get()) {
            line = reader.readLine();
            if (line == null) {
                // 对方关闭连接
                break;
            }
            // 回调所有监听器：原始数据
            for (OnSensorDataListener listener : dataListeners) {
                listener.onRawData(line);
            }
            // 解析 JSON 数据
            Protocol.SensorData sensorData = Protocol.parseSensorData(line);
            if (sensorData != null) {
                for (OnSensorDataListener listener : dataListeners) {
                    listener.onSensorData(sensorData);
                }
            }
        }
    }

    /**
     * 心跳循环
     * 定时发送心跳包，保持连接活跃
     */
    private void heartbeatLoop() {
        while (isRunning.get() && isConnected.get()) {
            try {
                Thread.sleep(HEARTBEAT_INTERVAL_MS);
                if (isConnected.get()) {
                    sendCommand(Protocol.buildPingCommand());
                }
            } catch (InterruptedException e) {
                Thread.currentThread().interrupt();
                break;
            }
        }
        isHeartbeatRunning.set(false);
    }

    /**
     * 关闭 Socket 及相关资源
     */
    private void closeSocket() {
        try {
            if (reader != null) {
                reader.close();
                reader = null;
            }
        } catch (IOException e) {
            // 忽略关闭异常
        }
        try {
            if (outputStream != null) {
                outputStream.close();
                outputStream = null;
            }
        } catch (IOException e) {
            // 忽略关闭异常
        }
        try {
            if (socket != null && !socket.isClosed()) {
                socket.close();
            }
            socket = null;
        } catch (IOException e) {
            // 忽略关闭异常
        }
    }

    /**
     * 通知连接状态变化
     * @param state 新状态
     * @param message 状态描述
     */
    private void notifyStateChanged(int state, String message) {
        for (OnConnectionStateListener listener : stateListeners) {
            listener.onStateChanged(state, message);
        }
    }

    /**
     * 获取当前连接状态
     * @return 连接状态常量
     */
    public int getConnectionState() {
        if (!isRunning.get()) {
            return STATE_DISCONNECTED;
        }
        return isConnected.get() ? STATE_CONNECTED : STATE_CONNECTING;
    }

    /**
     * 是否已连接
     * @return true 已连接
     */
    public boolean isConnected() {
        return isConnected.get();
    }
}
