/**
 * ============================================================================
 * 文件名: CameraHttpClient.java
 * 功能描述:
 *   - 通过 HTTP 从 ESP32-S3-EYE 摄像头板获取 MJPEG 视频流
 *   - 摄像头板 AP 热点 IP: 192.168.4.1，端点: /video
 *   - 解析 multipart/x-mixed-replace 流，逐帧回调 JPEG 数据
 *   - 用于 DebugFragment 的"原始视频(直出)"显示
 * 依赖关系:
 *   - 依赖 OkHttp 库
 *   - 被 DebugFragment 调用
 * 接口说明:
 *   - start(): 开始拉取 MJPEG 流
 *   - stop(): 停止拉取
 *   - setOnFrameListener(OnFrameListener): 设置帧回调
 * ============================================================================
 */
package com.smarteye.blindguide.network;

import android.os.Handler;
import android.os.Looper;
import android.util.Log;

import com.smarteye.blindguide.data.AppConfig;

import java.io.BufferedInputStream;
import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.net.HttpURLConnection;
import java.net.URL;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.atomic.AtomicBoolean;

/**
 * MJPEG 视频流客户端
 * 从 ESP32-S3-EYE 摄像头板拉取 /video 端点
 */
public class CameraHttpClient {

    private static final String TAG = "CameraHttpClient";

    /** MJPEG 流读取超时（秒） */
    private static final int READ_TIMEOUT_SEC = 10;

    /** 连接超时（秒） */
    private static final int CONNECT_TIMEOUT_SEC = 5;

    /** 最大重连次数 */
    private static final int MAX_RECONNECT = 5;

    /** 重连间隔（毫秒） */
    private static final long RECONNECT_DELAY_MS = 1500;

    /** 视频流 URL */
    private String streamUrl;

    /** 是否正在运行 */
    private final AtomicBoolean isRunning = new AtomicBoolean(false);

    /** 线程池 */
    private final ExecutorService executor = Executors.newSingleThreadExecutor();

    /** 主线程 Handler */
    private final Handler mainHandler = new Handler(Looper.getMainLooper());

    /** 帧回调监听器 */
    private OnFrameListener frameListener;

    /** 帧计数 */
    private int frameCount = 0;

    /** MJPEG boundary 分隔符 */
    private static final String BOUNDARY_PREFIX = "--";
    private static final String CONTENT_LENGTH_HEADER = "Content-Length:";
    private static final byte[] JPEG_SOI = {(byte) 0xFF, (byte) 0xD8}; // JPEG 起始标记
    private static final byte[] JPEG_EOI = {(byte) 0xFF, (byte) 0xD9}; // JPEG 结束标记

    /**
     * 帧回调接口
     */
    public interface OnFrameListener {
        /**
         * 收到一帧 JPEG 图像
         * @param jpegData JPEG 数据
         * @param frameNumber 帧序号
         * @param timestampMs 接收时间戳
         */
        void onFrame(byte[] jpegData, int frameNumber, long timestampMs);

        /**
         * 错误回调
         * @param error 错误信息
         */
        void onError(String error);

        /**
         * 连接状态变化
         * @param connected 是否已连接
         * @param message 状态描述
         */
        void onStateChanged(boolean connected, String message);
    }

    /**
     * 构造 MJPEG 客户端
     * 默认 URL: http://192.168.4.1/video
     */
    public CameraHttpClient() {
        this("http://" + AppConfig.ESP32_HOST + "/video");
    }

    /**
     * 构造 MJPEG 客户端
     * @param streamUrl MJPEG 流 URL
     */
    public CameraHttpClient(String streamUrl) {
        this.streamUrl = streamUrl;
    }

    /**
     * 设置视频流 URL
     * @param url MJPEG 流 URL
     */
    public void setStreamUrl(String url) {
        this.streamUrl = url;
    }

    /**
     * 设置帧回调
     */
    public void setOnFrameListener(OnFrameListener listener) {
        this.frameListener = listener;
    }

