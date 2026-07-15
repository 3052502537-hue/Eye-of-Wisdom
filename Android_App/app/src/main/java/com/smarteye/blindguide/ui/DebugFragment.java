/**
 * ============================================================================
 * 文件名: DebugFragment.java
 * 功能描述: 开发者调试页面 Fragment v3.0
 *   v3.0: 移除雷达(UDP)引用，新增HC-SR04雷达可视化、SDM10激光显示
 *         双屏视频：AI检测框叠加 + 原图直出
 *         摄像板IP动态获取(来自传感器JSON)
 *
 *   - HC-SR04 超声波雷达可视化 (RadarView 扇形显示)
 *   - SDM10 激光测距数字显示
 *   - AI检测框叠加视频预览 (DetectionOverlayView)
 *   - 原图直出预览 (HTTP MJPEG 从摄像板拉流)
 *   - 传感器原始数据、处理后数据、网络统计、运行日志
 * ============================================================================
 */
package com.smarteye.blindguide.ui;

import android.graphics.BitmapFactory;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.view.LayoutInflater;
import android.view.View;
import android.view.ViewGroup;
import android.widget.Button;
import android.widget.ImageView;
import android.widget.ScrollView;
import android.widget.TextView;

import androidx.annotation.NonNull;
import androidx.annotation.Nullable;
import androidx.fragment.app.Fragment;

import com.smarteye.blindguide.MainActivity;
import com.smarteye.blindguide.R;
import com.smarteye.blindguide.ai.TFLiteClassifier;
import com.smarteye.blindguide.data.AppConfig;
import com.smarteye.blindguide.logic.ObstacleAnalyzer;
import com.smarteye.blindguide.network.CameraHttpClient;
import com.smarteye.blindguide.network.Protocol;

import java.text.SimpleDateFormat;
import java.util.Date;
import java.util.List;
import java.util.Locale;

/**
 * 开发者调试页面 Fragment v3.0
 */
public class DebugFragment extends Fragment implements MainActivity.DebugDataListener {

    // ==================== 视图引用 ====================

    /** HC-SR04 超声波雷达可视化 */
    private RadarView radarView;

    /** AI检测框叠加预览 */
    private DetectionOverlayView detectionOverlay;

    /** 原图直出 */
    private ImageView imageRawVideo;
    private TextView textRawVideoInfo;
    private TextView textFrameInfo;

    /** 传感器数据 */
    private TextView textLaserDist;
    private TextView textUltrasonicDist;
    private TextView textCameraIp;
    private TextView textRawTimestamp;

    /** 处理后数据 */
    private TextView textDangerLevel;
    private TextView textMinDistance;
    private TextView textDebounceInfo;
    private TextView textAdvice;
    private TextView textDetectionCount;

    /** 网络统计 */
    private TextView textNetworkStats;

    /** 日志 */
    private TextView textLog;
    private ScrollView scrollLog;
    private Button btnClearLog;
    private Button btnResetStats;
    private Button btnHttpCamera;

    // ==================== 状态数据 ====================

    private MainActivity activity;
    private CameraHttpClient httpCamera;
    private boolean isHttpCameraRunning = false;
    private final Handler mainHandler = new Handler(Looper.getMainLooper());
    private final SimpleDateFormat timeFormat =
            new SimpleDateFormat("HH:mm:ss.SSS", Locale.getDefault());
    private static final int MAX_LOG_LINES = 200;
    private final StringBuilder logBuilder = new StringBuilder();

    /** 帧率统计 */
    private long lastFrameTime = 0;
    private int frameCountSinceLastUpdate = 0;
    private float currentFps = 0;

    /** 最新传感器数据 */
    private Protocol.SensorData lastSensorData;

    /** 最新识别结果 */
    private TFLiteClassifier.ClassifyResult lastClassifyResult;

    /** 摄像板IP（从传感器JSON动态获取） */
    private String cameraIp = AppConfig.CAMERA_ESP32_IP;

    // ==================== 生命周期 ====================

    @Override
    public View onCreateView(@NonNull LayoutInflater inflater, @Nullable ViewGroup container,
                             @Nullable Bundle savedInstanceState) {
        View view = inflater.inflate(R.layout.fragment_debug, container, false);
        bindViews(view);
        setupListeners();
        return view;
    }

