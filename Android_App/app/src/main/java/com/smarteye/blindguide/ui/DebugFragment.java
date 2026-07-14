/**
 * ============================================================================
 * 文件名: DebugFragment.java
 * 功能描述:
 *   - 开发者调试页面 Fragment（连续点击标题5次激活）
 *   - 通过 DebugDataListener 接口从 MainActivity 获取数据（不覆盖主回调）
 *   - 视频预览：DetectionOverlayView 显示 JPEG + AI 检测框叠加
 *   - 原始传感器数据：激光/前后雷达的驱动层原始输出
 *   - 处理后数据：危险等级/最近距离/防抖状态/避障建议/视觉检测数量
 *   - 网络统计：TCP状态/UDP帧数/丢包率
 *   - 运行日志：带时间戳的调试信息
 * 依赖关系:
 *   - 实现 MainActivity.DebugDataListener 接口
 *   - 依赖 DetectionOverlayView 自定义 View
 *   - 依赖 AppConfig 读取阈值配置
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
import com.smarteye.blindguide.network.Protocol;
import com.smarteye.blindguide.network.UDPReceiver;

import java.text.SimpleDateFormat;
import java.util.Date;
import java.util.List;
import java.util.Locale;

/**
 * 开发者调试页面 Fragment
 * 通过 DebugDataListener 获取数据，不覆盖主业务回调
 */
public class DebugFragment extends Fragment implements MainActivity.DebugDataListener {

    // ==================== 视图引用 ====================

    /** 视频预览 + AI 检测框叠加 */
    private DetectionOverlayView detectionOverlay;

    /** 帧信息 */
    private TextView textFrameInfo;

    /** 原始传感器数据 */
    private TextView textRawLaser;
    private TextView textRawRadarFront;
    private TextView textRawRadarRear;
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

    // ==================== 状态数据 ====================

    /** MainActivity 引用 */
    private MainActivity activity;

    /** 主线程 Handler */
    private final Handler mainHandler = new Handler(Looper.getMainLooper());

    /** 时间格式化 */
    private final SimpleDateFormat timeFormat =
            new SimpleDateFormat("HH:mm:ss.SSS", Locale.getDefault());

    /** 日志最大行数 */
    private static final int MAX_LOG_LINES = 200;

    /** 日志缓冲区 */
    private final StringBuilder logBuilder = new StringBuilder();

    /** 帧率统计 */
    private long lastFrameTime = 0;
    private int frameCountSinceLastUpdate = 0;
    private float currentFps = 0;

    /** 最近一次传感器数据（用于处理后数据显示） */
    private Protocol.SensorData lastSensorData;

    /** 最近一次识别结果 */
    private TFLiteClassifier.ClassifyResult lastClassifyResult;

    // ==================== 生命周期 ====================

    @Override
    public View onCreateView(@NonNull LayoutInflater inflater, @Nullable ViewGroup container,
                             @Nullable Bundle savedInstanceState) {
        View view = inflater.inflate(R.layout.fragment_debug, container, false);
        bindViews(view);
        setupListeners();
        return view;
    }

    /**
     * 绑定视图
     */
    private void bindViews(View view) {
        // 视频预览
        detectionOverlay = view.findViewById(R.id.detection_overlay);
        textFrameInfo = view.findViewById(R.id.text_frame_info);

        // 原始传感器数据
        textRawLaser = view.findViewById(R.id.text_raw_laser);
        textRawRadarFront = view.findViewById(R.id.text_raw_radar_front);
        textRawRadarRear = view.findViewById(R.id.text_raw_radar_rear);
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
    }

    /**
     * 设置按钮监听器
     */
    private void setupListeners() {
        btnClearLog.setOnClickListener(v -> {
            logBuilder.setLength(0);
            textLog.setText("");
            appendLog("日志已清空");
        });

        btnResetStats.setOnClickListener(v -> {
            if (activity != null && activity.getUdpReceiver() != null) {
                activity.getUdpReceiver().resetStatistics();
                updateNetworkStats();
                appendLog("统计已重置");
            }
        });
    }