    /**
     * 开始拉取视频流
     */
    public void start() {
        if (isRunning.get()) return;
        isRunning.set(true);
        executor.execute(this::streamLoop);
        Log.i(TAG, "MJPEG 客户端启动: " + streamUrl);
    }

    /**
     * 停止拉取
     */
    public void stop() {
        isRunning.set(false);
        notifyStateChanged(false, "已停止");
        Log.i(TAG, "MJPEG 客户端停止");
    }

    public boolean isRunning() { return isRunning.get(); }
    public int getFrameCount() { return frameCount; }

    /**
     * 重置统计
     */
    public void resetStatistics() {
        frameCount = 0;
    }

    // ==================== MJPEG 流拉取 ====================

    private void streamLoop() {
        int reconnectCount = 0;

        while (isRunning.get()) {
            try {
                notifyStateChanged(true, "正在连接...");
                readMjpegStream();
                // 正常退出（如 stop 调用）
                break;
            } catch (IOException e) {
                Log.w(TAG, "MJPEG 流异常: " + e.getMessage());
                notifyStateChanged(false, "连接断开: " + e.getMessage());

                if (!isRunning.get()) break;

                reconnectCount++;
                if (reconnectCount > MAX_RECONNECT) {
                    notifyStateChanged(false, "重连失败（已达最大次数）");
                    break;
                }

                notifyStateChanged(false, "等待重连... (" + reconnectCount + "/" + MAX_RECONNECT + ")");
                try {
                    Thread.sleep(RECONNECT_DELAY_MS);
                } catch (InterruptedException ie) {
                    Thread.currentThread().interrupt();
                    break;
                }
            }
        }
    }

    /**
     * 读取 MJPEG 流并解析帧
     * multipart/x-mixed-replace 格式:
     *   --frame\r\n
     *   Content-Type: image/jpeg\r\n
     *   Content-Length: N\r\n
     *   \r\n
     *   <N bytes JPEG data>\r\n
     */
    private void readMjpegStream() throws IOException {
        URL url = new URL(streamUrl);
        HttpURLConnection conn = null;
        InputStream rawStream = null;
        BufferedInputStream bis = null;

        try {
            conn = (HttpURLConnection) url.openConnection();
            conn.setConnectTimeout(CONNECT_TIMEOUT_SEC * 1000);
            conn.setReadTimeout(READ_TIMEOUT_SEC * 1000);
            conn.setRequestMethod("GET");
            conn.connect();

            int responseCode = conn.getResponseCode();
            if (responseCode != 200) {
                throw new IOException("HTTP " + responseCode);
            }

            String contentType = conn.getContentType();
            Log.i(TAG, "连接成功, Content-Type: " + contentType);

            notifyStateChanged(true, "已连接");

            rawStream = conn.getInputStream();
            bis = new BufferedInputStream(rawStream, 32768);

            // 从 Content-Type 中提取 boundary
            String boundary = extractBoundary(contentType);

            // 如果没有 boundary，尝试从流开头读取
            if (boundary == null || boundary.isEmpty()) {
                // 读取第一行获取 boundary
                boundary = readLine(bis);
                if (boundary != null && boundary.startsWith(BOUNDARY_PREFIX)) {
                    boundary = boundary.substring(2).trim();
                }
            }

            if (boundary == null || boundary.isEmpty()) {
                // 无 boundary 时回退到 JPEG SOI/EOI 检测模式
                readJpegByMarkers(bis);
            } else {
                // 标准 MJPEG 解析
                readMjpegByBoundary(bis, boundary);
            }

        } finally {
            try { if (bis != null) bis.close(); } catch (Exception ignored) {}
            try { if (rawStream != null) rawStream.close(); } catch (Exception ignored) {}
            if (conn != null) conn.disconnect();
        }
    }

    /**
     * 从 Content-Type 提取 boundary
     */
    private String extractBoundary(String contentType) {
        if (contentType == null) return null;
        int idx = contentType.indexOf("boundary=");
        if (idx < 0) return null;
        String boundary = contentType.substring(idx + 9);
        // 去掉可能的引号
        if (boundary.startsWith("\"") && boundary.endsWith("\"")) {
            boundary = boundary.substring(1, boundary.length() - 1);
        }
        return boundary.trim();
    }

