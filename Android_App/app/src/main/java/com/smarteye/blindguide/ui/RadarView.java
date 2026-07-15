/**
 * ============================================================================
 * 文件名: RadarView.java
 * 功能描述:
 *   HC-SR04 超声波传感器"类雷达"可视化 View
 *   前方扇形扫描区域，以半圆弧显示距离环，
 *   被检测障碍物在对应距离处高亮显示。
 *   传感器安装在前方中央，不旋转 — 方位固定为前方扇区。
 *
 * 视觉特性:
 *   - 深色背景模拟雷达屏幕（暗绿色网格）
 *   - 4个同心距离环（1m, 2m, 3m, 4m）
 *   - 前方扇形区域（±30°）高亮显示当前障碍物距离
 *   - 绿色=安全(>2.5m)，橙色=注意(1~2.5m)，红色=危险(<1m)
 *   - 实时距离数字叠加显示
 *
 * 依赖关系:
 *   - 被 fragment_debug.xml 引用
 *   - 被 DebugFragment 更新数据
 * ============================================================================
 */
package com.smarteye.blindguide.ui;

import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.DashPathEffect;
import android.graphics.Paint;
import android.graphics.Path;
import android.graphics.RectF;
import android.util.AttributeSet;
import android.view.View;

/**
 * HC-SR04 超声波雷达可视化 View
 */
public class RadarView extends View {

    // ==================== 绘制参数 ====================

    /** 雷达扇形角度（度），前方±30° */
    private static final float SWEEP_ANGLE = 60f;

    /** 起始角度（顶部=-90°），扇形中心指向正上方=前方 */
    private static final float START_ANGLE = -90f - SWEEP_ANGLE / 2f;

    /** 距离环数量 */
    private static final int RING_COUNT = 4;

    /** 每环对应距离(米) */
    private static final float METERS_PER_RING = 1.0f;

    /** 最大量程(米)，HC-SR04=4m */
    private static final float MAX_RANGE_M = 4.0f;

    /** 角度刻度间隔(度) */
    private static final float ANGLE_STEP = 15f;

    // ==================== 颜色定义 ====================

    private static final int COLOR_BG         = 0xFF0A0E0A;  // 深绿黑背景
    private static final int COLOR_GRID       = 0xFF1A3A1A;  // 网格线(暗绿)
    private static final int COLOR_TEXT       = 0xFF4CAF50;  // 文字(绿)
    private static final int COLOR_SAFE       = 0xFF4CAF50;  // 安全区域(绿, >2.5m)
    private static final int COLOR_CAUTION    = 0xFFFF9800;  // 注意区域(橙, 1~2.5m)
    private static final int COLOR_DANGER     = 0xFFF44336;  // 危险区域(红, <1m)
    private static final int COLOR_BLIP       = 0xFF00E5FF;  // 目标亮点(青色)
    private static final int COLOR_CENTER     = 0xFF2E7D32;  // 中心原点

    // ==================== 当前数据 ====================

    /** 当前超声波距离(米)，-1=无效 */
    private float ultrasonicDistance = -1f;

    /** 当前激光距离(米)，-1=无效 */
    private float laserDistance = -1f;

    /** 危险等级 (0=SAFE, 1=CAUTION, 2=DANGER) */
    private int dangerLevel = 0;

    /** 传感器在线状态 */
    private boolean sensorOnline = false;

    // ==================== Paint 对象 ====================

    private final Paint paintBg;
    private final Paint paintGrid;
    private final Paint paintRingLabel;
    private final Paint paintAngleLabel;
    private final Paint paintSafeFill;
    private final Paint paintCautionFill;
    private final Paint paintDangerFill;
    private final Paint paintBlip;
    private final Paint paintCenter;
    private final Paint paintStatusText;
    private final Paint paintDistText;

    public RadarView(Context context) {
        this(context, null);
    }