    @Override
    public void onActivityCreated(@Nullable Bundle savedInstanceState) {
        super.onActivityCreated(savedInstanceState);
        activity = (MainActivity) getActivity();
    }

    @Override
    public void onResume() {
        super.onResume();
        // 注册为调试数据监听器（不覆盖主回调）
        if (activity != null) {
            activity.addDebugDataListener(this);
        }
        updateNetworkStats();
        appendLog("调试页面已就绪");
        appendLog("监听模式: 附加监听 (主回调不受影响)");
    }

    @Override
    public void onPause() {
        super.onPause();
        // 注销调试数据监听器
        if (activity != null) {
            activity.removeDebugDataListener(this);
        }
    }

    // ==================== DebugDataListener 接口实现 ====================

    @Override
    public void onSensorData(Protocol.SensorData data) {
        this.lastSensorData = data;
        mainHandler.post(() -> {
            updateRawSensorData(data);
            updateProcessedData(data, null);
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
            // 更新检测框叠加层
            if (result.hasDetections()) {
                detectionOverlay.setDetections(result.detections);
            }
            // 更新处理后数据中的视觉检测信息
            updateProcessedData(lastSensorData, result);
        });
    }

    @Override
    public void onAnalysisResult(ObstacleAnalyzer.AnalysisResult result) {
        mainHandler.post(() -> {
            updateAnalysisResult(result);
        });
    }

    // ==================== 数据更新方法 ====================

    /**
     * 更新视频预览
     */
    private void updateVideoPreview(Protocol.ImageFrame frame) {
        try {
            if (frame.jpegData != null && frame.jpegData.length > 0) {
                // 解码 JPEG 并设置到底图
                detectionOverlay.setImageBitmap(
                        BitmapFactory.decodeByteArray(frame.jpegData, 0, frame.jpegData.length));

                // 更新帧信息
                textFrameInfo.setText(String.format("帧#%d | %.1f fps",
                        frame.frameNumber, currentFps));
            }
        } catch (Exception e) {
            appendLog("视频解码失败: " + e.getMessage());
        }
    }

    /**
     * 计算并更新帧率
     */
    private void updateFrameRate() {
        long now = System.currentTimeMillis();
        frameCountSinceLastUpdate++;

        if (lastFrameTime == 0) {
            lastFrameTime = now;
            return;
        }

        long elapsed = now - lastFrameTime;
        if (elapsed >= 1000) { // 每秒更新一次帧率
            currentFps = frameCountSinceLastUpdate * 1000f / elapsed;
            frameCountSinceLastUpdate = 0;
            lastFrameTime = now;
        }
    }

    /**
     * 更新原始传感器数据显示
     */
    private void updateRawSensorData(Protocol.SensorData data) {
        // 激光原始数据
        textRawLaser.setText(String.format("激光测距: %.2f m | 电池: %d%% | 模式: %s",
                data.laser_front, data.battery, AppConfig.getModeName(data.mode)));

        // 前雷达原始数据
        if (data.radar_front != null) {
            textRawRadarFront.setText(String.format(
                    "前雷达: 距离=%.2fm | 速度=%.2fm/s | 角度=%.1f°",
                    data.radar_front.dist, data.radar_front.speed, data.radar_front.angle));
        } else {
            textRawRadarFront.setText("前雷达: 无数据");
        }

        // 后雷达原始数据
        if (data.radar_back != null) {
            textRawRadarRear.setText(String.format(
                    "后雷达: 距离=%.2fm | 速度=%.2fm/s | 角度=%.1f°",
                    data.radar_back.dist, data.radar_back.speed, data.radar_back.angle));
        } else {
            textRawRadarRear.setText("后雷达: 无数据");
        }

        // 时间戳
        textRawTimestamp.setText(String.format("帧时间戳: %s | ESP32 level=%d img=%d",
                timeFormat.format(new Date(data.timestamp)), data.level, data.img));
    }

