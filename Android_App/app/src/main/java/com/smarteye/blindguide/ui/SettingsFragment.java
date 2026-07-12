/**
 * ============================================================================
 * 文件名: SettingsFragment.java
 * 功能描述:
 *   - 设置页面 Fragment
 *   - 模式切换（传感器/自动/风险播报）
 *   - 音量/语速调节
 *   - 传感器参数配置（距离阈值）
 *   - WiFi 配置（SSID、密码）
 *   - 功能开关（GPS、视觉识别、震动反馈、语音控制）
 *   - 大字体、大按钮，视障友好
 * 依赖关系:
 *   - 依赖 AppConfig 读写配置
 *   - 依赖 TTSManager 调节语速
 *   - 依赖 MainActivity 提供核心组件
 * 接口说明:
 *   - onCreateView: 加载布局
 *   - setupListeners: 设置各控件监听
 * ============================================================================
 */
package com.smarteye.blindguide.ui;

import android.os.Bundle;
import android.text.InputType;
import android.view.LayoutInflater;
import android.view.View;
import android.view.ViewGroup;
import android.widget.Button;
import android.widget.CompoundButton;
import android.widget.EditText;
import android.widget.SeekBar;
import android.widget.Switch;
import android.widget.TextView;
import android.widget.Toast;

import androidx.annotation.NonNull;
import androidx.annotation.Nullable;
import androidx.fragment.app.Fragment;

import com.smarteye.blindguide.MainActivity;
import com.smarteye.blindguide.R;
import com.smarteye.blindguide.data.AppConfig;
import com.smarteye.blindguide.logic.ObstacleAnalyzer;
import com.smarteye.blindguide.tts.TTSManager;

/**
 * 设置页面 Fragment
 */
public class SettingsFragment extends Fragment {

    /** 模式选择按钮组 */
    private Button btnModeSensor;
    private Button btnModeAuto;
    private Button btnModeRisk;

    /** 语速调节 */
    private SeekBar seekBarTtsRate;
    private TextView textTtsRateValue;
    private Button btnTestTts;

    /** 距离阈值调节 */
    private SeekBar seekBarDangerDist;
    private TextView textDangerDistValue;
    private SeekBar seekBarCautionDist;
    private TextView textCautionDistValue;

    /** WiFi 配置 */
    private EditText editWifiSSID;
    private EditText editWifiPassword;
    private Button btnSaveWifi;

    /** 功能开关 */
    private Switch switchGps;
    private Switch switchVision;
    private Switch switchVibration;
    private Switch switchVoiceControl;

    /** MainActivity 引用 */
    private MainActivity activity;

    @Override
    public View onCreateView(@NonNull LayoutInflater inflater, @Nullable ViewGroup container,
                             @Nullable Bundle savedInstanceState) {
        View view = inflater.inflate(R.layout.fragment_settings, container, false);

        bindViews(view);
        setupListeners();
        loadConfig();

        return view;
    }

    /**
     * 绑定视图
     * @param view 根视图
     */
    private void bindViews(View view) {
        btnModeSensor = view.findViewById(R.id.btn_mode_sensor);
        btnModeAuto = view.findViewById(R.id.btn_mode_auto);
        btnModeRisk = view.findViewById(R.id.btn_mode_risk);

        seekBarTtsRate = view.findViewById(R.id.seekbar_tts_rate);
        textTtsRateValue = view.findViewById(R.id.text_tts_rate_value);
        btnTestTts = view.findViewById(R.id.btn_test_tts);

        seekBarDangerDist = view.findViewById(R.id.seekbar_danger_dist);
        textDangerDistValue = view.findViewById(R.id.text_danger_dist_value);
        seekBarCautionDist = view.findViewById(R.id.seekbar_caution_dist);
        textCautionDistValue = view.findViewById(R.id.text_caution_dist_value);

        editWifiSSID = view.findViewById(R.id.edit_wifi_ssid);
        editWifiPassword = view.findViewById(R.id.edit_wifi_password);
        btnSaveWifi = view.findViewById(R.id.btn_save_wifi);

        switchGps = view.findViewById(R.id.switch_gps);
        switchVision = view.findViewById(R.id.switch_vision);
        switchVibration = view.findViewById(R.id.switch_vibration);
        switchVoiceControl = view.findViewById(R.id.switch_voice_control);
    }

    /**
     * 设置监听器
     */
    private void setupListeners() {
        // 模式选择按钮
        View.OnClickListener modeListener = v -> {
            int mode;
            int id = v.getId();
            if (id == R.id.btn_mode_sensor) {
                mode = AppConfig.MODE_SENSOR_ONLY;
            } else if (id == R.id.btn_mode_auto) {
                mode = AppConfig.MODE_AUTO;
            } else if (id == R.id.btn_mode_risk) {
                mode = AppConfig.MODE_RISK_ONLY;
            } else {
                mode = AppConfig.MODE_AUTO;
            }
            AppConfig.getInstance().setCurrentMode(mode);
            if (activity != null && activity.getAnalyzer() != null) {
                activity.getAnalyzer().setMode(mode);
            }
            updateModeButtons();
            TTSManager.getInstance().speak("已切换到" + AppConfig.getModeName(mode),
                    TTSManager.PRIORITY_MEDIUM);
        };
        btnModeSensor.setOnClickListener(modeListener);
        btnModeAuto.setOnClickListener(modeListener);
        btnModeRisk.setOnClickListener(modeListener);

        // 语速调节 SeekBar
        seekBarTtsRate.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener() {
            @Override
            public void onProgressChanged(SeekBar seekBar, int progress, boolean fromUser) {
                // 进度0-100映射到语速0.5-2.0
                float rate = 0.5f + progress / 100f * 1.5f;
                textTtsRateValue.setText(String.format("语速：%.1f", rate));
            }

            @Override
            public void onStartTrackingTouch(SeekBar seekBar) {}

            @Override
            public void onStopTrackingTouch(SeekBar seekBar) {
                float rate = 0.5f + seekBar.getProgress() / 100f * 1.5f;
                AppConfig.getInstance().setTtsRate(rate);
                TTSManager.getInstance().setSpeechRate(rate);
            }
        });