    /**
     * 按 boundary 解析 MJPEG 流
     */
    private void readMjpegByBoundary(BufferedInputStream bis, String boundary) throws IOException {
        String expectedBoundary = BOUNDARY_PREFIX + boundary;
        String line;
        int contentLength = -1;

        // 跳过到第一个 boundary
        while (isRunning.get()) {
            line = readLine(bis);
            if (line == null) throw new IOException("流结束（等待boundary）");
            if (line.equals(expectedBoundary)) break;
        }

        while (isRunning.get()) {
            contentLength = -1;

            // 读取 MIME headers
            while (isRunning.get()) {
                line = readLine(bis);
                if (line == null) throw new IOException("流结束（等待header）");

                // 空行 = headers 结束
                if (line.isEmpty()) break;

                // 检查是否是新 boundary（某些实现不严格）
                if (line.startsWith(expectedBoundary)) {
                    // 已经到下一帧，跳过当前帧
                    continue;
                }

                // 解析 Content-Length
                if (line.regionMatches(true, 0, CONTENT_LENGTH_HEADER, 0, CONTENT_LENGTH_HEADER.length())) {
                    try {
                        contentLength = Integer.parseInt(line.substring(CONTENT_LENGTH_HEADER.length()).trim());
                    } catch (NumberFormatException ignored) {}
                }
            }

            // 读取 JPEG 数据
            byte[] jpegData;
            if (contentLength > 0 && contentLength < 1024 * 1024) {
                // 已知 Content-Length，精确读取
                jpegData = new byte[contentLength];
                int totalRead = 0;
                while (totalRead < contentLength && isRunning.get()) {
                    int n = bis.read(jpegData, totalRead, contentLength - totalRead);
                    if (n < 0) throw new IOException("流结束（读取JPEG数据）");
                    totalRead += n;
                }
            } else {
                // 未知长度，读取到 boundary 或 EOI
                jpegData = readUntilBoundary(bis, expectedBoundary);
            }

            // 验证并回调帧
            if (jpegData != null && jpegData.length > 100) {
                frameCount++;
                notifyFrame(jpegData, frameCount);
            }

            // 读取本帧末尾的 \r\n（在 boundary 之前）
            // 如果用了 readUntilBoundary，缓冲区已经定位到 boundary 之后

            // 如果是精确读取模式，需要继续读到下一个 boundary
            if (contentLength > 0) {
                // 跳过本帧末尾的 \r\n 和 boundary 行
                while (isRunning.get()) {
                    line = readLine(bis);
                    if (line == null) throw new IOException("流结束（等待下一boundary）");
                    if (line.equals(expectedBoundary)) break;
                }
            }
        }
    }

    /**
     * 读取直到遇到 boundary 标记，返回中间所有数据
     * 调用后缓冲区定位于 boundary 之后
     */
    private byte[] readUntilBoundary(BufferedInputStream bis, String boundary) throws IOException {
        ByteArrayOutputStream baos = new ByteArrayOutputStream(16384);
        String boundaryLine = boundary;
        int boundaryLen = boundaryLine.length();

        // 使用环形缓冲区匹配 boundary 行
        StringBuilder ring = new StringBuilder();

        while (isRunning.get()) {
            int b = bis.read();
            if (b < 0) break;

            baos.write(b);
            ring.append((char) b);

            // 保持 ring 不超过 boundary 长度 + 2
            if (ring.length() > boundaryLen + 2) {
                ring.delete(0, ring.length() - boundaryLen - 2);
            }

            // 检查是否匹配 boundary（boundary 出现在行首，即 \r\n 之后）
            String ringStr = ring.toString();
            int boundIdx = ringStr.lastIndexOf(boundaryLine);
            if (boundIdx >= 0) {
                // 确认 boundary 前是 \r\n
                if (boundIdx >= 2) {
                    char prev0 = ringStr.charAt(boundIdx - 2);
                    char prev1 = ringStr.charAt(boundIdx - 1);
                    if (prev0 == '\r' && prev1 == '\n') {
                        // 找到了！截掉 boundary 及之后的
                        byte[] all = baos.toByteArray();
                        int jpegEnd = all.length - ring.length() + boundIdx - 2;
                        if (jpegEnd > 0 && jpegEnd < all.length) {
                            byte[] jpeg = new byte[jpegEnd];
                            System.arraycopy(all, 0, jpeg, 0, jpegEnd);
                            return jpeg;
                        }
                        break;
                    }
                }
            }
        }

        // 没找到 boundary，返回所有数据
        byte[] all = baos.toByteArray();
        return all.length > 0 ? all : null;
    }

