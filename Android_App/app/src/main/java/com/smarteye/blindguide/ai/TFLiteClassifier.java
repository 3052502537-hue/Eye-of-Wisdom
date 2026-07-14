/**
 * ============================================================================
 * 文件名: TFLiteClassifier.java
 * 功能描述:
 *   - 基于 TensorFlow Lite 的视觉目标检测推理引擎（YOLOv8-Nano → TFLite）
 *   - 支持盲道检测、红绿灯识别、大型障碍物、路口识别、斑马线识别
 *   - 加载 assets 目录下的 .tflite 模型文件
 *   - 接收 JPEG 字节数据，解码为 Bitmap 后进行推理
 *   - 输出多目标检测结果（bbox + class + score），支持单帧多目标
 * 依赖关系:
 *   - 依赖 TensorFlow Lite 库
 *   - 依赖 AppConfig 获取模型配置参数和阈值
 *   - 被 ObstacleAnalyzer 调用进行视觉识别
 *   - 被 DebugFragment 用于显示识别结果
 * 接口说明:
 *   - initialize(Context): 加载模型，初始化推理引擎
 *   - classify(byte[]): 输入 JPEG 数据，返回多目标检测结果
 *   - release(): 释放模型资源
 *   - 通过 OnClassifyResultListener 回调检测结果（含 DetectedObject 列表）
 * ============================================================================
 */
package com.smarteye.blindguide.ai;

import android.content.Context;
import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.util.Log;

import com.smarteye.blindguide.data.AppConfig;

import org.tensorflow.lite.Interpreter;

import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Collections;
import java.util.Comparator;
import java.util.HashMap;
import java.util.List;
import java.util.Map;

/**
 * TFLite 视觉目标检测推理引擎
 * 适配 YOLOv8-Nano 等目标检测模型，输出多目标 bbox + class + score
 */
public class TFLiteClassifier {

    private static final String TAG = "TFLiteClassifier";

    /** 输入图像通道数（RGB） */
    private static final int CHANNELS = 3;

    /** 像素值归一化因子 */
    private static final float NORMALIZE_VALUE = 255.0f;

    /** TFLite 解释器 */
    private Interpreter interpreter;

    /** 模型输入图像尺寸 */
    private final int inputSize;

    /** 模型输入字节缓冲区 */
    private ByteBuffer inputImageBuffer;

    /** 最大检测数量 */
    private final int maxDetections;

    // ==================== 输出张量索引（运行时确定） ====================

    private int boxOutputIndex = 0;
    private int classOutputIndex = 1;
    private int scoreOutputIndex = 2;
    private int numOutputIndex = 3;

    /** 分类标签列表 */
    private List<String> labels;

    /** 是否已初始化 */
    private boolean isInitialized = false;

    // ==================== 内部数据结构 ====================

    /**
     * 单个检测目标
     */
    public static class DetectedObject {
        /** 类别名称 */
        public String className;

        /** 置信度（0.0~1.0） */
        public float confidence;

        /** bbox 中心归一化坐标 [0, 1] */
        public float centerX;
        public float centerY;

        /** bbox 归一化坐标 [0, 1] */
        public float left;
        public float top;
        public float right;
        public float bottom;

        public DetectedObject() {}

        @Override
        public String toString() {
            return String.format("%s(%.0f%%) [%.2f,%.2f,%.2f,%.2f]",
                    className, confidence * 100, left, top, right, bottom);
        }
    }

    /**
     * 检测结果数据结构
     */
    public static class ClassifyResult {
        /** 置信度最高的类别名称（兼容旧接口） */
        public String className;

        /** 置信度最高的置信度（0.0~1.0，兼容旧接口） */
        public float confidence;

        /** 所有类别的置信度分布（检测模式下为 null） */
        public float[] probabilities;

        /** 推理耗时（毫秒） */
        public long inferenceTime;

        /** 输入图像 Bitmap（调试用） */
        public Bitmap inputBitmap;

        /** 所有检测到的目标列表（按置信度降序排列） */
        public List<DetectedObject> detections;

        public ClassifyResult() {
            this.detections = new ArrayList<>();
        }