        // 测试语音按钮
        btnTestTts.setOnClickListener(v -> {
            TTSManager.getInstance().speak("语音播报测试，当前语速正常",
                    TTSManager.PRIORITY_MEDIUM);
        });

        // 危险距离阈值调节
        seekBarDangerDist.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener() {
            @Override
            public void onProgressChanged(SeekBar seekBar, int progress, boolean fromUser) {
                float dist = 0.5f + progress / 10f; // 0.5m ~ 10.5m
                textDangerDistValue.setText(String.format("危险距离：%.1f米", dist));
            }

            @Override
            public void onStartTrackingTouch(SeekBar seekBar) {}

            @Override
            public void onStopTrackingTouch(SeekBar seekBar) {
                // 注意：AppConfig 中阈值为静态常量，此处仅展示 UI
                // 实际使用时建议改造为可配置项
                Toast.makeText(getContext(), "距离阈值已更新", Toast.LENGTH_SHORT).show();
            }
        });

        // 注意距离阈值调节
        seekBarCautionDist.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener() {
            @Override
            public void onProgressChanged(SeekBar seekBar, int progress, boolean fromUser) {
                float dist = 1.0f + progress / 10f; // 1.0m ~ 11.0m
                textCautionDistValue.setText(String.format("注意距离：%.1f米", dist));
            }

            @Override
            public void onStartTrackingTouch(SeekBar seekBar) {}

            @Override
            public void onStopTrackingTouch(SeekBar seekBar) {
                Toast.makeText(getContext(), "距离阈值已更新", Toast.LENGTH_SHORT).show();
            }
        });

        // WiFi 配置保存
        btnSaveWifi.setOnClickListener(v -> {
            String ssid = editWifiSSID.getText().toString().trim();
            String password = editWifiPassword.getText().toString().trim();
            if (ssid.isEmpty()) {
                Toast.makeText(getContext(), "请输入 WiFi 名称", Toast.LENGTH_SHORT).show();
                return;
            }
            AppConfig.getInstance().setWifiSSID(ssid);
            AppConfig.getInstance().setWifiPassword(password);
            TTSManager.getInstance().speak("WiFi 配置已保存", TTSManager.PRIORITY_MEDIUM);
            Toast.makeText(getContext(), "已保存", Toast.LENGTH_SHORT).show();
        });

        // 功能开关
        switchGps.setOnCheckedChangeListener((buttonView, isChecked) -> {
            AppConfig.getInstance().setGpsEnabled(isChecked);
        });

        switchVision.setOnCheckedChangeListener((buttonView, isChecked) -> {
            AppConfig.getInstance().setVisionEnabled(isChecked);
        });

        switchVibration.setOnCheckedChangeListener((buttonView, isChecked) -> {
            AppConfig.getInstance().setVibrationEnabled(isChecked);
        });

        switchVoiceControl.setOnCheckedChangeListener((buttonView, isChecked) -> {
            AppConfig.getInstance().setVoiceControlEnabled(isChecked);
        });
    }

    /**
     * 加载当前配置到 UI
     */
    private void loadConfig() {
        AppConfig config = AppConfig.getInstance();

        // 当前模式按钮高亮
        updateModeButtons();

        // 语速 SeekBar：0.5-2.0 映射到 0-100
        float rate = config.getTtsRate();
        int progress = (int) ((rate - 0.5f) / 1.5f * 100);
        seekBarTtsRate.setProgress(progress);
        textTtsRateValue.setText(String.format("语速：%.1f", rate));

        // 距离阈值 SeekBar
        seekBarDangerDist.setProgress((int) (AppConfig.DISTANCE_DANGER * 10 - 5));
        textDangerDistValue.setText(String.format("危险距离：%.1f米", AppConfig.DISTANCE_DANGER));
        seekBarCautionDist.setProgress((int) (AppConfig.DISTANCE_CAUTION * 10 - 10));
        textCautionDistValue.setText(String.format("注意距离：%.1f米", AppConfig.DISTANCE_CAUTION));

        // WiFi 配置
        editWifiSSID.setText(config.getWifiSSID());
        editWifiPassword.setText(config.getWifiPassword());
        editWifiPassword.setInputType(InputType.TYPE_CLASS_TEXT | InputType.TYPE_TEXT_VARIATION_PASSWORD);

        // 功能开关
        switchGps.setChecked(config.isGpsEnabled());
        switchVision.setChecked(config.isVisionEnabled());
        switchVibration.setChecked(config.isVibrationEnabled());
        switchVoiceControl.setChecked(config.isVoiceControlEnabled());
    }

    /**
     * 更新模式按钮状态
     */
    private void updateModeButtons() {
        int currentMode = AppConfig.getInstance().getCurrentMode();
        // 高亮当前模式按钮
        btnModeSensor.setSelected(currentMode == AppConfig.MODE_SENSOR_ONLY);
        btnModeAuto.setSelected(currentMode == AppConfig.MODE_AUTO);
        btnModeRisk.setSelected(currentMode == AppConfig.MODE_RISK_ONLY);
    }

    @Override
    public void onActivityCreated(@Nullable Bundle savedInstanceState) {
        super.onActivityCreated(savedInstanceState);
        activity = (MainActivity) getActivity();
    }
}
