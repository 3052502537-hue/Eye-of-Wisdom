/**
 * ============================================================================
 * 文件名: TFLiteClassifier.java
 * 功能描述:
 *   - 基于 TensorFlow Lite 的视觉识别推理引擎
 *   - 支持盲道检测、红绿灯识别、大型障碍物、路口识别、斑马线识别
 *   - 加载 assets 目录下的 .tflite 模型文件
 *   - 接收 JPEG 字节数据，解码为 Bitmap 后进行推理
 * 依赖关系:
 *   - 依赖 TensorFlow Lite 库
 *   - 依赖 AppConfig 获取模型配置参数
 *   - 被 ObstacleAnalyzer 调用进行视觉识别
 *   - 被 DebugFragment 用于显示识别结果
 * 接口说明:
 *   - initialize(Context): 加载模型，初始化推理引擎
 *   - classify(byte[]): 输入 JPEG 数据，返回识别结果
 *   - release(): 释放模型资源
 *   - 通过 OnClassifyResultListener 回调识别结果
 * ============================================================================
 */
package com.smarteye.blindguide.ai;

import android.content.Context;
import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.util.Log;

import com.smarteye.blindguide.data.AppConfig;

import org.tensorflow.lite.Interpreter;
import org.tensorflow.lite.gpu.CompatibilityList;
import org.tensorflow.lite.gpu.GpuDelegate;

import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;

/**
 * TFLite 视觉识别分类器
 * 负责加载模型并执行图像分类推理
 */
public class TFLiteClassifier {

    private static final String TAG = "TFLiteClassifier";

    /** 输入图像通道数（RGB） */
    private static final int CHANNELS = 3;

    /** 像素值归一化因子 */
    private static final float NORMALIZE_VALUE = 255.0f;

    /** TFLite 解释器 */
    private Interpreter interpreter;

    /** GPU 代理（可选，加速推理） */
    private GpuDelegate gpuDelegate;

    /** 输入图像尺寸 */
    private final int inputSize;

    /** 模型输入字节数组 */
    private ByteBuffer inputImageBuffer;

    /** 模型输出数组 */
    private float[][] outputBuffer;

    /** 分类标签列表 */
    private List<String> labels;

    /** 是否已初始化 */
    private boolean isInitialized = false;

    /**
     * 识别结果回调接口
     */
    public interface OnClassifyResultListener {
        /**
         * 识别完成回调
         * @param result 识别结果
         */
        void onResult(ClassifyResult result);

        /**
         * 识别错误回调
         * @param error 错误信息
         */
        void onError(String error);
    }

    /**
     * 识别结果数据结构
     */
    public static class ClassifyResult {
        /** 识别到的类别名称 */
        public String className;

        /** 置信度（0.0~1.0） */
        public float confidence;

        /** 所有类别的置信度分布 */
        public float[] probabilities;

        /** 推理耗时（毫秒） */
        public long inferenceTime;

        /** 输入图像 Bitmap（调试用） */
        public Bitmap inputBitmap;

        public ClassifyResult() {}

        /**
         * 构造识别结果
         * @param className 类别名称
         * @param confidence 置信度
         */
        public ClassifyResult(String className, float confidence) {
            this.className = className;
            this.confidence = confidence;
        }
    }

    /**
     * 构造分类器
     * 使用 AppConfig 中的默认输入尺寸
     */
    public TFLiteClassifier() {
        this(AppConfig.TFLITE_INPUT_SIZE);
    }

    /**
     * 构造分类器
     * @param inputSize 模型输入图像尺寸（像素）
     */
    public TFLiteClassifier(int inputSize) {
        this.inputSize = inputSize;
    }