        /**
         * 是否有有效检测结果
         */
        public boolean hasDetections() {
            return detections != null && !detections.isEmpty();
        }

        /**
         * 获取置信度最高的检测目标
         */
        public DetectedObject getTopDetection() {
            if (hasDetections()) {
                return detections.get(0);
            }
            return null;
        }
    }

    /**
     * 识别结果回调接口
     */
    public interface OnClassifyResultListener {
        /**
         * 识别完成回调
         * @param result 检测结果（含 DetectedObject 列表）
         */
        void onResult(ClassifyResult result);

        /**
         * 识别错误回调
         * @param error 错误信息
         */
        void onError(String error);
    }

    // ==================== 构造方法 ====================

    /**
     * 构造分类器
     * 使用 AppConfig 中的默认输入尺寸
     */
    public TFLiteClassifier() {
        this(AppConfig.TFLITE_INPUT_SIZE, AppConfig.MAX_DETECTIONS);
    }

    /**
     * 构造分类器
     * @param inputSize 模型输入图像尺寸（像素）
     * @param maxDetections 最大检测目标数
     */
    public TFLiteClassifier(int inputSize, int maxDetections) {
        this.inputSize = inputSize;
        this.maxDetections = maxDetections;
    }

    // ==================== 初始化 ====================

    /**
     * 初始化分类器
     * 加载模型文件、标签，配置推理引擎
     * @param context 上下文，用于访问 assets
     * @return true 初始化成功，false 失败
     */
    public boolean initialize(Context context) {
        try {
            // 加载模型文件
            ByteBuffer modelBuffer = loadModelFile(context, AppConfig.TFLITE_MODEL_FILE);
            if (modelBuffer == null) {
                Log.e(TAG, "模型文件加载失败: " + AppConfig.TFLITE_MODEL_FILE);
                return false;
            }

            // 配置解释器选项（CPU 多线程推理）
            Interpreter.Options options = new Interpreter.Options();
            options.setNumThreads(4);

            // 创建解释器
            interpreter = new Interpreter(modelBuffer, options);

            // 识别输出张量索引（通过名称匹配，兜底按索引顺序）
            detectOutputTensorIndices();

            // 初始化输入缓冲区
            int bufferSize = 4 * inputSize * inputSize * CHANNELS;
            inputImageBuffer = ByteBuffer.allocateDirect(bufferSize);
            inputImageBuffer.order(ByteOrder.nativeOrder());

            // 加载分类标签
            labels = loadDefaultLabels();

            isInitialized = true;
            Log.i(TAG, "TFLite 目标检测模型初始化成功"
                    + ", 输入: " + inputSize + "×" + inputSize
                    + ", 最大检测数: " + maxDetections
                    + ", 输出张量: boxes@" + boxOutputIndex
                    + " classes@" + classOutputIndex
                    + " scores@" + scoreOutputIndex
                    + " num@" + numOutputIndex);
            return true;

        } catch (Exception e) {
            Log.e(TAG, "初始化失败: " + e.getMessage(), e);
            return false;
        }
    }

    /**
     * 识别输出张量索引
     * TFLite 目标检测模型通常输出 4 个张量：boxes, classes, scores, num_detections
     * 不同导出工具生成的名称可能不同，按名称匹配 + 兜底按索引
     */
    private void detectOutputTensorIndices() {
        int outputCount = interpreter.getOutputTensorCount();
        Log.i(TAG, "模型输出张量数量: " + outputCount);

        boolean matched = false;
        for (int i = 0; i < outputCount; i++) {
            String name = interpreter.getOutputTensor(i).name();
            Log.d(TAG, "  输出[" + i + "]: " + name);

            String lower = name.toLowerCase();
            if (lower.contains("box") || lower.contains("location") || lower.contains("bbox")) {
                boxOutputIndex = i;
                matched = true;
            } else if (lower.contains("class") || lower.contains("label")) {
                classOutputIndex = i;
                matched = true;
            } else if (lower.contains("score") || lower.contains("confidence")) {
                scoreOutputIndex = i;
                matched = true;
            } else if (lower.contains("num") || lower.contains("count")) {
                numOutputIndex = i;
                matched = true;
            }
        }

        // 兜底：按 TFLite Detection PostProcess 标准顺序
        if (!matched && outputCount >= 4) {
            Log.w(TAG, "未能按名称匹配输出张量，使用默认索引顺序: 0=boxes, 1=classes, 2=scores, 3=num");
            boxOutputIndex = 0;
            classOutputIndex = 1;
            scoreOutputIndex = 2;
            numOutputIndex = 3;
        }
    }

