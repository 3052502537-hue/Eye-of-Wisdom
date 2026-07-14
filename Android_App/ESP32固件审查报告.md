# 导盲头环 ESP32 固件代码审查报告

> 审查范围：`ESP32_Main/`（主控板）vs `ESP32_Camera/`（摄像头板）vs `Android_App/`
> 审查日期：2026-07-13

---

## 一、致命缺陷（不改则通信完全失败）

### F1. SPI 协议帧格式不一致（主控 ↔ 摄像头）

两块板子的 SPI 帧格式**完全不同**，无法通信：

| 字段 | 主控板 (protocol.h) | 摄像头板 (config.h + spi_slave_comm.cpp) |
|------|---------------------|------------------------------------------|
| 数据长度字段 | **2 字节，小端** | **4 字节，大端** |
| 帧开销 | 8 字节 | 10 字节 |

**修复建议**：统一到一种格式。推荐主控板 `protocol.h` 的 2 字节小端格式（更紧凑），然后同步修改摄像头板 `config.h` 中 `SPI_FRAME_LEN_LEN` 从 4 改为 2，`_buildFrame` 中长度写入从小端改为大端或统一小端。

### F2. SPI 命令码不一致（主控 ↔ 摄像头）

| 命令 | 主控板 (protocol.h) | 摄像头板 (config.h) |
|------|---------------------|---------------------|
| 图像帧起始 | `0x01` SPI_CMD_IMG_FRAME_START | — |
| 图像帧数据 | `0x02` SPI_CMD_IMG_FRAME_DATA | — |
| 图像帧结束 | `0x03` SPI_CMD_IMG_FRAME_END | — |
| 通用图像帧 | — | `0x01` SPI_CMD_IMG_FRAME（数据/元数据都用这个） |
| 心跳 | `0x30` | `0x02` |
| 应答 | `0x20` ACK / `0x21` NACK | `0x03` ACK |

**主控板期望**：START(0x01) → 多个 DATA(0x02) → END(0x03) 三步协议
**摄像头板实现**：全部用 0x01 发送（元数据+数据块），没有 START/DATA/END 区分

**修复建议**：摄像头板 `sendFrame()` 改为三步协议：元数据用 `0x01`、数据块用 `0x02`、结尾用 `0x03`。心跳统一为 `0x30`。

### F3. DATA_READY 信号极性相反

| 端 | 就绪信号 | 空闲信号 |
|----|----------|----------|
| 主控板 | 检测 **下降沿** (HIGH→LOW) | 上拉 HIGH |
| 摄像头板 | 输出 **HIGH** | 输出 LOW |

主控板认为 LOW=有数据，摄像头板输出 HIGH=有数据。**完全相反**。

**修复建议**：统一极性。推荐摄像头板改为 `DATA_READY_ACTIVE = LOW`（低电平有效更常见，且与主控板 `INPUT_PULLUP` 配合更好）。

### F4. WiFi/TCP/UDP 端口号不一致（主控 ↔ Android APP）

| 参数 | ESP32 主控 (config.h) | Android APP (AppConfig.java) |
|------|----------------------|------------------------------|
| TCP 端口 | **8080** | **8888** |
| UDP 端口 | **9090** | **8889** |
| AP SSID | **BlindGuide_HeadRing** | **BlindGuide_AP** |
| AP 密码 | 12345678 | 12345678 ✅ |

**修复建议**：统一。APP 端统一到固件端，或反之。推荐用 APP 端的值（更短更好记）：TCP=8888, UDP=8889, SSID=BlindGuide_AP。

### F5. TCP JSON 数据格式不一致（主控 → Android APP）

**主控板发送** (task_manager.cpp:280-291)：
```json
{"type":"sensor","ts":...,"laser":{"dist":...,"valid":...},
 "radar_f":{"count":...,"valid":...},"radar_r":{"count":...,"valid":...},"level":...,"img":...}
```

**Android APP 解析** (Protocol.java 对应 SensorData)：
```java
// 期望字段: laser_front(数值), radar_front.dist/speed/angle, radar_back.dist/speed/angle
```

差异：
1. 字段名不匹配：`laser` vs `laser_front`, `radar_f` vs `radar_front`
2. 雷达数据无目标数组——固件只发 count/valid，APP 需要 dist/speed/angle 数组
3. 缺少 `battery` 字段

**修复建议**：以 README 中约定的 JSON 格式为准，主控板 `taskWifi()` 需要重构 JSON 组装逻辑。

---

## 二、高风险缺陷

### H1. 摄像头板：帧数据 use-after-free（ESP32_Camera.ino:133）

```cpp
frame.data = fb->buf;                          // 指向帧缓冲
xQueueSend(g_frameQueue, &frame, 0);           // 入队（引用指针）
g_camera.returnFrame(fb);                      // ← 立即释放！
// SPI 发送任务还没用这个数据，fb->buf 已被回收
```

代码注释自己都说了："完整实现应在此处拷贝数据或使用双缓冲机制"。

**修复建议**：在 `TransferFrame_t` 中预分配 `uint8_t data[JPEG_MAX_SIZE]`，入队前 `memcpy`。

### H2. 工作模式枚举不一致（主控 ↔ Android）

| 模式值 | ESP32 固件 (protocol.h) | Android APP (AppConfig.java) |
|--------|------------------------|------------------------------|
| 0 | MODE_NORMAL (正常导盲) | — |
| 1 | MODE_OUTDOOR (室外) | MODE_SENSOR_ONLY (传感器模式) |
| 2 | MODE_INDOOR (室内) | MODE_AUTO (自动模式) |
| 3 | MODE_SLEEP (省电) | MODE_RISK_ONLY (风险模式) |