    /**
     * 初始化分类器
     * 加载模型文件和标签
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

            // 配置解释器选项
            Interpreter.Options options = new Interpreter.Options();

            // 尝试启用 GPU 加速（失败则回退到 CPU）
            try {
                CompatibilityList compatibilityList = new CompatibilityList();
                if (compatibilityList.isDelegateSupportedOnThisDevice()) {
                    gpuDelegate = new GpuDelegate();
                    options.addDelegate(gpuDelegate);
                    Log.i(TAG, "GPU 加速已启用");
                }
            } catch (Exception e) {
                Log.w(TAG, "GPU 加速不可用，使用 CPU: " + e.getMessage());
                options.setNumThreads(4); // CPU 多线程
            }

            // 创建解释器
            interpreter = new Interpreter(modelBuffer, options);

            // 初始化输入输出缓冲区
            inputImageBuffer = ByteBuffer.allocateDirect(
                    4 * inputSize * inputSize * CHANNELS);
            inputImageBuffer.order(ByteOrder.nativeOrder());

            // 输出维度：[1, numLabels]，标签数将在加载标签后确定
            // 默认假设 5 个类别（盲道、红绿灯、障碍物、路口、斑马线）
            int numLabels = 5;
            outputBuffer = new float[1][numLabels];

            // 加载分类标签
            labels = loadDefaultLabels();

            isInitialized = true;
            Log.i(TAG, "TFLite 模型初始化成功，输入尺寸: " + inputSize);
            return true;

        } catch (Exception e) {
            Log.e(TAG, "初始化失败: " + e.getMessage(), e);
            return false;
        }
    }

    /**
     * 加载模型文件到 ByteBuffer
     * @param context 上下文
     * @param modelFileName 模型文件名
     * @return 模型数据 ByteBuffer，失败返回 null
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
     * 预定义的 5 个识别类别
     * @return 标签列表
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

    /**
     * 执行图像分类
     * @param jpegData JPEG 图像字节数据
     * @param listener 识别结果回调监听器
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

            // 执行推理
            interpreter.run(inputImageBuffer, outputBuffer);

            long inferenceTime = System.currentTimeMillis() - startTime;

            // 解析结果，找到置信度最高的类别
            int maxIndex = 0;
            float maxProb = outputBuffer[0][0];
            for (int i = 1; i < outputBuffer[0].length; i++) {
                if (outputBuffer[0][i] > maxProb) {
                    maxProb = outputBuffer[0][i];
                    maxIndex = i;
                }
            }

            // 构建结果对象
            ClassifyResult result = new ClassifyResult();
            result.className = (maxIndex < labels.size()) ? labels.get(maxIndex) : "未知";
            result.confidence = maxProb;
            result.probabilities = outputBuffer[0].clone();
            result.inferenceTime = inferenceTime;
            result.inputBitmap = scaledBitmap;

            if (listener != null) {
                listener.onResult(result);
            }

        } catch (Exception e) {
            if (listener != null) {
                listener.onError("推理失败: " + e.getMessage());
            }
        }
    }

    /**
     * 将 Bitmap 转换为模型输入 ByteBuffer
     * 归一化到 [0, 1] 区间
     * @param bitmap 输入图像
     */
    private void convertBitmapToBuffer(Bitmap bitmap) {
        inputImageBuffer.rewind();
        int[] intValues = new int[inputSize * inputSize];
        bitmap.getPixels(intValues, 0, inputSize, 0, 0, inputSize, inputSize);

        int pixel = 0;
        for (int i = 0; i < inputSize; i++) {
            for (int j = 0; j < inputSize; j++) {
                final int val = intValues[pixel++];
                // 提取 RGB 通道并归一化
                inputImageBuffer.putFloat(((val >> 16) & 0xFF) / NORMALIZE_VALUE); // R
                inputImageBuffer.putFloat(((val >> 8) & 0xFF) / NORMALIZE_VALUE);  // G
                inputImageBuffer.putFloat((val & 0xFF) / NORMALIZE_VALUE);         // B
            }
        }
    }

    /**
     * 设置自定义标签列表
     * @param labels 标签列表
     */
    public void setLabels(List<String> labels) {
        this.labels = labels;
        if (labels != null && !labels.isEmpty()) {
            outputBuffer = new float[1][labels.size()];
        }
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

    /**
     * 释放模型资源
     * 必须在不再使用时调用，避免内存泄漏
     */
    public void release() {
        if (interpreter != null) {
            interpreter.close();
            interpreter = null;
        }
        if (gpuDelegate != null) {
            gpuDelegate.close();
            gpuDelegate = null;
        }
        isInitialized = false;
        Log.i(TAG, "TFLite 资源已释放");
    }
}