    private void bindViews(View view) {
        // HC-SR04 雷达视图
        radarView = view.findViewById(R.id.radar_view);

        // 视频预览
        detectionOverlay = view.findViewById(R.id.detection_overlay);
        textFrameInfo = view.findViewById(R.id.text_frame_info);

        // 原图直出
        imageRawVideo = view.findViewById(R.id.image_raw_video);
        textRawVideoInfo = view.findViewById(R.id.text_raw_video_info);

        // 传感器数据
        textLaserDist = view.findViewById(R.id.text_laser_dist);
        textUltrasonicDist = view.findViewById(R.id.text_ultrasonic_dist);
        textCameraIp = view.findViewById(R.id.text_camera_ip);
        textRawTimestamp = view.findViewById(R.id.text_raw_timestamp);

        // 处理后数据
        textDangerLevel = view.findViewById(R.id.text_danger_level);
        textMinDistance = view.findViewById(R.id.text_min_distance);
        textDebounceInfo = view.findViewById(R.id.text_debounce_info);
        textAdvice = view.findViewById(R.id.text_advice);
        textDetectionCount = view.findViewById(R.id.text_detection_count);

        // 网络统计
        textNetworkStats = view.findViewById(R.id.text_network_stats);

        // 日志
        textLog = view.findViewById(R.id.text_log);
        scrollLog = view.findViewById(R.id.scroll_log);
        btnClearLog = view.findViewById(R.id.btn_clear_log);
        btnResetStats = view.findViewById(R.id.btn_reset_stats);
        btnHttpCamera = view.findViewById(R.id.btn_http_camera);
    }

    private void setupListeners() {
        btnClearLog.setOnClickListener(v -> {
            logBuilder.setLength(0);
            textLog.setText("");
            appendLog("日志已清空");
        });

        btnResetStats.setOnClickListener(v -> {
            frameCountSinceLastUpdate = 0;
            lastFrameTime = 0;
            currentFps = 0;
            if (httpCamera != null) httpCamera.resetStatistics();
            updateNetworkStats();
            appendLog("统计已重置");
        });

        if (btnHttpCamera != null) {
            btnHttpCamera.setOnClickListener(v -> toggleHttpCamera());
        }
    }

    @Override
    public void onActivityCreated(@Nullable Bundle savedInstanceState) {
        super.onActivityCreated(savedInstanceState);
        activity = (MainActivity) getActivity();
    }

    @Override
    public void onResume() {
        super.onResume();
        if (activity != null) {
            activity.addDebugDataListener(this);
        }
        updateNetworkStats();
        appendLog("调试页面 v3.0 已就绪");
        appendLog("传感器: SDM10激光 + HC-SR04超声波");
    }

    @Override
    public void onPause() {
        super.onPause();
        if (activity != null) {
            activity.removeDebugDataListener(this);
        }
        stopHttpCamera();
    }

    // ==================== DebugDataListener 接口实现 ====================

    @Override
    public void onSensorData(Protocol.SensorData data) {
        this.lastSensorData = data;

        // 动态更新摄像板IP
        if (data.camera_ip != null && !data.camera_ip.isEmpty()
                && !data.camera_ip.equals(cameraIp)) {
            cameraIp = data.camera_ip;
            appendLog("摄像板IP更新: " + cameraIp);
        }

        mainHandler.post(() -> {
            updateSensorDisplay(data);
            updateRadarView(data);
            updateProcessedData(data, lastClassifyResult);
            updateNetworkStats();
        });
    }

    @Override
    public void onImageFrame(Protocol.ImageFrame frame) {
        mainHandler.post(() -> {
            updateVideoPreview(frame);
            updateFrameRate();
        });
    }

    @Override
    public void onClassifyResult(TFLiteClassifier.ClassifyResult result) {
        this.lastClassifyResult = result;
        mainHandler.post(() -> {
            if (result.hasDetections()) {
                detectionOverlay.setDetections(result.detections);
            }
            updateProcessedData(lastSensorData, result);
        });
    }

    @Override
    public void onAnalysisResult(ObstacleAnalyzer.AnalysisResult result) {
        mainHandler.post(() -> {
            if (result != null && result.adviceText != null && !result.adviceText.isEmpty()) {
                textAdvice.setText("避障建议: " + result.adviceText);
            }
        });
    }

    // ==================== 数据更新方法 ====================

    /**
     * 更新HC-SR04雷达视图
     */
    private void updateRadarView(Protocol.SensorData data) {
        if (data == null || radarView == null) return;

        boolean usOnline = (data.ultrasonic > 0);
        radarView.updateUltrasonic(
                usOnline ? data.ultrasonic : -1f,
                usOnline);
        radarView.updateLaser(data.laser_front > 0 ? data.laser_front : -1f);
        radarView.updateDangerLevel(data.level);
    }

