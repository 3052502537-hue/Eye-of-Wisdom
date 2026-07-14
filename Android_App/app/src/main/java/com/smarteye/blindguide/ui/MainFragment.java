/**
 * ============================================================================
 * 文件名: MainFragment.java
 * 功能描述:
 *   - 主界面 Fragment，显示连接状态、当前模式、风险等级
 *   - 大字体、高对比度、大按钮，视障友好设计
 *   - 接收避障分析结果并更新 UI
 *   - 标题点击5次激活开发者模式
 *   - 提供紧急停止按钮和模式快速切换按钮
 * 依赖关系:
 *   - 依赖 MainActivity 提供的核心组件
 *   - 依赖 AppConfig 读取配置
 *   - 依赖 TTSManager 语音播报
 *   - 依赖 ObstacleAnalyzer 避障分析结果
 * 接口说明:
 *   - onCreateView: 加载布局
 *   - updateUI(): 更新界面显示
 *   - onAnalysisResult: 避障结果回调
 * ============================================================================
 */
package com.smarteye.blindguide.ui;

import android.graphics.Color;
import android.os.Bundle;
import android.view.LayoutInflater;
import android.view.View;
import android.view.ViewGroup;
import android.widget.Button;
import android.widget.TextView;

import androidx.annotation.NonNull;
import androidx.annotation.Nullable;
import androidx.fragment.app.Fragment;

import com.smarteye.blindguide.MainActivity;
import com.smarteye.blindguide.R;
import com.smarteye.blindguide.data.AppConfig;
import com.smarteye.blindguide.logic.ObstacleAnalyzer;
import com.smarteye.blindguide.network.TCPClient;
import com.smarteye.blindguide.tts.TTSManager;

/**
 * 主界面 Fragment
 * 显示核心状态信息，视障友好设计
 */
public class MainFragment extends Fragment implements ObstacleAnalyzer.OnAnalysisResultListener {

    /** 标题文本（点击5次激活调试模式） */
    private TextView textTitle;

    /** 连接状态文本 */
    private TextView textConnectionStatus;

    /** 当前模式文本 */
    private TextView textCurrentMode;

    /** 风险等级文本 */
    private TextView textRiskLevel;

    /** 障碍物信息文本 */
    private TextView textObstacleInfo;

    /** 建议方向文本 */
    private TextView textSuggestion;

    /** 紧急停止按钮 */
    private Button btnEmergencyStop;

    /** 模式切换按钮 */
    private Button btnSwitchMode;

    /** 语音控制按钮 */
    private Button btnVoiceControl;

    /** MainActivity 引用 */
    private MainActivity activity;

    /** 标题点击计数 */
    private int titleClickCount = 0;

    /** 标题上次点击时间 */
    private long lastTitleClickTime = 0;

    @Override
    public View onCreateView(@NonNull LayoutInflater inflater, @Nullable ViewGroup container,
                             @Nullable Bundle savedInstanceState) {
        View view = inflater.inflate(R.layout.fragment_main, container, false);

        // 绑定视图
        bindViews(view);

        // 设置点击监听
        setupClickListeners();

        return view;
    }

    /**
     * 绑定视图
     * @param view 根视图
     */
    private void bindViews(View view) {
        textTitle = view.findViewById(R.id.text_title);
        textConnectionStatus = view.findViewById(R.id.text_connection_status);
        textCurrentMode = view.findViewById(R.id.text_current_mode);
        textRiskLevel = view.findViewById(R.id.text_risk_level);
        textObstacleInfo = view.findViewById(R.id.text_obstacle_info);
        textSuggestion = view.findViewById(R.id.text_suggestion);
        btnEmergencyStop = view.findViewById(R.id.btn_emergency_stop);
        btnSwitchMode = view.findViewById(R.id.btn_switch_mode);
        btnVoiceControl = view.findViewById(R.id.btn_voice_control);
    }

    /**
     * 设置点击监听
     */
    private void setupClickListeners() {
        // 标题点击：5次激活调试模式
        textTitle.setOnClickListener(v -> onTitleClick());

        // 紧急停止按钮
        btnEmergencyStop.setOnClickListener(v -> {
            TTSManager.getInstance().stop();
            TTSManager.getInstance().speak("已停止播报", TTSManager.PRIORITY_HIGH);
        });

        // 模式切换按钮：循环切换三种模式
        btnSwitchMode.setOnClickListener(v -> {
            AppConfig config = AppConfig.getInstance();
            int currentMode = config.getCurrentMode();
            int nextMode;
            switch (currentMode) {
                case AppConfig.MODE_SENSOR_ONLY:
                    nextMode = AppConfig.MODE_AUTO;
                    break;
                case AppConfig.MODE_AUTO:
                    nextMode = AppConfig.MODE_RISK_ONLY;
                    break;
                case AppConfig.MODE_RISK_ONLY:
                    nextMode = AppConfig.MODE_SENSOR_ONLY;
                    break;
                default:
                    nextMode = AppConfig.MODE_AUTO;
            }
            config.setCurrentMode(nextMode);
            if (activity != null && activity.getAnalyzer() != null) {
                activity.getAnalyzer().setMode(nextMode);
            }
            updateUI();
            TTSManager.getInstance().speak("已切换到" + AppConfig.getModeName(nextMode),
                    TTSManager.PRIORITY_MEDIUM);
        });

        // 语音控制按钮
        btnVoiceControl.setOnClickListener(v -> {
            if (activity != null && activity.getVoiceControl() != null) {
                if (activity.getVoiceControl().isListening()) {
                    activity.getVoiceControl().stopListening();
                    TTSManager.getInstance().speak("语音控制已关闭", TTSManager.PRIORITY_MEDIUM);
                } else {
                    activity.getVoiceControl().startListening();
                    TTSManager.getInstance().speak("请说话", TTSManager.PRIORITY_MEDIUM);
                }
            }
        });
    }