当 APP 发送 `set_mode 2` 时，固件会切成"室内模式"而不是"自动模式"。

**修复建议**：统一模式定义。以 APP 的 3 种模式为准（1=传感器/2=自动/3=风险）。

### H3. 预警距离阈值不一致

| 等级 | ESP32 (config.h) | Android (AppConfig.java) |
|------|-------------------|--------------------------|
| 安全 | > 3.0m | > 3.0m ✅ |
| 注意 | < 3.0m | < 2.5m ❌ |
| 危险 | < 1.0m | < 1.0m ✅ |

**修复建议**：统一注意阈值。推荐用 APP 的 2.5m（与规划书一致）。

### H4. 摄像头板 `sendFrame()` 元数据格式与主控 `ImgFrameStart_t` 不匹配

摄像头板发送 9 字节元数据（width 2B + height 2B + format 1B + totalSize 4B）。  
主控板 `ImgFrameStart_t` 是 8 字节（width 2B + height 2B + format 1B + totalSize 4B，但字段对齐后 sizeof 可能不同，且依赖 struct packing）。

额外差 1 字节，且 `ImgFrameData_t` 期望有 `blockIndex` + `blockSize` 前缀，摄像头板没有发。

**修复建议**：摄像头板数据块增加 `blockIndex(2B) + blockSize(2B)` 前缀。或者直接废弃分块协议，重新设计统一的帧格式。

---

## 三、中等缺陷

### M1. SPI 从机使用 `DMA_CHANNEL_1` 可能与主控冲突
`spi_slave_comm.cpp:146` 硬编码 `DMA_CHANNEL_1`。ESP32-S3 只有 5 个 DMA 通道，如果主控板也用了某些外设的 DMA，可能冲突。建议改用 `spi_slave_initialize(..., DMA_CHANNEL_AUTO)` 或使用 `SPI_DMA_CH_AUTO`。

### M2. RD-03D 雷达协议是占位符
`rd03d_driver.cpp` 中帧格式（帧头 `0xAA 0xFF 0x03 0x00`、目标 4 字节、CRC16）是假设的示例格式。项目规划书标记 Q2 为"待确认"。需要拿到实际手册后重写 `parseFrame()`。

### M3. SDM10 激光协议同样是占位符
`sdm10_driver.cpp` 帧格式同样是假设的（`0xAA 0x55` + 距离 2B cm + 累加和）。规划书标记 Q1 为"待确认"。

### M4. ESP32 主控 taskWifi 未发送完整雷达数据
`taskWifi()` 只发 `radar_f: {count, valid}`，缺少实际目标数组。APP 端 `ObstacleAnalyzer` 需要 dist/speed/angle 才能做融合决策。

### M5. taskDecision 在释放互斥锁后再次读取 `_sensorFrame.level`
`task_manager.cpp:342` 访问 `_sensorFrame.level` 时已无锁保护。虽 uint8_t 读写原子，但代码不规范。

---

## 四、轻微问题

### L1. 摄像头板 DEBUG 默认关闭，但 `DBG_PRINT` 宏接收参数
`config.h:40` 的 `#define DEBUG` 被注释掉。但 `ESP32_Camera.ino` 中大量使用 `DBG_PRINTF/DBG_PRINTLN`，编译后这些调用会变成空操作——没问题，但串口会完全静默。

### L2. `ImageFrame_t.data` 指针生命周期不明确
`spi_master_comm.cpp:250` 设置 `_latestFrame.data = _imgBuf`（指向成员数组），外部通过 `getLatestFrame()` 取指针后应立即拷贝。但目前 taskWifi 中的 `sendImageUdp` 立即使用数据，暂无不安全。

### L3. `taskCommHandler` 中帧率设置未实际生效
`ESP32_Camera.ino:250-253` 只是打印日志，未修改全局帧率变量。实际帧率由 `CAMERA_FRAME_INTERVAL_MS` 宏编译期确定。

### L4. 摄像头板与主控板的 SPI 引脚在各自 config.h 中定义，但命名不一致
主控 `config.h`：`PIN_SPI_MOSI=11, PIN_SPI_MISO=13, PIN_SPI_SCK=12, PIN_SPI_CS=10`  
摄像头 `config.h`：`PIN_SPI_MOSI=41, PIN_SPI_MISO=42, PIN_SPI_SCLK=40, PIN_SPI_CS=39, PIN_DATA_READY=38`

两块板引脚号不同是正确的（它们是两块独立的 ESP32），但 **命名交叉对应关系** 容易让接线者混淆。需要在 README 中明确标注交叉接线表。

---

## 五、修复优先级

| 优先级 | 编号 | 问题 | 影响 |
|--------|------|------|------|
| **立即** | F1-F5 | SPI 协议/端口/JSON 不一致 | 板间通信和 APP 连接完全不可用 |
| **高** | H1 | Camera use-after-free | 图像数据随机损坏 |
| **高** | H2 | 工作模式编码不一致 | 模式切换行为错误 |
| **中** | H3 | 预警阈值不一致 | 影响安全判断精度 |
| **中** | M4 | JSON 缺少雷达目标数组 | APP 端传感器融合缺数据 |
| **低** | M1-M3 | DMA/协议占位符 | 需要实物调试验证 |