    public RadarView(Context context, AttributeSet attrs) {
        super(context, attrs);

        paintBg = new Paint(Paint.ANTI_ALIAS_FLAG);

        paintGrid = new Paint(Paint.ANTI_ALIAS_FLAG);
        paintGrid.setStyle(Paint.Style.STROKE);
        paintGrid.setStrokeWidth(1.5f);
        paintGrid.setColor(COLOR_GRID);
        paintGrid.setPathEffect(new DashPathEffect(new float[]{8, 4}, 0));

        paintRingLabel = new Paint(Paint.ANTI_ALIAS_FLAG);
        paintRingLabel.setColor(COLOR_TEXT);
        paintRingLabel.setTextSize(24f);
        paintRingLabel.setTextAlign(Paint.Align.LEFT);

        paintAngleLabel = new Paint(Paint.ANTI_ALIAS_FLAG);
        paintAngleLabel.setColor(COLOR_TEXT);
        paintAngleLabel.setTextSize(20f);
        paintAngleLabel.setTextAlign(Paint.Align.CENTER);

        paintSafeFill = new Paint(Paint.ANTI_ALIAS_FLAG);
        paintSafeFill.setStyle(Paint.Style.FILL);
        paintSafeFill.setColor(COLOR_SAFE);
        paintSafeFill.setAlpha(40);

        paintCautionFill = new Paint(Paint.ANTI_ALIAS_FLAG);
        paintCautionFill.setStyle(Paint.Style.FILL);
        paintCautionFill.setColor(COLOR_CAUTION);
        paintCautionFill.setAlpha(60);

        paintDangerFill = new Paint(Paint.ANTI_ALIAS_FLAG);
        paintDangerFill.setStyle(Paint.Style.FILL);
        paintDangerFill.setColor(COLOR_DANGER);
        paintDangerFill.setAlpha(80);

        paintBlip = new Paint(Paint.ANTI_ALIAS_FLAG);
        paintBlip.setStyle(Paint.Style.FILL);
        paintBlip.setColor(COLOR_BLIP);
        paintBlip.setAlpha(180);

        paintCenter = new Paint(Paint.ANTI_ALIAS_FLAG);
        paintCenter.setStyle(Paint.Style.FILL);
        paintCenter.setColor(COLOR_CENTER);

        paintStatusText = new Paint(Paint.ANTI_ALIAS_FLAG);
        paintStatusText.setColor(COLOR_TEXT);
        paintStatusText.setTextSize(28f);
        paintStatusText.setTextAlign(Paint.Align.CENTER);

        paintDistText = new Paint(Paint.ANTI_ALIAS_FLAG);
        paintDistText.setColor(Color.WHITE);
        paintDistText.setTextSize(40f);
        paintDistText.setTextAlign(Paint.Align.CENTER);
        paintDistText.setFakeBoldText(true);
    }

    // ==================== 公共接口 ====================

    /**
     * 更新超声波传感器数据
     * @param distanceM 距离(米)，-1表示无效
     * @param online    传感器在线状态
     */
    public void updateUltrasonic(float distanceM, boolean online) {
        this.ultrasonicDistance = distanceM;
        this.sensorOnline = online;
        invalidate();
    }

    /**
     * 更新激光传感器数据
     * @param distanceM 距离(米)
     */
    public void updateLaser(float distanceM) {
        this.laserDistance = distanceM;
        invalidate();
    }

    /**
     * 更新危险等级
     * @param level 0=SAFE, 1=CAUTION, 2=DANGER
     */
    public void updateDangerLevel(int level) {
        this.dangerLevel = level;
        invalidate();
    }

