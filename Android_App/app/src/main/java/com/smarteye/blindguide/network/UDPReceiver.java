/**
 * ============================================================================
 * 文件名: UDPReceiver.java
 * 功能描述:
 *   - UDP 接收器，监听指定端口接收 ESP32 发送的图像 JPEG 数据
 *   - v1.1: 支持 UDP 分片重组 (多slice拼合成完整JPEG)
 *   - 支持丢包检测和帧率统计
 * ============================================================================
 */
package com.smarteye.blindguide.network;

import com.smarteye.blindguide.data.AppConfig;

import java.io.ByteArrayOutputStream;
import java.net.DatagramPacket;
import java.net.DatagramSocket;
import java.net.SocketException;
import java.util.List;
import java.util.Map;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.CopyOnWriteArrayList;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.atomic.AtomicBoolean;

public class UDPReceiver {

    public static final int STATE_STOPPED = 0;
    public static final int STATE_RUNNING = 1;

    private byte[] receiveBuffer;
    private DatagramSocket socket;
    private final AtomicBoolean isRunning = new AtomicBoolean(false);
    private final ExecutorService executor = Executors.newSingleThreadExecutor();
    private final List<OnImageFrameListener> frameListeners = new CopyOnWriteArrayList<>();
    private final int port;

    /** 统计 */
    private long frameCount = 0;
    private long lostPackets = 0;
    private int lastFrameNumber = -1;
    private final List<OnReceiveStateListener> stateListeners = new CopyOnWriteArrayList<>();

    // ==================== v1.1: UDP 分片重组 ====================

    /** 分片重组缓冲区: frameId -> ByteArrayOutputStream */
    private final Map<Integer, ReassemblyCtx> reassemblyMap = new ConcurrentHashMap<>();

    /** 重组超时 (ms), 超时丢弃不完整帧 */
    private static final long REASSEMBLY_TIMEOUT_MS = 5000;

    private static class ReassemblyCtx {
        int totalSlices;
        int receivedCount;
        ByteArrayOutputStream baos = new ByteArrayOutputStream();
        long lastUpdateMs = System.currentTimeMillis();
    }

    // ==================== 回调接口 ====================

    public interface OnImageFrameListener {
        void onImageFrame(Protocol.ImageFrame frame);
    }

    public interface OnReceiveStateListener {
        void onStateChanged(int state, String message);
    }

    public UDPReceiver() {
        this(AppConfig.UDP_PORT);
    }

    public UDPReceiver(int port) {
        this.port = port;
        this.receiveBuffer = new byte[AppConfig.UDP_BUFFER_SIZE];
    }

    public void setOnImageFrameListener(OnImageFrameListener listener) {
        frameListeners.clear();
        if (listener != null) frameListeners.add(listener);
    }

    public void addOnImageFrameListener(OnImageFrameListener listener) {
        if (listener != null && !frameListeners.contains(listener)) {
            frameListeners.add(listener);
        }
    }

    public void removeOnImageFrameListener(OnImageFrameListener listener) {
        frameListeners.remove(listener);
    }

    public void setOnReceiveStateListener(OnReceiveStateListener listener) {
        stateListeners.clear();
        if (listener != null) stateListeners.add(listener);
    }

    public void startReceive() {
        if (isRunning.get()) return;
        isRunning.set(true);
        executor.execute(this::receiveLoop);
    }

    public void stopReceive() {
        isRunning.set(false);
        if (socket != null && !socket.isClosed()) socket.close();
        notifyStateChanged(STATE_STOPPED, "接收已停止");
    }