    /**
     * 回退模式：通过 JPEG SOI/EOI 标记检测帧边界
     * 用于没有标准 MJPEG boundary 的流
     */
    private void readJpegByMarkers(BufferedInputStream bis) throws IOException {
        ByteArrayOutputStream jpegBuffer = new ByteArrayOutputStream(32768);
        boolean inFrame = false;
        int soiCount = 0; // 连续的 FF D8 计数（去重）

        while (isRunning.get()) {
            int b = bis.read();
            if (b < 0) throw new IOException("流结束");

            jpegBuffer.write(b);

            if (!inFrame) {
                // 检测 SOI (FF D8)
                byte[] buf = jpegBuffer.toByteArray();
                int len = buf.length;
                if (len >= 2) {
                    if ((buf[len - 2] & 0xFF) == 0xFF && (buf[len - 1] & 0xFF) == 0xD8) {
                        soiCount++;
                        if (soiCount >= 2 || len == 2) {
                            // 找到新的 SOI，清掉前面的数据
                            jpegBuffer.reset();
                            jpegBuffer.write(0xFF);
                            jpegBuffer.write(0xD8);
                            inFrame = true;
                            soiCount = 0;
                        }
                    }
                }
            } else {
                // 检测 EOI (FF D9)
                byte[] buf = jpegBuffer.toByteArray();
                int len = buf.length;
                if (len >= 2) {
                    if ((buf[len - 2] & 0xFF) == 0xFF && (buf[len - 1] & 0xFF) == 0xD9) {
                        // 完整的一帧
                        byte[] jpegData = jpegBuffer.toByteArray();
                        if (jpegData.length > 100) {
                            frameCount++;
                            notifyFrame(jpegData, frameCount);
                        }
                        jpegBuffer.reset();
                        inFrame = false;
                    }
                }
                // 防止缓冲区无限增长
                if (jpegBuffer.size() > 512 * 1024) {
                    jpegBuffer.reset();
                    inFrame = false;
                }
            }
        }
    }

    /**
     * 读取一行（以 \r\n 或 \n 结尾）
     */
    private String readLine(BufferedInputStream bis) throws IOException {
        ByteArrayOutputStream baos = new ByteArrayOutputStream(256);
        int prev = -1;
        while (true) {
            int b = bis.read();
            if (b < 0) {
                // 流结束
                return baos.size() > 0 ? baos.toString("UTF-8") : null;
            }
            if (b == '\n') {
                // 去掉末尾的 \r
                byte[] bytes = baos.toByteArray();
                int end = bytes.length;
                if (end > 0 && bytes[end - 1] == '\r') end--;
                return new String(bytes, 0, end, "UTF-8");
            }
            baos.write(b);
            prev = b;
        }
    }

    // ==================== 回调通知 ====================

    private void notifyFrame(byte[] jpegData, int frameNum) {
        if (frameListener != null) {
            long ts = System.currentTimeMillis();
            mainHandler.post(() -> frameListener.onFrame(jpegData, frameNum, ts));
        }
    }

    private void notifyError(String error) {
        if (frameListener != null) {
            mainHandler.post(() -> frameListener.onError(error));
        }
    }

    private void notifyStateChanged(boolean connected, String message) {
        if (frameListener != null) {
            mainHandler.post(() -> frameListener.onStateChanged(connected, message));
        }
    }
}
