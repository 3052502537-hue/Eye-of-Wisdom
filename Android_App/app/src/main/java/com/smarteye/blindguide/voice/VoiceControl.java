/**
 * ============================================================================
 * 文件名: VoiceControl.java
 * 功能描述:
 *   - 语音控制模块（可选功能），使用 Android SpeechRecognizer 识别用户语音指令
 *   - 支持指令：切换模式、查询状态、停止播报、开启/关闭功能等
 *   - 预留指令扩展接口，便于后续添加自定义指令
 * 依赖关系:
 *   - 依赖 Android SpeechRecognizer API
 *   - 依赖 TTSManager 进行语音反馈
 *   - 依赖 AppConfig 管理启用状态
 *   - 被 MainActivity 调用启动/停止语音识别
 *   - 通过 OnVoiceCommandListener 回调识别到的指令
 * 接口说明:
 *   - initialize(Context): 初始化语音识别器
 *   - startListening(): 开始监听语音
 *   - stopListening(): 停止监听
 *   - registerCommand(String, VoiceCommandHandler): 注册自定义指令
 * ============================================================================
 */
package com.smarteye.blindguide.voice;

import android.content.Context;
import android.content.Intent;
import android.os.Bundle;
import android.speech.RecognitionListener;
import android.speech.RecognizerIntent;
import android.speech.SpeechRecognizer;
import android.util.Log;

import com.smarteye.blindguide.data.AppConfig;
import com.smarteye.blindguide.tts.TTSManager;

import java.util.HashMap;
import java.util.List;
import java.util.Locale;
import java.util.Map;

/**
 * 语音控制管理器
 * 识别用户语音指令并执行对应操作
 */
public class VoiceControl {

    private static final String TAG = "VoiceControl";

    /** 语音识别器 */
    private SpeechRecognizer speechRecognizer;

    /** 是否正在监听 */
    private boolean isListening = false;

    /** 是否已初始化 */
    private boolean isInitialized = false;

    /** 语音指令回调监听器 */
    private OnVoiceCommandListener commandListener;

    /** 自定义指令处理器映射表 */
    private final Map<String, VoiceCommandHandler> commandHandlers = new HashMap<>();

    /** 模式变更回调（供外部同步 ObstacleAnalyzer 等组件） */
    private ModeChangeListener modeChangeListener;

    /**
     * 模式变更监听器接口
     */
    public interface ModeChangeListener {
        void onModeChanged(int newMode);
    }

    /**
     * 设置模式变更监听器
     */
    public void setModeChangeListener(ModeChangeListener listener) {
        this.modeChangeListener = listener;
    }

    /**
     * 语音指令回调接口
     */
    public interface OnVoiceCommandListener {
        /**
         * 识别到指令时回调
         * @param command 指令文本
         * @param matchedCommand 匹配到的预定义指令
         */
        void onCommand(String command, String matchedCommand);

        /**
         * 识别结果回调（原始文本）
         * @param text 识别到的文本
         */
        void onResult(String text);

        /**
         * 识别错误回调
         * @param error 错误信息
         */
        void onError(String error);
    }

    /**
     * 自定义指令处理器接口
     */
    public interface VoiceCommandHandler {
        /**
         * 执行指令
         * @param args 指令参数
         */
        void execute(String args);
    }

    /**
     * 预定义语音指令
     */
    public static final String CMD_SWITCH_SENSOR_MODE = "传感器模式";
    public static final String CMD_SWITCH_AUTO_MODE = "自动模式";
    public static final String CMD_SWITCH_RISK_MODE = "风险模式";
    public static final String CMD_QUERY_STATUS = "查询状态";
    public static final String CMD_STOP_SPEAK = "停止播报";
    public static final String CMD_ENABLE_VISION = "开启视觉";
    public static final String CMD_DISABLE_VISION = "关闭视觉";
    public static final String CMD_ENABLE_GPS = "开启定位";
    public static final String CMD_DISABLE_GPS = "关闭定位";

    /**
     * 初始化语音识别器
     * @param context 上下文
     * @return true 初始化成功
     */
    public boolean initialize(Context context) {
        // 检查设备是否支持语音识别
        if (!SpeechRecognizer.isRecognitionAvailable(context)) {
            Log.e(TAG, "设备不支持语音识别");
            return false;
        }

        speechRecognizer = SpeechRecognizer.createSpeechRecognizer(context);
        speechRecognizer.setRecognitionListener(new VoiceRecognitionListener());

        // 注册默认指令处理器
        registerDefaultCommands();

        isInitialized = true;
        Log.i(TAG, "语音控制初始化成功");
        return true;
    }

