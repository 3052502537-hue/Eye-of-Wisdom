/**
 * ============================================================================
 * 文件名: DetectionOverlayView.java
 * 功能描述:
 *   - 自定义 View，用于显示视频帧并叠加 AI 目标检测框图
 *   - 底层 ImageView 显示 JPEG 解码后的 Bitmap
 *   - 顶层 Canvas 覆盖层绘制检测框 + 类别标签 + 置信度
 *   - 支持开关叠加层（setShowOverlay）
 *   - 预留大模型分析框图接口，后续可直接调用 setDetections() 绘制
 * 依赖关系:
 *   - 依赖 TFLiteClassifier.DetectedObject 数据结构
 *   - 被 fragment_debug.xml 引用（自定义 View 标签）
 * 接口说明:
 *   - setImageBitmap(Bitmap): 设置底图
 *   - setDetections(List<DetectedObject>): 设置检测框列表
 *   - setShowOverlay(boolean): 开关叠加层
 *   - setFrameInfo(int, int, int): 设置帧信息（帧号、宽度、高度）
 * ============================================================================
 */
package com.smarteye.blindguide.ui;

import android.content.Context;
import android.graphics.Bitmap;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.graphics.RectF;
import android.util.AttributeSet;
import android.view.View;
import android.widget.FrameLayout;
import android.widget.ImageView;

import com.smarteye.blindguide.ai.TFLiteClassifier.DetectedObject;

import java.util.ArrayList;
import java.util.List;

/**
 * 视频帧显示 + AI 检测框叠加自定义 View
 */
public class DetectionOverlayView extends FrameLayout {

    /** 检测框颜色表（按标签索引，与 TFLiteClassifier 的 labels 一致） */
    private static final int[] BOX_COLORS = {
            Color.argb(200, 76, 175, 80),    // 0 盲道 — 绿色
            Color.argb(200, 244, 67, 54),    // 1 红绿灯 — 红色
            Color.argb(200, 255, 193, 7),    // 2 大型障碍物 — 琥珀色
            Color.argb(200, 0, 188, 212),    // 3 路口 — 青色
            Color.argb(200, 156, 39, 176)    // 4 斑马线 — 紫色
    };

    private static final int DEFAULT_COLOR = Color.argb(200, 255, 255, 255);

    /** 底层 ImageView */
    private ImageView imageView;

    /** 顶层覆盖层 View */
    private OverlayView overlayView;

    /** 是否显示叠加层 */
    private boolean showOverlay = true;

    /** 当前检测目标列表 */
    private List<DetectedObject> detections = new ArrayList<>();

    /** 帧信息 */
    private int frameNumber = 0;
    private int imageWidth = 0;
    private int imageHeight = 0;

    public DetectionOverlayView(Context context) {
        super(context);
        init(context);
    }

    public DetectionOverlayView(Context context, AttributeSet attrs) {
        super(context, attrs);
        init(context);
    }

    public DetectionOverlayView(Context context, AttributeSet attrs, int defStyleAttr) {
        super(context, attrs, defStyleAttr);
        init(context);
    }

    /**
     * 初始化子 View
     */
    private void init(Context context) {
        // 底层：ImageView 显示 JPEG
        imageView = new ImageView(context);
        imageView.setScaleType(ImageView.ScaleType.FIT_CENTER);
        imageView.setBackgroundColor(Color.BLACK);
        LayoutParams imageParams = new LayoutParams(
                LayoutParams.MATCH_PARENT, LayoutParams.MATCH_PARENT);
        addView(imageView, imageParams);

        // 顶层：Canvas 覆盖层绘制检测框
        overlayView = new OverlayView(context);
        overlayView.setBackgroundColor(Color.TRANSPARENT);
        LayoutParams overlayParams = new LayoutParams(
                LayoutParams.MATCH_PARENT, LayoutParams.MATCH_PARENT);
        addView(overlayView, overlayParams);
    }

    /**
     * 设置底图 Bitmap
     * @param bitmap JPEG 解码后的图像
     */
    public void setImageBitmap(Bitmap bitmap) {
        if (bitmap != null) {
            imageWidth = bitmap.getWidth();
            imageHeight = bitmap.getHeight();
        }
        imageView.setImageBitmap(bitmap);
    }

    /**
     * 设置检测目标列表，触发覆盖层重绘
     * @param detections 检测目标列表（按置信度降序）
     */
    public void setDetections(List<DetectedObject> detections) {
        this.detections = (detections != null) ? detections : new ArrayList<>();
        overlayView.invalidate();
    }