    /**
     * 更新处理后数据（从传感器+视觉结果综合计算）
     */
    private void updateProcessedData(Protocol.SensorData data,
                                     TFLiteClassifier.ClassifyResult classifyResult) {
        if (data == null) return;

        // 计算最近距离
        float minDist = Float.MAX_VALUE;
        String distSource = "无";
        if (data.laser_front > 0) {
            minDist = data.laser_front;
            distSource = "激光";
        }
        if (data.radar_front != null && data.radar_front.dist > 0
                && data.radar_front.dist < minDist) {
            minDist = data.radar_front.dist;
            distSource = "前雷达";
        }

        // 危险等级（优先使用 ESP32 发送的 level，其次本地计算）
        String dangerText;
        int dangerColor;
        int level = data.level;
        if (minDist < AppConfig.DISTANCE_DANGER || level == 2) {
            dangerText = "⚠ 危险 (DANGER)";
            dangerColor = 0xFFF44336;
        } else if (minDist < AppConfig.DISTANCE_CAUTION || level == 1) {
            dangerText = "⚡ 注意 (CAUTION)";
            dangerColor = 0xFFFF9800;
        } else {
            dangerText = "✅ 安全 (SAFE)";
            dangerColor = 0xFF4CAF50;
        }

        textDangerLevel.setText("危险等级: " + dangerText);
        textDangerLevel.setTextColor(dangerColor);

        // 最近距离
        if (minDist < Float.MAX_VALUE) {
            textMinDistance.setText(String.format("最近距离: %.2f m (来源: %s)", minDist, distSource));
        } else {
            textMinDistance.setText("最近距离: 无有效数据");
        }

        // 防抖状态（ESP32 侧 WARN_DEBOUNCE_FRAMES=3）
        if (level >= 2) {
            textDebounceInfo.setText("防抖状态: DANGER已确认 (需3帧连续)");
        } else {
            textDebounceInfo.setText("防抖状态: 正常 (未触发DANGER防抖)");
        }

        // 视觉检测数量
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
     * 更新避障分析结果
     */
    private void updateAnalysisResult(ObstacleAnalyzer.AnalysisResult result) {
        if (result == null) return;

        // 避障建议
        if (result.adviceText != null && !result.adviceText.isEmpty()) {
            textAdvice.setText("避障建议: " + result.adviceText);
        }

        // 从分析结果更新一些处理后的信息
        if (result.obstacleDescription != null && !result.obstacleDescription.isEmpty()) {
            // 障碍物描述可以补充显示
        }
    }

    /**
     * 更新网络统计信息
     */
    private void updateNetworkStats() {
        if (activity == null) return;

        StringBuilder sb = new StringBuilder();

        // TCP 状态
        if (activity.getTcpClient() != null) {
            boolean connected = activity.getTcpClient().isConnected();
            sb.append("TCP: ").append(connected ? "🟢 已连接" : "🔴 未连接").append("\n");
        }

        // UDP 统计
        if (activity.getUdpReceiver() != null) {
            UDPReceiver udp = activity.getUdpReceiver();
            sb.append(String.format("UDP帧数: %d | 丢包: %d | 丢包率: %.1f%% | 帧率: %.1f fps",
                    udp.getFrameCount(), udp.getLostPackets(),
                    udp.getPacketLossRate() * 100, currentFps));
        } else {
            sb.append("UDP: 未启动");
        }

        textNetworkStats.setText(sb.toString());
    }

    // ==================== 日志方法 ====================

    /**
     * 追加日志信息
     * @param log 日志内容
     */
    public void appendLog(String log) {
        mainHandler.post(() -> {
            String timestamp = timeFormat.format(new Date());
            logBuilder.append("[").append(timestamp).append("] ")
                    .append(log).append("\n");

            // 限制日志行数
            String[] lines = logBuilder.toString().split("\n");
            if (lines.length > MAX_LOG_LINES) {
                logBuilder.setLength(0);
                for (int i = lines.length - MAX_LOG_LINES; i < lines.length; i++) {
                    logBuilder.append(lines[i]).append("\n");
                }
            }

            textLog.setText(logBuilder.toString());
            // 滚动到底部
            scrollLog.post(() -> scrollLog.fullScroll(ScrollView.FOCUS_DOWN));
        });
    }
}
