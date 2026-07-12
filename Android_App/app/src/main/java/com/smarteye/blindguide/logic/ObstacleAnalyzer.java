/**
 * ============================================================================
 * 文件名: ObstacleAnalyzer.java
 * 功能描述:
 *   - 避障决策引擎，融合多传感器数据和视觉识别结果
 *   - 根据激光测距、雷达数据、TFLite视觉识别输出综合判断风险等级
 *   - 生成自然语言避障建议文本，供 TTS 播报
 *   - 支持不同工作模式（传感器模式、自动模式、风险播报模式）
 *   - 预留算法接口，方便后续优化（融合策略可替换）
 * 依赖关系:
 *   - 依赖 AppConfig 获取距离阈值和模式配置
 *   - 依赖 Protocol.SensorData 传感器数据结构
 *   - 依赖 TFLiteClassifier.ClassifyResult 视觉识别结果
 *   - 依赖 TTSManager 进行语音播报
 *   - 被 MainFragment 调用处理数据并触发播报
 * 接口说明:
 *   - analyze(SensorData, ClassifyResult): 融合数据并输出避障建议
 *   - setMode(int): 设置当前工作模式
 *   - setOnAnalysisResultListener(OnAnalysisResultListener): 设置结果回调
 *   - registerStrategy(ObstacleStrategy): 注册自定义融合策略（扩展接口）
 * ============================================================================
 */
package com.smarteye.blindguide.logic;

import com.smarteye.blindguide.ai.TFLiteClassifier;
import com.smarteye.blindguide.data.AppConfig;
import com.smarteye.blindguide.network.Protocol;
import com.smarteye.blindguide.tts.TTSManager;

/**
 * 避障决策引擎
 * 融合传感器数据和视觉识别结果，输出避障建议
 */
public class ObstacleAnalyzer {

    /** 最小数据融合间隔（毫秒），避免过于频繁的分析 */
    private static final long MIN_ANALYSIS_INTERVAL_MS = 500;

    /** 最近一次传感器数据 */
    private Protocol.SensorData lastSensorData;

    /** 最近一次视觉识别结果 */
    private TFLiteClassifier.ClassifyResult lastVisionResult;

    /** 当前工作模式 */
    private int currentMode = AppConfig.MODE_AUTO;

    /** 上次分析时间 */
    private long lastAnalysisTime = 0;

    /** 上次播报的风险等级（避免重复播报） */
    private int lastRiskLevel = AppConfig.RISK_SAFE;

    /** 当前融合策略 */
    private ObstacleStrategy strategy;

    /** 分析结果回调监听器 */
    private OnAnalysisResultListener resultListener;

    /**
     * 避障分析结果
     */
    public static class AnalysisResult {
        /** 风险等级（RISK_SAFE / RISK_CAUTION / RISK_DANGER） */
        public int riskLevel;

        /** 避障建议文本（用于 TTS 播报） */
        public String adviceText;

        /** 检测到的障碍物描述 */
        public String obstacleDescription;

        /** 建议的行进方向 */
        public String suggestedDirection;

        /** 综合置信度（0.0~1.0） */
        public float confidence;

        /** 分析时间戳 */
        public long timestamp;

        public AnalysisResult() {
            this.timestamp = System.currentTimeMillis();
        }
    }

    /**
     * 融合策略接口
     * 预留扩展接口，可替换不同的数据融合算法
     */
    public interface ObstacleStrategy {
        /**
         * 执行数据融合分析
         * @param sensorData 传感器数据
         * @param visionResult 视觉识别结果
         * @return 分析结果
         */
        AnalysisResult analyze(Protocol.SensorData sensorData,
                               TFLiteClassifier.ClassifyResult visionResult);
    }

    /**
     * 分析结果回调接口
     */
    public interface OnAnalysisResultListener {
        /**
         * 分析完成回调
         * @param result 分析结果
         */
        void onResult(AnalysisResult result);

        /**
         * 风险等级变化回调
         * @param newRisk 新的风险等级
         * @param oldRisk 旧的风险等级
         */
        void onRiskChanged(int newRisk, int oldRisk);
    }

    /**
     * 默认融合策略实现
     * 基于距离阈值和视觉识别的简单融合
     */
    private static class DefaultStrategy implements ObstacleStrategy {