    /**
     * 更新传感器数据显示
     */
    private void updateSensorDisplay(Protocol.SensorData data) {
        // SDM10 激光距离
        if (data.laser_front > 0) {
            textLaserDist.setText(String.format("🔴 SDM10激光: %.2f m (50Hz, ±5cm精度)",
                    data.laser_front));
        } else {
            textLaserDist.setText("🔴 SDM10激光: 无数据");
        }

        // HC-SR04 超声波距离
        if (data.ultrasonic > 0) {
            textUltrasonicDist.setText(String.format("🔵 HC-SR04超声波: %.2f m (2cm~4m量程)",
                    data.ultrasonic));
        } else {
            textUltrasonicDist.setText("🔵 HC-SR04超声波: 无回波(超量程或故障)");
        }

        // 摄像板IP
        textCameraIp.setText(String.format("📷 摄像板: %s (HTTP直连拉流)",
                (data.camera_ip != null && !data.camera_ip.isEmpty())
                        ? data.camera_ip : "未获取"));

        // 时间戳 + 系统状态
        textRawTimestamp.setText(String.format("帧时间戳: %s | 电量: %d%% | 模式: %s | ESP32等级: %d",
                timeFormat.format(new Date(data.timestamp)),
                data.battery,
                AppConfig.getModeName(data.mode),
                data.level));
    }

    /**
     * 更新视频预览 (AI叠加 + 原图)
     */
    private void updateVideoPreview(Protocol.ImageFrame frame) {
        try {
            if (frame.jpegData != null && frame.jpegData.length > 0) {
                android.graphics.Bitmap bmp = BitmapFactory.decodeByteArray(
                        frame.jpegData, 0, frame.jpegData.length);

                if (bmp != null) {
                    // 同时更新AI叠加和原图
                    detectionOverlay.setImageBitmap(bmp);
                    imageRawVideo.setImageBitmap(bmp);

                    String info = String.format("帧#%d | %.1f fps | %dx%d | %dKB",
                            frame.frameNumber, currentFps,
                            bmp.getWidth(), bmp.getHeight(),
                            frame.jpegData.length / 1024);
                    textFrameInfo.setText(info);
                    textRawVideoInfo.setText(String.format("%dx%d | %.1f fps",
                            bmp.getWidth(), bmp.getHeight(), currentFps));
                }
            }
        } catch (Exception e) {
            appendLog("视频解码失败: " + e.getMessage());
        }
    }

    private void updateFrameRate() {
        long now = System.currentTimeMillis();
        frameCountSinceLastUpdate++;
        if (lastFrameTime == 0) { lastFrameTime = now; return; }
        long elapsed = now - lastFrameTime;
        if (elapsed >= 1000) {
            currentFps = frameCountSinceLastUpdate * 1000f / elapsed;
            frameCountSinceLastUpdate = 0;
            lastFrameTime = now;
        }
    }

    /**
     * 更新处理后数据
     */
    private void updateProcessedData(Protocol.SensorData data,
                                     TFLiteClassifier.ClassifyResult classifyResult) {
        if (data == null) return;

        // 最近距离 (激光+超声波融合)
        float minDist = data.getMinDistance();
        String distSource = data.getMinDistanceSource();

        // 危险等级
        String dangerText;
        int dangerColor;
        int level = data.level;
        if (minDist > 0 && minDist < AppConfig.DISTANCE_DANGER || level == 2) {
            dangerText = "⚠ 危险 (DANGER)";
            dangerColor = 0xFFF44336;
        } else if (minDist > 0 && minDist < AppConfig.DISTANCE_CAUTION || level == 1) {
            dangerText = "⚡ 注意 (CAUTION)";
            dangerColor = 0xFFFF9800;
        } else {
            dangerText = "✅ 安全 (SAFE)";
            dangerColor = 0xFF4CAF50;
        }

        textDangerLevel.setText("危险等级: " + dangerText);
        textDangerLevel.setTextColor(dangerColor);

        // 最近距离
        if (minDist > 0) {
            textMinDistance.setText(String.format("最近距离: %.2f m (来源: %s)", minDist, distSource));
        } else {
            textMinDistance.setText("最近距离: 无有效数据");
        }

        // 防抖状态
        if (level >= 2) {
            textDebounceInfo.setText("防抖状态: DANGER已确认 (需3帧连续)");
        } else {
            textDebounceInfo.setText("防抖状态: 正常");
        }

        // 视觉检测
        if (classifyResult != null && classifyResult.hasDetections()) {
            List<TFLiteClassifier.DetectedObject> dets = classifyResult.detections;
            StringBuilder sb = new StringBuilder();
            sb.append("视觉检测: ").append(dets.size()).append("个目标");
            for (TFLiteClassifier.DetectedObject det : dets) {
                sb.append(String.format("\n  %s %.0f%% [%.2f,%.2f]",
                        det.className, det.confidence * 100, det.centerX, det.centerY));
            }
            textDetectionCount.setText(sb.toString());
        } else if (classifyResult != null) {
            textDetectionCount.setText(String.format("视觉检测: 0个目标 (最高: %s %.0f%%)",
                    classifyResult.className, classifyResult.confidence * 100));
        } else {
            textDetectionCount.setText("视觉检测: 等待模型推理...");
        }
    }

