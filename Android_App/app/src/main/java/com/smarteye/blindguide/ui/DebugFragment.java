/**
 * ============================================================================
 * 文件名: DebugFragment.java
 * 功能描述:
 *   - 调试页面 Fragment（默认隐藏，开发者模式激活后可见）
 *   - 显示 TCP 接收的原始传感器数据
 *   - 显示 UDP 接收的图像帧
 *   - 显示 TFLite 识别结果和置信度
 *   - 显示运行日志（连接状态、错误信息等）
 *   - 提供网络统计、帧率、丢包率等调试信息
 * 依赖关系:
 *   - 依赖 MainActivity 提供核心组件
 *   - 依赖 TCPClient、UDPReceiver 网络数据
 *   - 依赖 TFLiteClassifier 识别结果
 *   - 依赖 ObstacleAnalyzer 分析结果
 * 接口说明:
 *   - onCreateView: 加载布局
 *   - updateSensorData: 更新传感器数据显示
 *   - updateImageFrame: 更新图像显示
 *   - appendLog: 追加日志信息
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
import com.smarteye.blindguide.network.Protocol;
import com.smarteye.blindguide.network.TCPClient;
import com.smarteye.blindguide.network.UDPReceiver;

import java.text.SimpleDateFormat;
import java.util.Date;
import java.util.Locale;

/**
 * 调试页面 Fragment
 * 显示原始数据、图像、日志等调试信息
 */
public class DebugFragment extends Fragment {

    /** 原始传感器数据显示 */
    private TextView textRawSensor;

    /** 图像显示 */
    private ImageView imagePreview;

    /** 识别结果显示 */
    private TextView textClassifyResult;

    /** 避障分析结果显示 */
    private TextView textAnalysisResult;

    /** 网络统计信息 */
    private TextView textNetworkStats;

    /** 日志显示 */
    private TextView textLog;

    /** 日志滚动容器 */
    private ScrollView scrollLog;

    /** 清空日志按钮 */
    private Button btnClearLog;

    /** 重置统计按钮 */
    private Button btnResetStats;

    /** MainActivity 引用 */
    private MainActivity activity;

    /** 主线程 Handler，用于更新 UI */
    private final Handler mainHandler = new Handler(Looper.getMainLooper());

    /** 时间格式化 */
    private final SimpleDateFormat timeFormat =
            new SimpleDateFormat("HH:mm:ss.SSS", Locale.getDefault());

    /** 日志最大行数 */
    private static final int MAX_LOG_LINES = 200;

    /** 日志缓冲区 */
    private final StringBuilder logBuilder = new StringBuilder();

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
     * @param view 根视图
     */
    private void bindViews(View view) {
        textRawSensor = view.findViewById(R.id.text_raw_sensor);
        imagePreview = view.findViewById(R.id.image_preview);
        textClassifyResult = view.findViewById(R.id.text_classify_result);
        textAnalysisResult = view.findViewById(R.id.text_analysis_result);
        textNetworkStats = view.findViewById(R.id.text_network_stats);
        textLog = view.findViewById(R.id.text_log);
        scrollLog = view.findViewById(R.id.scroll_log);
        btnClearLog = view.findViewById(R.id.btn_clear_log);
        btnResetStats = view.findViewById(R.id.btn_reset_stats);
    }

    /**
     * 设置监听器
     */
    private void setupListeners() {
        // 清空日志按钮
        btnClearLog.setOnClickListener(v -> {
            logBuilder.setLength(0);
            textLog.setText("");
            appendLog("日志已清空");
        });

        // 重置统计按钮
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

        registerCallbacks();
        appendLog("调试页面已就绪");
    }

    /**
     * 注册各种数据回调
     */
    private void registerCallbacks() {
        if (activity == null) return;

        // TCP 原始数据回调
        if (activity.getTcpClient() != null) {
            activity.getTcpClient().setOnSensorDataListener(new TCPClient.OnSensorDataListener() {
                @Override
                public void onSensorData(Protocol.SensorData data) {
                    updateSensorData(data);
                }

                @Override
                public void onRawData(String rawJson) {
                    appendLog("TCP: " + rawJson);
                }
            });

            // 连接状态回调
            activity.getTcpClient().setOnConnectionStateListener((state, message) -> {
                appendLog("TCP状态: " + message);
            });
        }

        // UDP 图像帧回调
        if (activity.getUdpReceiver() != null) {
            activity.getUdpReceiver().setOnImageFrameListener(frame -> {
                updateImageFrame(frame);
                updateNetworkStats();
            });

            activity.getUdpReceiver().setOnReceiveStateListener((state, message) -> {
                appendLog("UDP状态: " + message);
            });
        }

        // 避障分析结果回调
        if (activity.getAnalyzer() != null) {
            activity.getAnalyzer().setOnAnalysisResultListener(new ObstacleAnalyzer.OnAnalysisResultListener() {
                @Override
                public void onResult(ObstacleAnalyzer.AnalysisResult result) {
                    updateAnalysisResult(result);
                }

                @Override
                public void onRiskChanged(int newRisk, int oldRisk) {
                    appendLog("风险变化: " + oldRisk + " -> " + newRisk);
                }
            });
        }
    }