    /**
     * 标题点击处理
     * 连续点击5次激活开发者模式
     */
    private void onTitleClick() {
        long now = System.currentTimeMillis();
        if (now - lastTitleClickTime > 2000) {
            titleClickCount = 0;
        }
        titleClickCount++;
        lastTitleClickTime = now;

        if (titleClickCount >= AppConfig.DEBUG_ACTIVATE_CLICK_COUNT) {
            if (activity != null) {
                activity.onTitleClick();
            }
            titleClickCount = 0;
        }
    }

    @Override
    public void onActivityCreated(@Nullable Bundle savedInstanceState) {
        super.onActivityCreated(savedInstanceState);
        activity = (MainActivity) getActivity();

        // 注册避障分析结果回调（使用 add 避免覆盖其他监听器）
        if (activity != null && activity.getAnalyzer() != null) {
            activity.getAnalyzer().addOnAnalysisResultListener(this);
        }

        // 注册 TCP 连接状态回调
        if (activity != null && activity.getTcpClient() != null) {
            activity.getTcpClient().setOnConnectionStateListener((state, message) -> {
                if (getActivity() != null) {
                    getActivity().runOnUiThread(() -> {
                        textConnectionStatus.setText(message);
                        // 根据连接状态设置颜色
                        int color;
                        switch (state) {
                            case TCPClient.STATE_CONNECTED:
                                color = Color.parseColor("#4CAF50"); // 绿色
                                break;
                            case TCPClient.STATE_CONNECTING:
                                color = Color.parseColor("#FF9800"); // 橙色
                                break;
                            default:
                                color = Color.parseColor("#F44336"); // 红色
                                break;
                        }
                        textConnectionStatus.setTextColor(color);
                    });
                }
            });
        }

        updateUI();
    }

    /**
     * 更新 UI 显示
     */
    private void updateUI() {
        AppConfig config = AppConfig.getInstance();

        // 当前模式
        int mode = config.getCurrentMode();
        textCurrentMode.setText("当前模式：" + AppConfig.getModeName(mode));

        // 风险等级
        if (activity != null && activity.getAnalyzer() != null) {
            int risk = activity.getAnalyzer().getCurrentRiskLevel();
            updateRiskLevel(risk);
        }
    }

    /**
     * 更新风险等级显示
     * @param risk 风险等级常量
     */
    private void updateRiskLevel(int risk) {
        String riskText = AppConfig.getRiskName(risk);
        textRiskLevel.setText("风险等级：" + riskText);

        int bgColor;
        switch (risk) {
            case AppConfig.RISK_SAFE:
                bgColor = Color.parseColor("#4CAF50"); // 绿色
                break;
            case AppConfig.RISK_CAUTION:
                bgColor = Color.parseColor("#FF9800"); // 橙色
                break;
            case AppConfig.RISK_DANGER:
                bgColor = Color.parseColor("#F44336"); // 红色
                break;
            default:
                bgColor = Color.GRAY;
                break;
        }
        textRiskLevel.setBackgroundColor(bgColor);
    }

    // ==================== 避障分析结果回调 ====================

    @Override
    public void onResult(ObstacleAnalyzer.AnalysisResult result) {
        if (getActivity() == null) return;

        getActivity().runOnUiThread(() -> {
            // 更新障碍物信息
            if (result.obstacleDescription != null && !result.obstacleDescription.isEmpty()) {
                textObstacleInfo.setText("障碍物：" + result.obstacleDescription);
            }

            // 更新建议方向
            if (result.suggestedDirection != null) {
                textSuggestion.setText("建议：" + result.suggestedDirection);
            }

            // 更新风险等级
            updateRiskLevel(result.riskLevel);
        });
    }

    @Override
    public void onRiskChanged(int newRisk, int oldRisk) {
        // 风险等级变化时的处理（已在 onResult 中更新 UI）
    }

    @Override
    public void onResume() {
        super.onResume();
        updateUI();
    }
}