        @Override
        public AnalysisResult analyze(Protocol.SensorData sensorData,
                                       TFLiteClassifier.ClassifyResult visionResult) {
            AnalysisResult result = new AnalysisResult();
            StringBuilder advice = new StringBuilder();
            StringBuilder obstacle = new StringBuilder();
            float maxConfidence = 0f;

            // ==================== 传感器数据分析 ====================

            float minDistance = Float.MAX_VALUE;
            String nearestSource = "";

            // 激光测距数据
            if (sensorData != null) {
                if (sensorData.laser_front > 0 && sensorData.laser_front < minDistance) {
                    minDistance = sensorData.laser_front;
                    nearestSource = "前方激光";
                }

                // 前方雷达数据
                if (sensorData.radar_front != null
                        && sensorData.radar_front.dist > 0
                        && sensorData.radar_front.dist < minDistance) {
                    minDistance = sensorData.radar_front.dist;
                    nearestSource = "前方雷达";
                }

                // 后方雷达数据（用于后方来车提醒）
                if (sensorData.radar_back != null
                        && sensorData.radar_back.dist > 0
                        && sensorData.radar_back.dist < AppConfig.DISTANCE_DANGER
                        && sensorData.radar_back.speed > 0) {
                    advice.append("后方有物体靠近，");
                }
            }

            // 根据最近距离判断风险等级
            int sensorRisk = AppConfig.RISK_SAFE;
            if (minDistance < AppConfig.DISTANCE_DANGER) {
                sensorRisk = AppConfig.RISK_DANGER;
            } else if (minDistance < AppConfig.DISTANCE_CAUTION) {
                sensorRisk = AppConfig.RISK_CAUTION;
            }

            // ==================== 视觉识别结果分析 ====================

            int visionRisk = AppConfig.RISK_SAFE;
            if (visionResult != null && visionResult.confidence > 0.5f) {
                maxConfidence = Math.max(maxConfidence, visionResult.confidence);
                String className = visionResult.className;

                if ("大型障碍物".equals(className)) {
                    visionRisk = AppConfig.RISK_DANGER;
                    obstacle.append("前方有大型障碍物，");
                    advice.append("前方有障碍物，请绕行，");
                } else if ("红绿灯".equals(className)) {
                    // 红绿灯需进一步识别颜色（预留扩展）
                    obstacle.append("前方有红绿灯，");
                    advice.append("前方红绿灯，请注意，");
                    visionRisk = AppConfig.RISK_CAUTION;
                } else if ("斑马线".equals(className)) {
                    obstacle.append("前方有斑马线，");
                    advice.append("前方斑马线，请注意过马路，");
                    visionRisk = AppConfig.RISK_CAUTION;
                } else if ("路口".equals(className)) {
                    obstacle.append("前方有路口，");
                    advice.append("前方路口，请注意，");
                    visionRisk = AppConfig.RISK_CAUTION;
                } else if ("盲道".equals(className)) {
                    obstacle.append("前方有盲道，");
                    // 盲道本身不构成风险，但提示用户
                    advice.append("前方有盲道，");
                    visionRisk = AppConfig.RISK_SAFE;
                }
            }

            // ==================== 数据融合 ====================

            // 取传感器和视觉识别中较高的风险等级
            result.riskLevel = Math.max(sensorRisk, visionRisk);

            // 距离信息加入建议
            if (minDistance != Float.MAX_VALUE) {
                obstacle.append(String.format("最近障碍%.1f米", minDistance));
                if (result.riskLevel == AppConfig.RISK_DANGER) {
                    advice.insert(0, "危险！").append(String.format("前方%.1f米有障碍，请停止。", minDistance));
                } else if (result.riskLevel == AppConfig.RISK_CAUTION) {
                    advice.append(String.format("前方%.1f米有障碍。", minDistance));
                }
            }

            // 根据风险等级生成建议方向
            result.suggestedDirection = generateDirection(result.riskLevel, sensorData, visionResult);

            result.adviceText = advice.toString();
            result.obstacleDescription = obstacle.toString();
            result.confidence = maxConfidence;

            return result;
        }

        /**
         * 生成建议行进方向
         * @param riskLevel 风险等级
         * @param sensorData 传感器数据
         * @param visionResult 视觉结果
         * @return 方向建议
         */
        private String generateDirection(int riskLevel,
                                          Protocol.SensorData sensorData,
                                          TFLiteClassifier.ClassifyResult visionResult) {
            if (riskLevel == AppConfig.RISK_DANGER) {
                return "请停止前进";
            } else if (riskLevel == AppConfig.RISK_CAUTION) {
                // 根据雷达角度判断绕行方向
                if (sensorData != null && sensorData.radar_front != null) {
                    float angle = sensorData.radar_front.angle;
                    if (angle > 30) {
                        return "建议向左绕行";
                    } else if (angle < -30) {
                        return "建议向右绕行";
                    }
                }
                return "请减速慢行";
            }
            return "可安全前进";
        }
    }