    /**
     * 更新传感器数据显示
     * @param data 传感器数据
     */
    private void updateSensorData(Protocol.SensorData data) {
        mainHandler.post(() -> {
            StringBuilder sb = new StringBuilder();
            sb.append("=== 传感器数据 ===\n");
            sb.append(String.format("时间: %s\n",
                    timeFormat.format(new Date(data.timestamp))));
            sb.append(String.format("前方激光: %.2f 米\n", data.laser_front));

            if (data.radar_front != null) {
                sb.append(String.format("前方雷达: %s\n", data.radar_front.toString()));
            }
            if (data.radar_back != null) {
                sb.append(String.format("后方雷达: %s\n", data.radar_back.toString()));
            }

            sb.append(String.format("电池电量: %d%%\n", data.battery));
            sb.append(String.format("工作模式: %s\n", AppConfig.getModeName(data.mode)));

            textRawSensor.setText(sb.toString());
        });
    }

    /**
     * 更新图像显示
     * @param frame 图像帧
     */
    private void updateImageFrame(Protocol.ImageFrame frame) {
        mainHandler.post(() -> {
            try {
                // 解码 JPEG 并显示
                if (frame.jpegData != null && frame.jpegData.length > 0) {
                    imagePreview.setImageBitmap(
                            BitmapFactory.decodeByteArray(frame.jpegData, 0, frame.jpegData.length));
                }
            } catch (Exception e) {
                appendLog("图像解码失败: " + e.getMessage());
            }

            // 调用 TFLite 识别并显示结果
            if (activity != null && activity.getClassifier() != null
                    && activity.getClassifier().isInitialized()) {
                activity.getClassifier().classify(frame.jpegData,
                        new TFLiteClassifier.OnClassifyResultListener() {
                            @Override
                            public void onResult(TFLiteClassifier.ClassifyResult result) {
                                updateClassifyResult(result);
                            }

                            @Override
                            public void onError(String error) {
                                appendLog("识别错误: " + error);
                            }
                        });
            }
        });
    }

    /**
     * 更新识别结果显示
     * @param result 识别结果
     */
    private void updateClassifyResult(TFLiteClassifier.ClassifyResult result) {
        mainHandler.post(() -> {
            StringBuilder sb = new StringBuilder();
            sb.append("=== 视觉识别 ===\n");
            sb.append(String.format("类别: %s\n", result.className));
            sb.append(String.format("置信度: %.2f%%\n", result.confidence * 100));
            sb.append(String.format("耗时: %d ms\n", result.inferenceTime));

            if (result.probabilities != null) {
                sb.append("--- 各类别概率 ---\n");
                java.util.List<String> labels = activity.getClassifier().getLabels();
                for (int i = 0; i < result.probabilities.length && i < labels.size(); i++) {
                    sb.append(String.format("%s: %.2f%%\n",
                            labels.get(i), result.probabilities[i] * 100));
                }
            }

            textClassifyResult.setText(sb.toString());
        });
    }

    /**
     * 更新避障分析结果显示
     * @param result 分析结果
     */
    private void updateAnalysisResult(ObstacleAnalyzer.AnalysisResult result) {
        mainHandler.post(() -> {
            StringBuilder sb = new StringBuilder();
            sb.append("=== 避障分析 ===\n");
            sb.append(String.format("风险等级: %s\n", AppConfig.getRiskName(result.riskLevel)));
            sb.append(String.format("障碍物: %s\n", result.obstacleDescription));
            sb.append(String.format("建议: %s\n", result.suggestedDirection));
            sb.append(String.format("置信度: %.2f\n", result.confidence));

            textAnalysisResult.setText(sb.toString());
        });
    }

    /**
     * 更新网络统计信息
     */
    private void updateNetworkStats() {
        mainHandler.post(() -> {
            if (activity == null || activity.getUdpReceiver() == null) return;

            UDPReceiver udp = activity.getUdpReceiver();
            StringBuilder sb = new StringBuilder();
            sb.append("=== 网络统计 ===\n");
            sb.append(String.format("TCP状态: %s\n",
                    activity.getTcpClient() != null && activity.getTcpClient().isConnected()
                            ? "已连接" : "未连接"));
            sb.append(String.format("UDP接收帧数: %d\n", udp.getFrameCount()));
            sb.append(String.format("UDP丢包数: %d\n", udp.getLostPackets()));
            sb.append(String.format("丢包率: %.2f%%\n", udp.getPacketLossRate() * 100));

            textNetworkStats.setText(sb.toString());
        });
    }

    /**
     * 追加日志信息
     * @param log 日志内容
     */
    public void appendLog(String log) {
        mainHandler.post(() -> {
            String timestamp = timeFormat.format(new Date());
            logBuilder.append("[").append(timestamp).append("] ")
                    .append(log).append("\n");

            // 限制日志行数，避免内存溢出
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

    @Override
    public void onResume() {
        super.onResume();
        updateNetworkStats();
    }
}
