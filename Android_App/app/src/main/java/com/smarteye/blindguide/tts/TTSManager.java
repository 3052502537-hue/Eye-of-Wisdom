/**
 * ============================================================================
 * 文件名: TTSManager.java
 * 功能描述:
 *   - TTS 语音播报管理器，封装 Android TextToSpeech 引擎
 *   - 支持中文语音播报，可调节语速、音调
 *   - 内置播报节流机制，避免频繁播报干扰用户
 *   - 支持播报优先级，危险告警优先播报
 * 依赖关系:
 *   - 依赖 Android TextToSpeech API
 *   - 依赖 AppConfig 获取默认语速配置
 *   - 被 ObstacleAnalyzer 调用播报避障建议
 *   - 被 MainFragment 调用播报状态信息
 *   - 被 SettingsFragment 调用测试语音
 * 接口说明:
 *   - initialize(Context): 初始化 TTS 引擎
 *   - speak(String, int): 播报文本，支持优先级
 *   - stop(): 停止当前播报
 *   - release(): 释放资源
 * ============================================================================
 */
package com.smarteye.blindguide.tts;

import android.content.Context;
import android.speech.tts.TextToSpeech;
import android.speech.tts.UtteranceProgressListener;
import android.util.Log;

import com.smarteye.blindguide.data.AppConfig;

import java.util.HashMap;
import java.util.Locale;
import java.util.concurrent.atomic.AtomicBoolean;

/**
 * TTS 语音播报管理器
 * 单例模式，全局管理语音播报
 */
public class TTSManager {

    private static final String TAG = "TTSManager";

    /** 播报优先级：低（普通信息） */
    public static final int PRIORITY_LOW = 0;

    /** 播报优先级：中（提示信息） */
    public static final int PRIORITY_MEDIUM = 1;

    /** 播报优先级：高（危险告警，立即打断） */
    public static final int PRIORITY_HIGH = 2;

    /** TTS 引擎实例 */
    private TextToSpeech tts;

    /** 是否已初始化 */
    private final AtomicBoolean isInitialized = new AtomicBoolean(false);

    /** 是否正在播报 */
    private final AtomicBoolean isSpeaking = new AtomicBoolean(false);

    /** 上次播报时间（毫秒） */
    private long lastSpeakTime = 0;

    /** 上次播报内容（用于去重） */
    private String lastSpeakText = "";

    /** 当前语速 */
    private float speechRate = AppConfig.TTS_DEFAULT_RATE;

    /** 单例实例 */
    private static TTSManager instance;

    /**
     * 私有构造方法
     */
    private TTSManager() {}

    /**
     * 获取单例实例
     * @return TTSManager 单例
     */
    public static synchronized TTSManager getInstance() {
        if (instance == null) {
            instance = new TTSManager();
        }
        return instance;
    }

    /**
     * 初始化 TTS 引擎
     * @param context 上下文
     * @return true 初始化成功
     */
    public boolean initialize(Context context) {
        if (isInitialized.get()) {
            return true;
        }

        tts = new TextToSpeech(context, status -> {
            if (status == TextToSpeech.SUCCESS) {
                // 设置中文语言
                int result = tts.setLanguage(Locale.CHINESE);
                if (result == TextToSpeech.LANG_MISSING_DATA
                        || result == TextToSpeech.LANG_NOT_SUPPORTED) {
                    Log.w(TAG, "中文不支持，回退到默认语言");
                    tts.setLanguage(Locale.getDefault());
                }
                // 设置语速
                tts.setSpeechRate(speechRate);
                isInitialized.set(true);
                Log.i(TAG, "TTS 初始化成功");
            } else {
                Log.e(TAG, "TTS 初始化失败，错误码: " + status);
            }
        });

        // 设置播报状态监听
        tts.setOnUtteranceProgressListener(new UtteranceProgressListener() {
            @Override
            public void onStart(String utteranceId) {
                isSpeaking.set(true);
            }

            @Override
            public void onDone(String utteranceId) {
                isSpeaking.set(false);
            }

            @Override
            public void onError(String utteranceId) {
                isSpeaking.set(false);
            }
        });

        return true;
    }

    /**
     * 播报文本
     * @param text 待播报文本
     * @param priority 播报优先级（PRIORITY_LOW / PRIORITY_MEDIUM / PRIORITY_HIGH）
     */
    public void speak(String text, int priority) {
        if (!isInitialized.get() || tts == null || text == null || text.isEmpty()) {
            return;
        }

        long now = System.currentTimeMillis();

        // 优先级判断
        if (priority == PRIORITY_HIGH) {
            // 高优先级：立即打断当前播报
            tts.stop();
            doSpeak(text, priority);
        } else if (priority == PRIORITY_MEDIUM) {
            // 中优先级：节流控制，避免频繁播报
            if (now - lastSpeakTime < AppConfig.TTS_MIN_INTERVAL_MS) {
                return;
            }
            // 内容去重
            if (text.equals(lastSpeakText) && now - lastSpeakTime < AppConfig.TTS_MIN_INTERVAL_MS * 3) {
                return;
            }
            doSpeak(text, priority);
        } else {
            // 低优先级：等待当前播报完成
            if (isSpeaking.get()) {
                return;
            }
            doSpeak(text, priority);
        }
    }

    /**
     * 执行播报
     * @param text 文本
     * @param priority 优先级
     */
    private void doSpeak(String text, int priority) {
        HashMap<String, String> params = new HashMap<>();
        params.put(TextToSpeech.Engine.KEY_PARAM_UTTERANCE_ID,
                "tts_" + System.currentTimeMillis());

        // 高优先级使用 QUEUE_FLUSH 打断队列，其他使用 QUEUE_ADD 排队
        int queueMode = (priority == PRIORITY_HIGH)
                ? TextToSpeech.QUEUE_FLUSH
                : TextToSpeech.QUEUE_ADD;

        tts.speak(text, queueMode, params);
        lastSpeakTime = System.currentTimeMillis();
        lastSpeakText = text;
    }

    /**
     * 播报文本（默认中优先级）
     * @param text 待播报文本
     */
    public void speak(String text) {
        speak(text, PRIORITY_MEDIUM);
    }

    /**
     * 停止当前播报
     */
    public void stop() {
        if (tts != null) {
            tts.stop();
            isSpeaking.set(false);
        }
    }

    /**
     * 设置语速
     * @param rate 语速（0.5~2.0，1.0 为正常）
     */
    public void setSpeechRate(float rate) {
        this.speechRate = Math.max(0.5f, Math.min(2.0f, rate));
        if (tts != null) {
            tts.setSpeechRate(this.speechRate);
        }
    }

    /**
     * 获取当前语速
     * @return 语速值
     */
    public float getSpeechRate() {
        return speechRate;
    }

    /**
     * 是否正在播报
     * @return true 正在播报
     */
    public boolean isSpeaking() {
        return isSpeaking.get();
    }

    /**
     * 是否已初始化
     * @return true 已初始化
     */
    public boolean isInitialized() {
        return isInitialized.get();
    }

    /**
     * 释放 TTS 资源
     */
    public void release() {
        if (tts != null) {
            tts.stop();
            tts.shutdown();
            tts = null;
        }
        isInitialized.set(false);
        isSpeaking.set(false);
        Log.i(TAG, "TTS 资源已释放");
    }
}