    /**
     * 加载模型文件到 ByteBuffer
     */
    private ByteBuffer loadModelFile(Context context, String modelFileName) {
        try (InputStream is = context.getAssets().open(modelFileName)) {
            ByteArrayOutputStream bos = new ByteArrayOutputStream();
            byte[] buffer = new byte[1024];
            int len;
            while ((len = is.read(buffer)) != -1) {
                bos.write(buffer, 0, len);
            }
            byte[] modelBytes = bos.toByteArray();

            ByteBuffer bb = ByteBuffer.allocateDirect(modelBytes.length);
            bb.order(ByteOrder.nativeOrder());
            bb.put(modelBytes);
            bb.rewind();
            return bb;
        } catch (IOException e) {
            Log.e(TAG, "模型文件不存在: " + modelFileName);
            return null;
        }
    }

    /**
     * 加载默认分类标签
     * 预定义的 5 个识别类别，顺序与模型训练时一致
     */
    private List<String> loadDefaultLabels() {
        return new ArrayList<>(Arrays.asList(
                "盲道",         // 索引 0
                "红绿灯",       // 索引 1
                "大型障碍物",   // 索引 2
                "路口",         // 索引 3
                "斑马线"        // 索引 4
        ));
    }

    // ==================== 推理 ====================

    /**
     * 执行目标检测推理
     * @param jpegData JPEG 图像字节数据
     * @param listener 检测结果回调监听器
     */
    public void classify(byte[] jpegData, OnClassifyResultListener listener) {
        if (!isInitialized || interpreter == null) {
            if (listener != null) {
                listener.onError("分类器未初始化");
            }
            return;
        }

        long startTime = System.currentTimeMillis();

        try {
            // 解码 JPEG 为 Bitmap
            Bitmap bitmap = BitmapFactory.decodeByteArray(jpegData, 0, jpegData.length);
            if (bitmap == null) {
                if (listener != null) {
                    listener.onError("JPEG 解码失败");
                }
                return;
            }

            // 缩放到模型输入尺寸
            Bitmap scaledBitmap = Bitmap.createScaledBitmap(bitmap, inputSize, inputSize, true);
            if (bitmap != scaledBitmap) {
                bitmap.recycle();
            }

            // 将 Bitmap 转换为 ByteBuffer
            convertBitmapToBuffer(scaledBitmap);

            // 准备输出缓冲区（Map<Integer, Object> 格式）
            int actualMaxDets = getOutputSize(classOutputIndex, 0); // 先取类别输出的维度
            if (actualMaxDets <= 0) actualMaxDets = maxDetections;

            Map<Integer, Object> outputMap = new HashMap<>();
            outputMap.put(boxOutputIndex, new float[1][actualMaxDets][4]);
            outputMap.put(classOutputIndex, new float[1][actualMaxDets]);
            outputMap.put(scoreOutputIndex, new float[1][actualMaxDets]);
            outputMap.put(numOutputIndex, new float[1]);

            // 执行推理
            interpreter.runForMultipleInputsOutputs(
                    new Object[]{inputImageBuffer}, outputMap);

            long inferenceTime = System.currentTimeMillis() - startTime;

            // 解析输出张量
            float[][][] boxes = (float[][][]) outputMap.get(boxOutputIndex);
            float[][] classes = (float[][]) outputMap.get(classOutputIndex);
            float[][] scores = (float[][]) outputMap.get(scoreOutputIndex);
            float[] numDetections = (float[]) outputMap.get(numOutputIndex);

            int numDet = Math.min((int) numDetections[0], actualMaxDets);

            // 构建检测结果列表
            List<DetectedObject> detections = new ArrayList<>();
            float topScoreThreshold = AppConfig.DETECTION_SCORE_THRESHOLD;

            for (int i = 0; i < numDet; i++) {
                float score = scores[0][i];
                if (score < topScoreThreshold) {
                    continue;
                }

                int classIdx = (int) classes[0][i];
                DetectedObject obj = new DetectedObject();
                obj.confidence = score;
                obj.className = (classIdx >= 0 && classIdx < labels.size())
                        ? labels.get(classIdx) : "未知(" + classIdx + ")";

                // 解析 bbox [ymin, xmin, ymax, xmax] 归一化坐标
                float ymin = boxes[0][i][0];
                float xmin = boxes[0][i][1];
                float ymax = boxes[0][i][2];
                float xmax = boxes[0][i][3];

                obj.top = Math.max(0, ymin);
                obj.left = Math.max(0, xmin);
                obj.bottom = Math.min(1, ymax);
                obj.right = Math.min(1, xmax);
                obj.centerX = (obj.left + obj.right) / 2f;
                obj.centerY = (obj.top + obj.bottom) / 2f;

                detections.add(obj);
            }

            // 按置信度降序排列
            Collections.sort(detections, (a, b) -> Float.compare(b.confidence, a.confidence));

            // 构建结果对象
            ClassifyResult result = new ClassifyResult();
            result.detections = detections;
            result.inferenceTime = inferenceTime;
            result.inputBitmap = scaledBitmap;

            // 兼容旧接口：取最高置信度目标
            if (!detections.isEmpty()) {
                DetectedObject top = detections.get(0);
                result.className = top.className;
                result.confidence = top.confidence;
            } else {
                result.className = "无检测结果";
                result.confidence = 0f;
            }

            if (listener != null) {
                listener.onResult(result);
            }

        } catch (Exception e) {
            Log.e(TAG, "推理失败: " + e.getMessage(), e);
            if (listener != null) {
                listener.onError("推理失败: " + e.getMessage());
            }
        }
    }

