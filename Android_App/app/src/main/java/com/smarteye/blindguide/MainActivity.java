/**
 * ============================================================================
 * 文件名: MainActivity.java
 * 功能描述:
 *   - 应用主 Activity，承载底部导航和 Fragment 切换
 *   - 初始化网络通信（TCP/UDP）、TTS、TFLite、避障分析器等核心组件
 *   - 管理 Activity 生命周期，统一释放资源
 *   - 处理权限申请（定位、录音等）
 *   - 提供调试模式激活入口（连续点击标题5次）
 * 依赖关系:
 *   - 依赖 TCPClient、UDPReceiver 网络通信
 *   - 依赖 TTSManager 语音播报
 *   - 依赖 TFLiteClassifier 视觉识别
 *   - 依赖 ObstacleAnalyzer 避障决策
 *   - 依赖 MainFragment、SettingsFragment、DebugFragment 三个页面
 * 接口说明:
 *   - getTcpClient(): 获取 TCP 客户端实例
 *   - getUdpReceiver(): 获取 UDP 接收器实例
 *   - getAnalyzer(): 获取避障分析器实例
 *   - getClassifier(): 获取 TFLite 分类器实例
 *   - showDebugFragment(): 显示调试页面（开发者模式）
 * ============================================================================
 */
package com.smarteye.blindguide;

import android.Manifest;
import android.os.Bundle;
import android.util.Log;
import android.widget.Toast;

import androidx.activity.result.ActivityResultLauncher;
import androidx.activity.result.contract.ActivityResultContracts;
import androidx.appcompat.app.AppCompatActivity;
import androidx.fragment.app.Fragment;
import androidx.fragment.app.FragmentManager;
import androidx.fragment.app.FragmentTransaction;

import com.google.android.material.bottomnavigation.BottomNavigationView;
import com.smarteye.blindguide.ai.TFLiteClassifier;
import com.smarteye.blindguide.data.AppConfig;
import com.smarteye.blindguide.logic.ObstacleAnalyzer;
import com.smarteye.blindguide.network.CameraHttpClient;
import com.smarteye.blindguide.network.Protocol;
import com.smarteye.blindguide.network.TCPClient;
import com.smarteye.blindguide.tts.TTSManager;
import com.smarteye.blindguide.ui.DebugFragment;
import com.smarteye.blindguide.ui.MainFragment;
import com.smarteye.blindguide.ui.SettingsFragment;
import com.smarteye.blindguide.voice.VoiceControl;

import java.util.List;
import java.util.concurrent.CopyOnWriteArrayList;

/**
 * 应用主 Activity
 * 管理 Fragment 切换和核心组件生命周期
 */
public class MainActivity extends AppCompatActivity {

    private static final String TAG = "MainActivity";

    /** Fragment 标签 */
    private static final String TAG_MAIN = "main";
    private static final String TAG_SETTINGS = "settings";
    private static final String TAG_DEBUG = "debug";

    /** 底部导航视图 */
    private BottomNavigationView bottomNav;

    /** 核心组件 */
    private TCPClient tcpClient;
    private CameraHttpClient cameraClient;  // v3.0: HTTP MJPEG替代UDP
    private TFLiteClassifier classifier;
    private ObstacleAnalyzer analyzer;
    private VoiceControl voiceControl;

    /** 当前显示的 Fragment */
    private Fragment currentFragment;

    /** 权限请求启动器 */
    private ActivityResultLauncher<String[]> permissionLauncher;

    /** 调试数据监听器列表（线程安全） */
    private final List<DebugDataListener> debugListeners = new CopyOnWriteArrayList<>();

    /**
     * 调试数据监听器接口
     * DebugFragment 通过此接口获取数据，避免覆盖主回调
     */
    public interface DebugDataListener {
        /** 收到传感器数据 */
        void onSensorData(Protocol.SensorData data);

        /** 收到图像帧 */
        void onImageFrame(Protocol.ImageFrame frame);

        /** TFLite 识别完成 */
        void onClassifyResult(TFLiteClassifier.ClassifyResult result);

        /** 避障分析完成 */
        void onAnalysisResult(ObstacleAnalyzer.AnalysisResult result);
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_main);

        // 初始化视图
        initViews();

        // 初始化权限请求
        initPermissionLauncher();

        // 初始化核心组件
        initCoreComponents();

        // 请求必要权限
        requestNecessaryPermissions();

