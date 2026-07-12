# ============================================================================
# 文件名: proguard-rules.pro
# 功能描述:
#   - ProGuard 代码混淆规则配置文件
#   - 被 app/build.gradle 在 release 构建时引用
#   - 当前为空，添加自定义混淆规则时在此文件编辑
# 依赖关系:
#   - 被 app/build.gradle 引用
# 接口说明:
#   - 添加需要保留的类、方法、字段的混淆规则
# ============================================================================

# 保留 TFLite 相关类，避免混淆导致模型加载失败
-keep class org.tensorflow.lite.** { *; }

# 保留 Gson 序列化/反序列化的数据模型类
-keep class com.smarteye.blindguide.network.Protocol$* { *; }

# 保留 JNI 调用的类（如有）
-keepclasseswithmembernames class * {
    native <methods>;
}