    /**
     * 获取输出张量在指定维度的尺寸
     */
    private int getOutputSize(int tensorIndex, int dimension) {
        try {
            int[] shape = interpreter.getOutputTensor(tensorIndex).shape();
            if (shape != null && shape.length > dimension) {
                return shape[dimension];
            }
        } catch (Exception e) {
            Log.w(TAG, "无法获取输出张量形状: " + e.getMessage());
        }
        return -1;
    }

    /**
     * 将 Bitmap 转换为模型输入 ByteBuffer
     * 归一化到 [0, 1] 区间，RGB 通道顺序
     */
    private void convertBitmapToBuffer(Bitmap bitmap) {
        inputImageBuffer.rewind();
        int[] intValues = new int[inputSize * inputSize];
        bitmap.getPixels(intValues, 0, inputSize, 0, 0, inputSize, inputSize);

        int pixel = 0;
        for (int i = 0; i < inputSize; i++) {
            for (int j = 0; j < inputSize; j++) {
                final int val = intValues[pixel++];
                inputImageBuffer.putFloat(((val >> 16) & 0xFF) / NORMALIZE_VALUE); // R
                inputImageBuffer.putFloat(((val >> 8) & 0xFF) / NORMALIZE_VALUE);  // G
                inputImageBuffer.putFloat((val & 0xFF) / NORMALIZE_VALUE);         // B
            }
        }
    }

    // ==================== 配置方法 ====================

    /**
     * 设置自定义标签列表
     * @param labels 标签列表
     */
    public void setLabels(List<String> labels) {
        this.labels = labels;
    }

    /**
     * 获取标签列表
     * @return 标签列表
     */
    public List<String> getLabels() {
        return labels;
    }

    /**
     * 是否已初始化
     * @return true 已初始化
     */
    public boolean isInitialized() {
        return isInitialized;
    }

    // ==================== 资源释放 ====================

    /**
     * 释放模型资源
     * 必须在不再使用时调用，避免内存泄漏
     */
    public void release() {
        if (interpreter != null) {
            interpreter.close();
            interpreter = null;
        }
        isInitialized = false;
        Log.i(TAG, "TFLite 资源已释放");
    }
}