    /**
     * 更新网络统计
     */
    private void updateNetworkStats() {
        if (activity == null) return;

        StringBuilder sb = new StringBuilder();

        // TCP 状态
        if (activity.getTcpClient() != null) {
            boolean connected = activity.getTcpClient().isConnected();
            sb.append("TCP(传感器): ").append(connected ? "🟢 已连接" : "🔴 未连接").append("\n");
        }

        // HTTP 摄像头状态
        if (httpCamera != null && isHttpCameraRunning) {
            sb.append(String.format("HTTP(摄像板): 🟢 %s/video | 帧数: %d | %.1f fps",
                    cameraIp, httpCamera.getFrameCount(), currentFps));
        } else {
            sb.append("HTTP(摄像板): 未启动");
        }

        textNetworkStats.setText(sb.toString());
    }

    // ==================== 日志 ====================

    public void appendLog(String log) {
        mainHandler.post(() -> {
            String timestamp = timeFormat.format(new Date());
            logBuilder.append("[").append(timestamp).append("] ")
                    .append(log).append("\n");

            String[] lines = logBuilder.toString().split("\n");
            if (lines.length > MAX_LOG_LINES) {
                logBuilder.setLength(0);
                for (int i = lines.length - MAX_LOG_LINES; i < lines.length; i++) {
                    logBuilder.append(lines[i]).append("\n");
                }
            }

            textLog.setText(logBuilder.toString());
            scrollLog.post(() -> scrollLog.fullScroll(ScrollView.FOCUS_DOWN));
        });
    }

    // ==================== HTTP 摄像头直连 ====================

    private void toggleHttpCamera() {
        if (isHttpCameraRunning) {
            stopHttpCamera();
        } else {
            startHttpCamera();
        }
    }

    private void startHttpCamera() {
        if (httpCamera == null) {
            httpCamera = new CameraHttpClient();
        }

        // 使用动态获取的摄像板IP
        String url = "http://" + cameraIp + "/video";
        httpCamera.setStreamUrl(url);

        httpCamera.setOnFrameListener(new CameraHttpClient.OnFrameListener() {
            @Override
            public void onFrame(byte[] jpegData, int frameNumber, long fetchTimeMs) {
                mainHandler.post(() -> {
                    android.graphics.Bitmap bmp = BitmapFactory.decodeByteArray(
                            jpegData, 0, jpegData.length);
                    if (bmp != null) {
                        // 更新原图显示
                        imageRawVideo.setImageBitmap(bmp);
                        textRawVideoInfo.setText(String.format(
                                "%dx%d | HTTP直连 | %dKB | #%d",
                                bmp.getWidth(), bmp.getHeight(),
                                jpegData.length / 1024, frameNumber));

                        // 同时更新AI检测叠加框的背景图
                        detectionOverlay.setImageBitmap(bmp);
                    }
                });

                // 更新帧率
                mainHandler.post(DebugFragment.this::updateFrameRate);
            }

            @Override
            public void onError(String error) {
                appendLog("HTTP摄像头: " + error);
            }

            @Override
            public void onStateChanged(boolean connected, String message) {
                if (!connected) {
                    appendLog("HTTP摄像头断开: " + message);
                }
            }
        });

        httpCamera.start();
        isHttpCameraRunning = true;
        updateHttpButtonState();
        appendLog("HTTP摄像头直连已开启 → " + url);
    }

    private void stopHttpCamera() {
        if (httpCamera != null) {
            httpCamera.stop();
        }
        isHttpCameraRunning = false;
        updateHttpButtonState();
    }

    private void updateHttpButtonState() {
        if (btnHttpCamera != null) {
            btnHttpCamera.setText(isHttpCameraRunning ? "关闭HTTP" : "HTTP直连");
            btnHttpCamera.setBackgroundTintList(
                    android.content.res.ColorStateList.valueOf(
                            isHttpCameraRunning ? 0xFF4CAF50 : 0xFF1565C0));
        }
    }
}