    private void receiveLoop() {
        try {
            socket = new DatagramSocket(port);
            socket.setReuseAddress(true);
            socket.setReceiveBufferSize(AppConfig.UDP_BUFFER_SIZE * 2);
            notifyStateChanged(STATE_RUNNING, "UDP接收已启动，端口: " + port);

            while (isRunning.get()) {
                try {
                    DatagramPacket packet = new DatagramPacket(receiveBuffer, receiveBuffer.length);
                    socket.receive(packet);

                    // v1.1: 解析分片并尝试重组
                    Protocol.SliceInfo slice = Protocol.parseImageSlice(
                            packet.getData(), packet.getLength());

                    if (slice != null) {
                        byte[] completeJpeg = reassembleSlice(slice);
                        if (completeJpeg != null) {
                            frameCount++;
                            // 丢包检测
                            if (lastFrameNumber >= 0) {
                                int expected = (lastFrameNumber + 1) & 0xFFFFFFFF;
                                if (slice.frameNumber != expected) {
                                    int diff = (slice.frameNumber - lastFrameNumber) & 0xFFFFFFFF;
                                    if (diff > 1 && diff < 1000) lostPackets += (diff - 1);
                                }
                            }
                            lastFrameNumber = slice.frameNumber;

                            Protocol.ImageFrame frame = new Protocol.ImageFrame(
                                    slice.frameNumber, completeJpeg);
                            for (OnImageFrameListener l : frameListeners) {
                                l.onImageFrame(frame);
                            }
                        }
                    }

                    // 定期清理超时的重组缓冲区
                    cleanupReassembly();
                } catch (SocketException e) {
                    if (isRunning.get()) notifyStateChanged(STATE_STOPPED, "Socket异常: " + e.getMessage());
                    break;
                } catch (Exception e) {
                    // 单包解析失败，继续
                }
            }
        } catch (SocketException e) {
            notifyStateChanged(STATE_STOPPED, "无法绑定端口: " + e.getMessage());
        } finally {
            if (socket != null && !socket.isClosed()) socket.close();
        }
    }

    /**
     * v1.1: 分片重组
     * 将所有 slice 拼合成完整 JPEG 后返回，未完成则返回 null
     */
    private byte[] reassembleSlice(Protocol.SliceInfo slice) {
        int frameId = slice.frameNumber;

        ReassemblyCtx ctx = reassemblyMap.get(frameId);
        if (ctx == null) {
            ctx = new ReassemblyCtx();
            ctx.totalSlices = slice.sliceTotal;
            reassemblyMap.put(frameId, ctx);
        }

        ctx.totalSlices = slice.sliceTotal;  // 以最新包为准
        ctx.lastUpdateMs = System.currentTimeMillis();

        try {
            ctx.baos.write(slice.jpegSlice);
            ctx.receivedCount++;
        } catch (Exception e) {
            return null;
        }

        // 所有分片收齐 → 返回完整 JPEG
        if (ctx.receivedCount >= ctx.totalSlices) {
            reassemblyMap.remove(frameId);
            try {
                return ctx.baos.toByteArray();
            } catch (Exception e) {
                return null;
            }
        }

        return null;  // 未收齐
    }

    /**
     * 清理超时的重组缓冲区
     */
    private void cleanupReassembly() {
        long now = System.currentTimeMillis();
        for (Map.Entry<Integer, ReassemblyCtx> e : reassemblyMap.entrySet()) {
            if (now - e.getValue().lastUpdateMs > REASSEMBLY_TIMEOUT_MS) {
                try { e.getValue().baos.close(); } catch (Exception ignored) {}
                reassemblyMap.remove(e.getKey());
            }
        }
    }

    private void notifyStateChanged(int state, String message) {
        for (OnReceiveStateListener l : stateListeners) l.onStateChanged(state, message);
    }

    public long getFrameCount() { return frameCount; }
    public long getLostPackets() { return lostPackets; }
    public float getPacketLossRate() {
        long total = frameCount + lostPackets;
        return total == 0 ? 0f : (float) lostPackets / total;
    }
    public void resetStatistics() {
        frameCount = 0;
        lostPackets = 0;
        lastFrameNumber = -1;
    }
    public boolean isRunning() { return isRunning.get(); }
}