    /**
     * 构造避障分析器
     * 默认使用 DefaultStrategy
     */
    public ObstacleAnalyzer() {
        this.strategy = new DefaultStrategy();
    }

    /**
     * 注册自定义融合策略
     * 扩展接口，方便后续替换更复杂的算法
     * @param strategy 融合策略实现
     */
    public void registerStrategy(ObstacleStrategy strategy) {
        if (strategy != null) {
            this.strategy = strategy;
        }
    }

    /**
     * 设置分析结果回调监听器
     * @param listener 监听器
     */
    public void setOnAnalysisResultListener(OnAnalysisResultListener listener) {
        this.resultListener = listener;
    }

    /**
     * 设置当前工作模式
     * @param mode 工作模式常量
     */
    public void setMode(int mode) {
        this.currentMode = mode;
    }

    /**
     * 更新传感器数据
     * @param sensorData 最新传感器数据
     */
    public void updateSensorData(Protocol.SensorData sensorData) {
        this.lastSensorData = sensorData;
        // 触发分析
        analyze();
    }

    /**
     * 更新视觉识别结果
     * @param visionResult 最新视觉识别结果
     */
    public void updateVisionResult(TFLiteClassifier.ClassifyResult visionResult) {
        this.lastVisionResult = visionResult;
        // 触发分析
        analyze();
    }

    /**
     * 执行分析
     * 融合最新数据并生成建议
     */
    public void analyze() {
        long now = System.currentTimeMillis();
        // 节流控制，避免过于频繁分析
        if (now - lastAnalysisTime < MIN_ANALYSIS_INTERVAL_MS) {
            return;
        }
        lastAnalysisTime = now;

        if (strategy == null) {
            return;
        }

        // 调用策略执行分析
        AnalysisResult result = strategy.analyze(lastSensorData, lastVisionResult);
        if (result == null) {
            return;
        }

        // 风险等级变化检测
        if (result.riskLevel != lastRiskLevel) {
            if (resultListener != null) {
                resultListener.onRiskChanged(result.riskLevel, lastRiskLevel);
            }
            lastRiskLevel = result.riskLevel;
        }

        // 回调分析结果
        if (resultListener != null) {
            resultListener.onResult(result);
        }

        // 根据工作模式触发 TTS 播报
        triggerTTS(result);
    }

    /**
     * 根据工作模式触发 TTS 播报
     * @param result 分析结果
     */
    private void triggerTTS(AnalysisResult result) {
        if (result.adviceText == null || result.adviceText.isEmpty()) {
            return;
        }

        switch (currentMode) {
            case AppConfig.MODE_SENSOR_ONLY:
                // 传感器模式：仅在危险时播报
                if (result.riskLevel == AppConfig.RISK_DANGER) {
                    TTSManager.getInstance().speak(result.adviceText, TTSManager.PRIORITY_HIGH);
                }
                break;

            case AppConfig.MODE_AUTO:
                // 自动模式：根据风险等级决定播报优先级
                if (result.riskLevel == AppConfig.RISK_DANGER) {
                    TTSManager.getInstance().speak(result.adviceText, TTSManager.PRIORITY_HIGH);
                } else if (result.riskLevel == AppConfig.RISK_CAUTION) {
                    TTSManager.getInstance().speak(result.adviceText, TTSManager.PRIORITY_MEDIUM);
                }
                break;

            case AppConfig.MODE_RISK_ONLY:
                // 风险播报模式：仅播报风险等级
                String riskText = AppConfig.getRiskName(result.riskLevel);
                if (result.riskLevel == AppConfig.RISK_DANGER) {
                    TTSManager.getInstance().speak(riskText, TTSManager.PRIORITY_HIGH);
                } else if (result.riskLevel == AppConfig.RISK_CAUTION) {
                    TTSManager.getInstance().speak(riskText, TTSManager.PRIORITY_MEDIUM);
                }
                break;
        }
    }

    /**
     * 获取最近的分析结果
     * @return 最近分析结果，无数据返回 null
     */
    public AnalysisResult getLastResult() {
        if (strategy == null) {
            return null;
        }
        return strategy.analyze(lastSensorData, lastVisionResult);
    }

    /**
     * 获取当前风险等级
     * @return 风险等级常量
     */
    public int getCurrentRiskLevel() {
        return lastRiskLevel;
    }

    /**
     * 重置分析器状态
     */
    public void reset() {
        lastSensorData = null;
        lastVisionResult = null;
        lastRiskLevel = AppConfig.RISK_SAFE;
        lastAnalysisTime = 0;
    }
}