        // 默认显示主界面
        if (savedInstanceState == null) {
            switchFragment(TAG_MAIN);
        }
    }

    /**
     * 初始化视图
     */
    private void initViews() {
        bottomNav = findViewById(R.id.bottom_navigation);

        // v1.1: 调试阶段开发者页直接可见
        updateDebugNavVisibility();

        // 底部导航项点击监听
        bottomNav.setOnItemSelectedListener(item -> {
            int itemId = item.getItemId();
            if (itemId == R.id.nav_main) {
                switchFragment(TAG_MAIN);
                return true;
            } else if (itemId == R.id.nav_settings) {
                switchFragment(TAG_SETTINGS);
                return true;
            } else if (itemId == R.id.nav_debug) {
                switchFragment(TAG_DEBUG);
                return true;
            }
            return false;
        });

        // 调试页已默认可见，无需隐藏
    }

    /**
     * 初始化权限请求启动器
     */
    private void initPermissionLauncher() {
        permissionLauncher = registerForActivityResult(
                new ActivityResultContracts.RequestMultiplePermissions(),
                result -> {
                    Boolean locationGranted = result.get(Manifest.permission.ACCESS_FINE_LOCATION);
                    Boolean audioGranted = result.get(Manifest.permission.RECORD_AUDIO);

                    if (locationGranted != null && locationGranted) {
                        Log.i(TAG, "定位权限已授予");
                    }
                    if (audioGranted != null && audioGranted) {
                        Log.i(TAG, "录音权限已授予");
                    }
                });
    }

    /**
     * 初始化核心组件
     */
    private void initCoreComponents() {
        // 初始化 TTS
        TTSManager.getInstance().initialize(this);

        // 初始化 TCP 客户端 (传感器数据)
        tcpClient = new TCPClient();

        // v3.0: 初始化 HTTP 摄像头客户端 (替代UDP, 直连摄像板拉流)
        cameraClient = new CameraHttpClient();
        // 默认URL: 摄像板静态IP, 可通过传感器JSON中的camera_ip动态更新
        cameraClient.setStreamUrl("http://" + AppConfig.CAMERA_ESP32_IP + "/video");

        // 初始化 TFLite 分类器
        classifier = new TFLiteClassifier();
        classifier.initialize(this);

        // 初始化避障分析器
        analyzer = new ObstacleAnalyzer();
        analyzer.setMode(AppConfig.getInstance().getCurrentMode());

        // 初始化语音控制（可选）
        voiceControl = new VoiceControl();
        voiceControl.initialize(this);
        // 语音切换模式时同步更新 ObstacleAnalyzer
        voiceControl.setModeChangeListener(mode -> {
            if (analyzer != null) {
                analyzer.setMode(mode);
            }
        });

        // 设置 TCP 数据回调（使用 add 而非 set，避免覆盖其他监听器）
        tcpClient.addOnSensorDataListener(new TCPClient.OnSensorDataListener() {
            @Override
            public void onSensorData(Protocol.SensorData data) {
                // 更新避障分析器
                analyzer.updateSensorData(data);

                // 动态更新摄像板IP (从传感器JSON获取)
                if (data.camera_ip != null && !data.camera_ip.isEmpty()) {
                    String currentUrl = cameraClient != null ?
                            "http://" + data.camera_ip + "/video" : "";
                    // IP变更时更新URL
                    if (cameraClient != null && !currentUrl.isEmpty()) {
                        cameraClient.setStreamUrl(currentUrl);
                    }
                }

                // 转发给调试监听器
                for (DebugDataListener dl : debugListeners) {
                    dl.onSensorData(data);
                }
            }

            @Override
            public void onRawData(String rawJson) {
                Log.d(TAG, "TCP原始数据: " + rawJson);
            }
        });

        // 添加避障分析结果回调 → 转发给调试监听器（使用 add 避免覆盖 MainFragment 的回调）
        analyzer.addOnAnalysisResultListener(new ObstacleAnalyzer.OnAnalysisResultListener() {
            @Override
            public void onResult(ObstacleAnalyzer.AnalysisResult result) {
                for (DebugDataListener dl : debugListeners) {
                    dl.onAnalysisResult(result);
                }
            }

            @Override
            public void onRiskChanged(int newRisk, int oldRisk) {
                // 风险变化由 MainFragment 处理
            }
        });

        // v3.0: 设置 HTTP 摄像头帧回调 (替代UDP, 直连摄像板)
        cameraClient.setOnFrameListener(new CameraHttpClient.OnFrameListener() {
            @Override
            public void onFrame(byte[] jpegData, int frameNumber, long fetchTimeMs) {
                // 构造 ImageFrame 传递给调试监听器
                Protocol.ImageFrame frame = new Protocol.ImageFrame(
                        frameNumber, jpegData, 0, 0);

                // 转发图像帧给调试监听器
                for (DebugDataListener dl : debugListeners) {
                    dl.onImageFrame(frame);
                }

                // 调用 TFLite 进行视觉识别
                if (AppConfig.getInstance().isVisionEnabled() && classifier.isInitialized()) {
                    classifier.classify(jpegData, new TFLiteClassifier.OnClassifyResultListener() {
                        @Override
                        public void onResult(TFLiteClassifier.ClassifyResult result) {
                            analyzer.updateVisionResult(result);
                            for (DebugDataListener dl : debugListeners) {
                                dl.onClassifyResult(result);
                            }
                        }

                        @Override
                        public void onError(String error) {
                            Log.e(TAG, "TFLite识别错误: " + error);
                        }
                    });
                }
            }

            @Override
            public void onError(String error) {
                Log.e(TAG, "HTTP摄像头错误: " + error);
            }

            @Override
            public void onStateChanged(boolean connected, String message) {
                Log.d(TAG, "HTTP摄像头状态: " + (connected ? "已连接" : "断开")
                        + " - " + message);
            }
        });
    }

    /**
     * 请求必要权限
     */
    private void requestNecessaryPermissions() {
        String[] permissions = {
                Manifest.permission.ACCESS_FINE_LOCATION,
                Manifest.permission.ACCESS_COARSE_LOCATION,
                Manifest.permission.RECORD_AUDIO
        };
        permissionLauncher.launch(permissions);
    }

    /**
     * 切换 Fragment
     * @param tag Fragment 标签
     */
    private void switchFragment(String tag) {
        FragmentManager fm = getSupportFragmentManager();
        FragmentTransaction transaction = fm.beginTransaction();

        // 隐藏当前 Fragment
        if (currentFragment != null) {
            transaction.hide(currentFragment);
        }

        // 查找或创建目标 Fragment
        Fragment targetFragment = fm.findFragmentByTag(tag);
        if (targetFragment == null) {
            targetFragment = createFragment(tag);
            if (targetFragment != null) {
                transaction.add(R.id.fragment_container, targetFragment, tag);
            }
        } else {
            transaction.show(targetFragment);
        }

        currentFragment = targetFragment;
        transaction.commitAllowingStateLoss();
    }

    /**
     * 根据 tag 创建 Fragment
     * @param tag Fragment 标签
     * @return Fragment 实例
     */
    private Fragment createFragment(String tag) {
        switch (tag) {
            case TAG_MAIN:
                return new MainFragment();
            case TAG_SETTINGS:
                return new SettingsFragment();
            case TAG_DEBUG:
                return new DebugFragment();
            default:
                return null;
        }
    }

    /**
     * 更新调试导航项可见性
     */
    private void updateDebugNavVisibility() {
        if (bottomNav.getMenu().findItem(R.id.nav_debug) != null) {
            bottomNav.getMenu().findItem(R.id.nav_debug)
                    .setVisible(AppConfig.getInstance().isDebugModeActivated());
        }
    }

    /**
     * 激活调试模式（由 MainFragment 计数完成后调用）
     */
    public void activateDebugMode() {
        AppConfig.getInstance().setDebugModeActivated(true);
        updateDebugNavVisibility();
        Toast.makeText(this, "开发者模式已激活", Toast.LENGTH_SHORT).show();
        TTSManager.getInstance().speak("开发者模式已激活", TTSManager.PRIORITY_MEDIUM);
    }

    // ==================== Getter 方法（供 Fragment 访问）====================

    /**
     * 获取 TCP 客户端实例
     * @return TCPClient 实例
     */
    public TCPClient getTcpClient() {
        return tcpClient;
    }

    /**
     * 获取 HTTP 摄像头客户端实例 (v3.0: 替代UDP)
     * @return CameraHttpClient 实例
     */
    public CameraHttpClient getCameraClient() {
        return cameraClient;
    }

    /**
     * 获取避障分析器实例
     * @return ObstacleAnalyzer 实例
     */
    public ObstacleAnalyzer getAnalyzer() {
        return analyzer;
    }

    /**
     * 获取 TFLite 分类器实例
     * @return TFLiteClassifier 实例
     */
    public TFLiteClassifier getClassifier() {
        return classifier;
    }

    /**
     * 获取语音控制实例
     * @return VoiceControl 实例
     */
    public VoiceControl getVoiceControl() {
        return voiceControl;
    }

    /**
     * 添加调试数据监听器
     * DebugFragment 通过此方法注册，不会覆盖主回调
     * @param listener 调试数据监听器
     */
    public void addDebugDataListener(DebugDataListener listener) {
        if (listener != null && !debugListeners.contains(listener)) {
            debugListeners.add(listener);
        }
    }

    /**
     * 移除调试数据监听器
     * @param listener 调试数据监听器
     */
    public void removeDebugDataListener(DebugDataListener listener) {
        debugListeners.remove(listener);
    }

    /**
     * 显示调试 Fragment
     */
    public void showDebugFragment() {
        bottomNav.setSelectedItemId(R.id.nav_debug);
    }

    @Override
    protected void onResume() {
        super.onResume();
        // 启动TCP传感器连接
        if (tcpClient != null) {
            tcpClient.connect();
        }
        // v3.0: 启动HTTP摄像头拉流
        if (cameraClient != null) {
            cameraClient.start();
        }
    }

    @Override
    protected void onPause() {
        super.onPause();
        // 导盲头环需要持续接收传感器数据，onPause 不主动断开
        // 仅在 onDestroy 时释放所有资源
    }

    @Override
    protected void onDestroy() {
        super.onDestroy();
        // 释放所有资源
        if (tcpClient != null) {
            tcpClient.disconnect();
        }
        if (cameraClient != null) {
            cameraClient.stop();
        }
        if (classifier != null) {
            classifier.release();
        }
        if (voiceControl != null) {
            voiceControl.release();
        }
        TTSManager.getInstance().release();
    }
}