    @Override
    protected void onDraw(Canvas canvas) {
        super.onDraw(canvas);

        int w = getWidth();
        int h = getHeight();

        // 背景
        canvas.drawColor(COLOR_BG);

        // 计算雷达区域参数
        float radarRadius = Math.min(w, h * 1.8f) / 2f;
        float centerX = w / 2f;
        float centerY = h * 0.85f;  // 圆心偏下

        RectF radarOval = new RectF(
                centerX - radarRadius, centerY - radarRadius,
                centerX + radarRadius, centerY + radarRadius);

        // 绘制距离环
        for (int i = 1; i <= RING_COUNT; i++) {
            float ringRadius = radarRadius * i / RING_COUNT;
            RectF ringOval = new RectF(
                    centerX - ringRadius, centerY - ringRadius,
                    centerX + ringRadius, centerY + ringRadius);

            // 画圆弧
            Path arcPath = new Path();
            arcPath.addArc(ringOval, START_ANGLE, SWEEP_ANGLE);
            canvas.drawPath(arcPath, paintGrid);

            // 距离标注
            float labelAngle = (float) Math.toRadians(START_ANGLE - 5);
            float labelX = centerX + ringRadius * (float) Math.cos(labelAngle);
            float labelY = centerY + ringRadius * (float) Math.sin(labelAngle);
            canvas.drawText(i + "m", labelX, labelY, paintRingLabel);
        }

        // 绘制角度刻度线
        for (float a = -SWEEP_ANGLE / 2f; a <= SWEEP_ANGLE / 2f; a += ANGLE_STEP) {
            float angleRad = (float) Math.toRadians(-90f + a);
            float cosA = (float) Math.cos(angleRad);
            float sinA = (float) Math.sin(angleRad);

            // 从外环向外延伸
            float startR = radarRadius * 0.92f;
            float endR = radarRadius * 1.02f;
            canvas.drawLine(
                    centerX + startR * cosA, centerY + startR * sinA,
                    centerX + endR * cosA, centerY + endR * sinA,
                    paintGrid);

            // 角度标注
            float labelR = radarRadius * 1.1f;
            String label = ((int) a) + "°";
            canvas.drawText(label,
                    centerX + labelR * cosA,
                    centerY + labelR * sinA + 8,
                    paintAngleLabel);
        }

        // 绘制左侧/右侧边界线
        Paint borderPaint = new Paint(paintGrid);
        borderPaint.setPathEffect(null);
        borderPaint.setStrokeWidth(2f);
        for (float a : new float[]{START_ANGLE, START_ANGLE + SWEEP_ANGLE}) {
            float angleRad = (float) Math.toRadians(a);
            canvas.drawLine(centerX, centerY,
                    centerX + radarRadius * (float) Math.cos(angleRad),
                    centerY + radarRadius * (float) Math.sin(angleRad),
                    borderPaint);
        }

        // 绘制危险等级着色区域
        float dangerR  = radarRadius / MAX_RANGE_M * 1.0f;
        float cautionR = radarRadius / MAX_RANGE_M * 2.5f;
        float safeR    = radarRadius;

        // 危险填充 (< 1m)
        if (dangerR > 0) {
            RectF dangerOval = new RectF(
                    centerX - dangerR, centerY - dangerR,
                    centerX + dangerR, centerY + dangerR);
            Path dangerPath = new Path();
            dangerPath.addArc(dangerOval, START_ANGLE, SWEEP_ANGLE);
            dangerPath.lineTo(centerX, centerY);
            dangerPath.close();
            canvas.drawPath(dangerPath, paintDangerFill);
        }

        // 注意填充 (1m - 2.5m)
        {
            RectF cautionOval = new RectF(
                    centerX - cautionR, centerY - cautionR,
                    centerX + cautionR, centerY + cautionR);
            Path cautionPath = new Path();
            cautionPath.addArc(cautionOval, START_ANGLE, SWEEP_ANGLE);
            float innerAngleRad = (float) Math.toRadians(START_ANGLE);
            cautionPath.lineTo(centerX + dangerR * (float) Math.cos(innerAngleRad),
                    centerY + dangerR * (float) Math.sin(innerAngleRad));
            // 沿着内弧反向
            cautionPath.arcTo(new RectF(
                    centerX - dangerR, centerY - dangerR,
                    centerX + dangerR, centerY + dangerR),
                    START_ANGLE + SWEEP_ANGLE, -SWEEP_ANGLE);
            cautionPath.close();
            canvas.drawPath(cautionPath, paintCautionFill);
        }

        // 障碍物目标显示（当前位置）
        float dist = (ultrasonicDistance > 0) ? ultrasonicDistance : laserDistance;
        if (dist > 0 && dist <= MAX_RANGE_M) {
            float blipR = radarRadius * dist / MAX_RANGE_M;

            // 根据危险等级选择颜色
            Paint blipPaint = new Paint(paintBlip);
            switch (dangerLevel) {
                case 2: blipPaint.setColor(COLOR_DANGER); blipPaint.setAlpha(200); break;
                case 1: blipPaint.setColor(COLOR_CAUTION); blipPaint.setAlpha(180); break;
                default: blipPaint.setColor(COLOR_BLIP); blipPaint.setAlpha(160); break;
            }

            // 在前方扇形区域绘制高亮弧段
            float blipSweep = 20f;  // 目标弧宽
            RectF blipOval = new RectF(
                    centerX - blipR, centerY - blipR,
                    centerX + blipR, centerY + blipR);
            Path blipPath = new Path();
            blipPath.addArc(blipOval, -90f - blipSweep / 2f, blipSweep);

            Paint blipStroke = new Paint(blipPaint);
            blipStroke.setStyle(Paint.Style.STROKE);
            blipStroke.setStrokeWidth(6f);
            canvas.drawPath(blipPath, blipStroke);

            // 在目标位置绘制点
            float blipAngleRad = (float) Math.toRadians(-90f);
            float blipX = centerX + blipR * (float) Math.cos(blipAngleRad);
            float blipY = centerY + blipR * (float) Math.sin(blipAngleRad);
            Paint dotPaint = new Paint(blipPaint);
            dotPaint.setStyle(Paint.Style.FILL);
            canvas.drawCircle(blipX, blipY, 8f, dotPaint);
            canvas.drawCircle(blipX, blipY, 14f, blipStroke);
        }

        // 中心原点
        canvas.drawCircle(centerX, centerY, 8f, paintCenter);
        canvas.drawCircle(centerX, centerY, 4f, new Paint() {{
            setColor(0xFF00FF00); setStyle(Style.FILL);
        }});

        // 距离数字显示 (屏幕中间上方)
        if (dist > 0 && dist <= MAX_RANGE_M) {
            String distText = String.format("%.2f m", dist);
            canvas.drawText(distText, centerX, h * 0.15f, paintDistText);

            String sourceText;
            if (ultrasonicDistance > 0 && laserDistance > 0) {
                sourceText = "超声波+激光";
            } else if (ultrasonicDistance > 0) {
                sourceText = "HC-SR04 超声波";
            } else {
                sourceText = "SDM10 激光";
            }
            paintStatusText.setTextSize(20f);
            canvas.drawText(sourceText, centerX, h * 0.15f + 36, paintStatusText);
        } else if (!sensorOnline) {
            paintStatusText.setTextSize(32f);
            canvas.drawText("传感器离线", centerX, centerY - 40, paintStatusText);
        } else {
            paintStatusText.setTextSize(32f);
            canvas.drawText("等待数据...", centerX, centerY - 40, paintStatusText);
        }

        // 传感器在线指示
        Paint onlineDot = new Paint(Paint.ANTI_ALIAS_FLAG);
        onlineDot.setStyle(Paint.Style.FILL);
        onlineDot.setColor(sensorOnline ? 0xFF4CAF50 : 0xFFF44336);
        canvas.drawCircle(w - 24, 24, 10, onlineDot);
        paintStatusText.setTextSize(20f);
        paintStatusText.setTextAlign(Paint.Align.RIGHT);
        canvas.drawText(sensorOnline ? "HC-SR04在线" : "HC-SR04离线",
                w - 42, 30, paintStatusText);
    }
}