    /**
     * 开关检测框叠加层
     * @param show true=显示检测框, false=仅显示图像
     */
    public void setShowOverlay(boolean show) {
        this.showOverlay = show;
        overlayView.setVisibility(show ? View.VISIBLE : View.GONE);
    }

    /**
     * 是否显示叠加层
     */
    public boolean isShowOverlay() {
        return showOverlay;
    }

    /**
     * 设置帧信息（用于调试显示）
     * @param frameNum 帧序号
     * @param width 图像宽度
     * @param height 图像高度
     */
    public void setFrameInfo(int frameNum, int width, int height) {
        this.frameNumber = frameNum;
        this.imageWidth = width;
        this.imageHeight = height;
    }

    /**
     * 清除所有检测框
     */
    public void clearDetections() {
        this.detections.clear();
        overlayView.invalidate();
    }

    // ==================== 内部类：覆盖层 View ====================

    /**
     * Canvas 覆盖层，绘制检测框和标签
     */
    private class OverlayView extends View {

        private final Paint boxPaint;
        private final Paint textPaint;
        private final Paint textBgPaint;

        public OverlayView(Context context) {
            super(context);

            // 检测框画笔
            boxPaint = new Paint();
            boxPaint.setStyle(Paint.Style.STROKE);
            boxPaint.setStrokeWidth(4f);
            boxPaint.setAntiAlias(true);

            // 标签文本画笔
            textPaint = new Paint();
            textPaint.setColor(Color.WHITE);
            textPaint.setTextSize(32f);
            textPaint.setAntiAlias(true);
            textPaint.setFakeBoldText(true);

            // 标签背景画笔
            textBgPaint = new Paint();
            textBgPaint.setStyle(Paint.Style.FILL);
            textBgPaint.setAntiAlias(true);
        }

        @Override
        protected void onDraw(Canvas canvas) {
            super.onDraw(canvas);

            if (detections.isEmpty()) {
                return;
            }

            float viewWidth = getWidth();
            float viewHeight = getHeight();

            // ImageView scaleType=FIT_CENTER 时的缩放和偏移计算
            float scaleW = viewWidth / Math.max(1, imageWidth);
            float scaleH = viewHeight / Math.max(1, imageHeight);
            float scale = Math.min(scaleW, scaleH);

            float drawWidth = imageWidth * scale;
            float drawHeight = imageHeight * scale;
            float offsetX = (viewWidth - drawWidth) / 2f;
            float offsetY = (viewHeight - drawHeight) / 2f;

            // 绘制每个检测框
            for (DetectedObject det : detections) {
                // 归一化坐标 → 实际绘制坐标
                float left = offsetX + det.left * drawWidth;
                float top = offsetY + det.top * drawHeight;
                float right = offsetX + det.right * drawWidth;
                float bottom = offsetY + det.bottom * drawHeight;

                // 根据类别选择颜色
                int colorIdx = getColorIndex(det.className);
                int color = (colorIdx >= 0 && colorIdx < BOX_COLORS.length)
                        ? BOX_COLORS[colorIdx] : DEFAULT_COLOR;

                // 绘制检测框
                boxPaint.setColor(color);
                canvas.drawRect(left, top, right, bottom, boxPaint);

                // 绘制标签背景
                String label = String.format("%s %.0f%%", det.className, det.confidence * 100);
                float textWidth = textPaint.measureText(label);
                float textHeight = textPaint.getTextSize();
                float labelTop = top - textHeight - 4f;
                if (labelTop < 0) {
                    labelTop = bottom + 4f; // 框在顶部时标签放下面
                }

                textBgPaint.setColor(color);
                canvas.drawRect(left, labelTop, left + textWidth + 8f,
                        labelTop + textHeight + 4f, textBgPaint);

                // 绘制标签文本
                canvas.drawText(label, left + 4f, labelTop + textHeight, textPaint);
            }
        }

        /**
         * 根据类别名获取颜色索引
         * 与 TFLiteClassifier.loadDefaultLabels() 的顺序一致
         */
        private int getColorIndex(String className) {
            if (className == null) return -1;
            if (className.contains("盲道")) return 0;
            if (className.contains("红绿灯")) return 1;
            if (className.contains("障碍物")) return 2;
            if (className.contains("路口")) return 3;
            if (className.contains("斑马线")) return 4;
            return -1;
        }
    }
}
