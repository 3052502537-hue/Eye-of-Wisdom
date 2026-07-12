/**
 * ============================================================================
 * 文件名: UDPReceiver.java
 * 功能描述:
 *   - UDP 接收器，监听指定端口接收 ESP32 发送的图像 JPEG 数据
 *   - 解析 UDP 数据包（帧号 + JPEG 数据）
 *   - 支持丢包检测和帧率统计
 * 依赖关系:
 *   - 依赖 AppConfig 获取端口配置
 *   - 依赖 Protocol 解析 UDP 图像数据包
 *   - 被 MainActivity 创建和管理生命周期
 *   - 通过 OnImageFrameListener 回调通知上层
 * 接口说明:
 *   - startReceive(): 启动接收
 *   - stopReceive(): 停止接收
 *   - setOnImageFrameListener(OnImageFrameListener): 设置图像帧回调监听器
 * ============================================================================
 */
package com.smarteye.blindguide.network;

import com.smarteye.blindguide.data.AppConfig;

import java.net.DatagramPacket;
import java.net.DatagramSocket;
import java.net.SocketException;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.atomic.AtomicBoolean;

/**
 * UDP 接收器
 * 负责接收 ESP32 发送的图像 JPEG 数据
 */
public class UDPReceiver {

    /** 接收状态：已停止 */
    public static final int STATE_STOPPED = 0;

    /** 接收状态：运行中 */
    public static final int STATE_RUNNING = 1;

    /** 接收缓冲区 */
    private byte[] receiveBuffer;

    /** UDP Socket */
    private DatagramSocket socket;

    /** 接收线程运行标志 */
    private final AtomicBoolean isRunning = new AtomicBoolean(false);

    /** 线程池，单线程接收 */
    private final ExecutorService executor = Executors.newSingleThreadExecutor();

    /** 图像帧回调监听器 */
    private OnImageFrameListener frameListener;

    /** 接收端口 */
    private final int port;

    /** 统计：接收帧数 */
    private long frameCount = 0;

    /** 统计：丢包数 */
    private long lostPackets = 0;

    /** 上一帧帧号，用于丢包检测 */
    private int lastFrameNumber = -1;

    /** 接收状态回调监听器 */
    private OnReceiveStateListener stateListener;

    /**
     * 图像帧回调接口
     */
    public interface OnImageFrameListener {
        /**
         * 收到图像帧时回调
         * @param frame 图像帧数据
         */
        void onImageFrame(Protocol.ImageFrame frame);
    }

    /**
     * 接收状态回调接口
     */
    public interface OnReceiveStateListener {
        /**
         * 接收状态变化时回调
         * @param state 新状态
         * @param message 状态描述
         */
        void onStateChanged(int state, String message);
    }

    /**
     * 构造 UDP 接收器
     * 使用 AppConfig 中的默认端口
     */
    public UDPReceiver() {
        this(AppConfig.UDP_PORT);
    }

    /**
     * 构造 UDP 接收器
     * @param port 接收端口
     */
    public UDPReceiver(int port) {
        this.port = port;
        this.receiveBuffer = new byte[AppConfig.UDP_BUFFER_SIZE];
    }

    /**
     * 设置图像帧回调监听器
     * @param listener 监听器实例
     */
    public void setOnImageFrameListener(OnImageFrameListener listener) {
        this.frameListener = listener;
    }

    /**
     * 设置接收状态回调监听器
     * @param listener 监听器实例
     */
    public void setOnReceiveStateListener(OnReceiveStateListener listener) {
        this.stateListener = listener;
    }

    /**
     * 启动 UDP 接收
     */
    public void startReceive() {
        if (isRunning.get()) {
            // 已在运行，避免重复启动
            return;
        }
        isRunning.set(true);
        executor.execute(this::receiveLoop);
    }

    /**
     * 停止 UDP 接收
     */
    public void stopReceive() {
        isRunning.set(false);
        if (socket != null && !socket.isClosed()) {
            socket.close();
        }
        notifyStateChanged(STATE_STOPPED, "接收已停止");
    }

    /**
     * 接收循环
     * 持续接收 UDP 数据包并解析
     */
    private void receiveLoop() {
        try {
            // 创建 UDP Socket，绑定指定端口
            socket = new DatagramSocket(port);
            socket.setReuseAddress(true);
            // 设置接收缓冲区大小
            socket.setReceiveBufferSize(AppConfig.UDP_BUFFER_SIZE * 2);

            notifyStateChanged(STATE_RUNNING, "UDP接收已启动，端口: " + port);

            while (isRunning.get()) {
                try {
                    // 创建接收数据包
                    DatagramPacket packet = new DatagramPacket(receiveBuffer, receiveBuffer.length);

                    // 阻塞接收数据包
                    socket.receive(packet);

                    // 解析图像数据包
                    Protocol.ImageFrame frame = Protocol.parseImagePacket(
                            packet.getData(), packet.getLength());

                    if (frame != null) {
                        frameCount++;

                        // 丢包检测：检查帧号连续性
                        if (lastFrameNumber >= 0) {
                            int expected = (lastFrameNumber + 1) & 0xFFFFFFFF;
                            if (frame.frameNumber != expected) {
                                // 计算丢包数（处理帧号回绕）
                                int diff = (frame.frameNumber - lastFrameNumber) & 0xFFFFFFFF;
                                if (diff > 1 && diff < 1000) {
                                    lostPackets += (diff - 1);
                                }
                            }
                        }
                        lastFrameNumber = frame.frameNumber;

                        // 回调图像帧
                        if (frameListener != null) {
                            frameListener.onImageFrame(frame);
                        }
                    }
                } catch (SocketException e) {
                    // Socket 关闭，正常退出
                    if (isRunning.get()) {
                        notifyStateChanged(STATE_STOPPED, "Socket异常: " + e.getMessage());
                    }
                    break;
                } catch (Exception e) {
                    // 单个包解析失败，继续接收下一个
                    if (isRunning.get()) {
                        // 可在此处回调错误信息
                    }
                }
            }
        } catch (SocketException e) {
            notifyStateChanged(STATE_STOPPED, "无法绑定端口: " + e.getMessage());
        } finally {
            if (socket != null && !socket.isClosed()) {
                socket.close();
            }
        }
    }

    /**
     * 通知接收状态变化
     * @param state 新状态
     * @param message 状态描述
     */
    private void notifyStateChanged(int state, String message) {
        if (stateListener != null) {
            stateListener.onStateChanged(state, message);
        }
    }

    /**
     * 获取接收帧数统计
     * @return 累计接收帧数
     */
    public long getFrameCount() {
        return frameCount;
    }

    /**
     * 获取丢包数统计
     * @return 累计丢包数
     */
    public long getLostPackets() {
        return lostPackets;
    }

    /**
     * 计算丢包率
     * @return 丢包率（0.0~1.0）
     */
    public float getPacketLossRate() {
        long total = frameCount + lostPackets;
        if (total == 0) {
            return 0f;
        }
        return (float) lostPackets / total;
    }

    /**
     * 重置统计数据
     */
    public void resetStatistics() {
        frameCount = 0;
        lostPackets = 0;
        lastFrameNumber = -1;
    }

    /**
     * 是否正在接收
     * @return true 正在接收
     */
    public boolean isRunning() {
        return isRunning.get();
    }
}