    /**
     * 注册默认指令处理器
     */
    private void registerDefaultCommands() {
        // 切换到传感器模式
        registerCommand(CMD_SWITCH_SENSOR_MODE, args -> {
            AppConfig.getInstance().setCurrentMode(AppConfig.MODE_SENSOR_ONLY);
            notifyModeChanged(AppConfig.MODE_SENSOR_ONLY);
            TTSManager.getInstance().speak("已切换到传感器模式", TTSManager.PRIORITY_MEDIUM);
        });

        // 切换到自动模式
        registerCommand(CMD_SWITCH_AUTO_MODE, args -> {
            AppConfig.getInstance().setCurrentMode(AppConfig.MODE_AUTO);
            notifyModeChanged(AppConfig.MODE_AUTO);
            TTSManager.getInstance().speak("已切换到自动模式", TTSManager.PRIORITY_MEDIUM);
        });

        // 切换到风险播报模式
        registerCommand(CMD_SWITCH_RISK_MODE, args -> {
            AppConfig.getInstance().setCurrentMode(AppConfig.MODE_RISK_ONLY);
            notifyModeChanged(AppConfig.MODE_RISK_ONLY);
            TTSManager.getInstance().speak("已切换到风险播报模式", TTSManager.PRIORITY_MEDIUM);
        });

        // 查询状态
        registerCommand(CMD_QUERY_STATUS, args -> {
            int mode = AppConfig.getInstance().getCurrentMode();
            String modeName = AppConfig.getModeName(mode);
            TTSManager.getInstance().speak("当前" + modeName, TTSManager.PRIORITY_MEDIUM);
        });

        // 停止播报
        registerCommand(CMD_STOP_SPEAK, args -> {
            TTSManager.getInstance().stop();
        });

        // 开启/关闭视觉识别
        registerCommand(CMD_ENABLE_VISION, args -> {
            AppConfig.getInstance().setVisionEnabled(true);
            TTSManager.getInstance().speak("视觉识别已开启", TTSManager.PRIORITY_MEDIUM);
        });
        registerCommand(CMD_DISABLE_VISION, args -> {
            AppConfig.getInstance().setVisionEnabled(false);
            TTSManager.getInstance().speak("视觉识别已关闭", TTSManager.PRIORITY_MEDIUM);
        });

        // 开启/关闭 GPS 定位
        registerCommand(CMD_ENABLE_GPS, args -> {
            AppConfig.getInstance().setGpsEnabled(true);
            TTSManager.getInstance().speak("定位已开启", TTSManager.PRIORITY_MEDIUM);
        });
        registerCommand(CMD_DISABLE_GPS, args -> {
            AppConfig.getInstance().setGpsEnabled(false);
            TTSManager.getInstance().speak("定位已关闭", TTSManager.PRIORITY_MEDIUM);
        });
    }

    /**
     * 注册自定义指令处理器
     * @param command 指令关键词
     * @param handler 处理器
     */
    public void registerCommand(String command, VoiceCommandHandler handler) {
        commandHandlers.put(command, handler);
    }

    /**
     * 设置语音指令回调监听器
     * @param listener 监听器
     */
    public void setOnVoiceCommandListener(OnVoiceCommandListener listener) {
        this.commandListener = listener;
    }

    /**
     * 开始监听语音
     */
    public void startListening() {
        if (!isInitialized || speechRecognizer == null) {
            return;
        }

        // 构建识别意图
        Intent intent = new Intent(RecognizerIntent.ACTION_RECOGNIZE_SPEECH);
        intent.putExtra(RecognizerIntent.EXTRA_LANGUAGE_MODEL,
                RecognizerIntent.LANGUAGE_MODEL_FREE_FORM);
        intent.putExtra(RecognizerIntent.EXTRA_LANGUAGE, Locale.CHINESE.toString());
        intent.putExtra(RecognizerIntent.EXTRA_MAX_RESULTS, 1);
        intent.putExtra(RecognizerIntent.EXTRA_PARTIAL_RESULTS, true);

        try {
            speechRecognizer.startListening(intent);
            isListening = true;
            Log.i(TAG, "开始语音识别");
        } catch (Exception e) {
            Log.e(TAG, "启动语音识别失败: " + e.getMessage());
        }
    }

    /**
     * 停止监听语音
     */
    public void stopListening() {
        if (speechRecognizer != null && isListening) {
            speechRecognizer.stopListening();
            isListening = false;
            Log.i(TAG, "停止语音识别");
        }
    }

    /**
     * 语音识别监听器实现
     */
    private class VoiceRecognitionListener implements RecognitionListener {

        @Override
        public void onReadyForSpeech(Bundle params) {
            // 准备就绪
        }

        @Override
        public void onBeginningOfSpeech() {
            // 开始说话
        }

        @Override
        public void onRmsChanged(float rmsdB) {
            // 音量变化
        }

        @Override
        public void onBufferReceived(byte[] buffer) {
            // 接收音频缓冲
        }

        @Override
        public void onEvent(int eventType, Bundle params) {
            // 预留事件处理
        }

        @Override
        public void onEndOfSpeech() {
            // 说话结束
            isListening = false;
        }

        @Override
        public void onError(int error) {
            isListening = false;
            String errorMsg = getErrorMessage(error);
            if (commandListener != null) {
                commandListener.onError(errorMsg);
            }
        }

        @Override
        public void onPartialResults(Bundle partialResults) {
            // 部分识别结果
            List<String> partial = partialResults.getStringArrayList(
                    SpeechRecognizer.RESULTS_RECOGNITION);
            if (partial != null && !partial.isEmpty()) {
                // 可用于实时显示识别文本
            }
        }

        @Override
        public void onResults(Bundle results) {
            List<String> matches = results.getStringArrayList(
                    SpeechRecognizer.RESULTS_RECOGNITION);
            if (matches != null && !matches.isEmpty()) {
                String text = matches.get(0);
                Log.i(TAG, "识别结果: " + text);

                // 回调原始结果
                if (commandListener != null) {
                    commandListener.onResult(text);
                }

                // 匹配并执行指令
                matchAndExecuteCommand(text);
            }
            isListening = false;
        }
    }

    /**
     * 匹配并执行指令
     * @param text 识别到的文本
     */
    private void matchAndExecuteCommand(String text) {
        for (Map.Entry<String, VoiceCommandHandler> entry : commandHandlers.entrySet()) {
            String cmd = entry.getKey();
            // 模糊匹配：识别文本包含指令关键词
            if (text.contains(cmd)) {
                Log.i(TAG, "匹配指令: " + cmd);
                if (commandListener != null) {
                    commandListener.onCommand(text, cmd);
                }
                entry.getValue().execute(text);
                return;
            }
        }
        // 未匹配到指令
        Log.w(TAG, "未匹配到指令: " + text);
    }

    /**
     * 获取错误信息
     * @param error 错误码
     * @return 错误描述
     */
    private String getErrorMessage(int error) {
        switch (error) {
            case SpeechRecognizer.ERROR_AUDIO:
                return "音频录制错误";
            case SpeechRecognizer.ERROR_CLIENT:
                return "客户端错误";
            case SpeechRecognizer.ERROR_INSUFFICIENT_PERMISSIONS:
                return "权限不足";
            case SpeechRecognizer.ERROR_NETWORK:
                return "网络错误";
            case SpeechRecognizer.ERROR_NETWORK_TIMEOUT:
                return "网络超时";
            case SpeechRecognizer.ERROR_NO_MATCH:
                return "未识别到语音";
            case SpeechRecognizer.ERROR_RECOGNIZER_BUSY:
                return "识别器忙碌";
            case SpeechRecognizer.ERROR_SERVER:
                return "服务器错误";
            case SpeechRecognizer.ERROR_SPEECH_TIMEOUT:
                return "语音输入超时";
            default:
                return "未知错误";
        }
    }

    /**
     * 是否正在监听
     * @return true 正在监听
     */
    public boolean isListening() {
        return isListening;
    }

    /**
     * 是否已初始化
     * @return true 已初始化
     */
    public boolean isInitialized() {
        return isInitialized;
    }

    /**
     * 通知外部监听器模式已变更
     */
    private void notifyModeChanged(int newMode) {
        if (modeChangeListener != null) {
            modeChangeListener.onModeChanged(newMode);
        }
    }

    /**
     * 释放资源
     */
    public void release() {
        if (speechRecognizer != null) {
            speechRecognizer.destroy();
            speechRecognizer = null;
        }
        isInitialized = false;
        isListening = false;
        Log.i(TAG, "语音控制资源已释放");
    }
}
