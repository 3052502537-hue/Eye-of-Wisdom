# "智慧之眼"导盲头环 — 基于大模型的互动式导盲头环设计与实现

> 为视障人士设计的智能导盲头环，集成激光测距、超声波测距、AI视觉识别，通过语音引导用户安全出行。

## 项目简介

本项目是一个基于双ESP32 + Android的智能导盲头环系统。主控ESP32通过SDM10激光测距和HC-SR04超声波传感器实现前方障碍物检测，摄像板ESP32独立采集OV2640图像并通过WiFi直传手机App。手机App作为统一上位机，提供传感器雷达可视化、AI视觉检测和TTS语音避障引导。

## 系统架构 (v3.0)

```
┌──────────────────────────────────────────────────────────────┐
│                      手机 APP (Android)                       │
│  ┌──────────┐  ┌──────────────┐  ┌──────────┐  ┌─────────┐ │
│  │ TCP接收   │  │ HTTP MJPEG   │  │ TFLite   │  │ TTS语音 │ │
│  │ 传感器JSON│  │ 视频流拉取   │  │ 视觉识别  │  │ 播报    │ │
│  └────┬─────┘  └──────┬───────┘  └────┬─────┘  └────┬────┘ │
│       │               │               │              │      │
│       │   TCP:8888    │   HTTP:80     │              │      │
│       │   (JSON)      │   (/video)    │              │      │
│       │               │               │              │      │
│  ┌────┴───────────────┴───────────────┴──────────────┘      │
│  │           避障决策引擎 + HC-SR04雷达可视化                 │
│  │           AI检测框叠加 + 原图直出双屏显示                  │
│  └──────────────────────────────────────────────────────────┘
│                       │ WiFi STA (连接主控AP)
└───────────────────────┼──────────────────────────────────────┘
                        │
              ┌─────────┴─────────┐
              │  ESP32_Main AP    │  192.168.4.1
              │  (WiFi 热点)      │
              └────────┬─────────┘
                       │
       ┌───────────────┼───────────────┐
       │               │               │
┌──────┴──────┐  ┌─────┴─────┐        │
│ ESP32_Main  │  │ESP32_Camera│        │
│ 主控板      │  │摄像板      │        │
│             │  │            │        │
│ SDM10 激光  │  │ OV2640     │        │
│ HC-SR04超声 │  │ WiFi STA   │        │
│ 蜂鸣器+RGB  │  │ HTTP服务器 │        │
│ WiFi AP     │  │ 192.168.4.10│       │
│ 192.168.4.1 │  │            │        │
└─────────────┘  └────────────┘        │
                                       │
                         📱 手机App直连摄像板拉流
                         http://192.168.4.10/video
```

**关键设计变化 (v3.0):**
- **主控板与摄像板无硬件连接** — 摄像板通过WiFi STA独立连接主控AP
- **摄像板直传手机** — HTTP MJPEG视频流，不经主控中转
- **HC-SR04替代Rd-03D** — 前方超声波测距，成本更低
- **手机App作为统一上位机** — 接收传感器数据 + 拉取视频流 + AI推理 + 可视化展示

## 目录结构

```
Eye-of-Wisdom/
├── ESP32_Main/                 # 主控板代码（ESP32-S3-N16R8）
│   ├── ESP32_Main.ino          # 主程序入口
│   ├── config.h                # 引脚定义和全局配置
│   ├── protocol.h              # 通信协议定义（JSON数据格式）
│   ├── sdm10_driver.h/.cpp     # SDM10 激光测距驱动（UART, 10m量程）
│   ├── hc_sr04_driver.h/.cpp   # HC-SR04 超声波驱动（GPIO, 2cm-4m）
│   ├── wifi_manager.h/.cpp     # WiFi AP + TCP服务器
│   ├── web_server.h/.cpp       # Web配置页面（调试/校准）
│   ├── alarm_manager.h/.cpp    # 蜂鸣器 + WS2812 RGB控制
│   └── task_manager.h/.cpp     # FreeRTOS 4任务调度
│
├── ESP32_Camera/               # 摄像板代码（ESP32-S3-WROOM）
│   ├── ESP32_Camera.ino        # 主程序入口
│   ├── config.h                # 引脚定义、WiFi STA、HTTP配置
│   ├── camera_driver.h/.cpp    # OV2640驱动（VGA + JPEG压缩）
│   └── camera_web_server.h/.cpp # HTTP MJPEG服务器（/video, /capture）
│
├── Android_App/                # 安卓APP代码（Java）
│   └── app/src/main/
│       ├── java/com/smarteye/blindguide/
│       │   ├── MainActivity.java       # 主Activity + 底部导航
│       │   ├── ui/                     # 页面Fragment
│       │   │   ├── MainFragment.java   # 主界面
│       │   │   ├── SettingsFragment.java
│       │   │   ├── DebugFragment.java  # 开发者调试面板 (v3.0)
│       │   │   ├── RadarView.java      # HC-SR04雷达可视化 (v3.0新增)
│       │   │   └── DetectionOverlayView.java # AI检测框叠加
│       │   ├── network/
│       │   │   ├── TCPClient.java      # TCP接收传感器JSON
│       │   │   ├── CameraHttpClient.java # HTTP MJPEG拉流
│       │   │   └── Protocol.java       # 协议解析
│       │   ├── ai/
│       │   │   └── TFLiteClassifier.java  # TFLite视觉推理
│       │   ├── tts/
│       │   │   └── TTSManager.java     # 语音播报
│       │   ├── logic/
│       │   │   └── ObstacleAnalyzer.java  # 避障决策引擎
│       │   └── data/
│       │       └── AppConfig.java      # 全局配置
│       └── res/layout/                 # 布局资源
│
├── 智慧之眼导盲头环/            # 项目规划文档
├── requirements.txt            # Python工具链依赖
├── library_dependencies.txt    # Arduino库依赖清单
├── LICENSE                     # GPLv3 开源协议
└── README.md
```

## 硬件清单

| 组件 | 型号 | 数量 | 说明 |
|------|------|------|------|
| 主控板 | ESP32-S3-N16R8 | 1 | 16MB Flash, 8MB PSRAM, 系统主控 |
| 摄像板 | ESP32-S3-WROOM | 1 | 连接OV2640摄像头，WiFi STA |
| 摄像头 | OV2640 | 1 | 200万像素, DVP接口, VGA JPEG |
| 激光测距 | SDM10 | 1 | 前方, 10m量程, UART, ±5cm |
| 超声波 | HC-SR04 | 1 | 前方, 2cm-400cm, GPIO, ±3mm |
| 蜂鸣器 | 有源蜂鸣器 | 1 | GPIO47, PWM控制 |
| RGB LED | WS2812（板载） | 1 | GPIO48, 状态指示 |
| 电池 | 18650锂电池 | 1 | 3.7V, 5V/3.3V双路供电 |

## 引脚分配

### 主控板（ESP32-S3-N16R8）
| 功能 | 引脚 | 接口 |
|------|------|------|
| SDM10 激光测距 | TX=17, RX=18 | UART1 (460800bps) |
| HC-SR04 超声波 | Trig=4, Echo=5 | GPIO直连 |
| 蜂鸣器 | GPIO47 | LEDC PWM |
| RGB LED | GPIO48 | WS2812 |

### 摄像板（ESP32-S3-WROOM）
| 功能 | 引脚 |
|------|------|
| OV2640 DVP | GPIO4-18（标准DVP引脚组） |
| WiFi STA | 连接主控AP (SSID: BlindGuide_AP) |
| 静态IP | 192.168.4.10 |

## 通信架构 (v3.0)

| 链路 | 协议 | 数据格式 | 说明 |
|------|------|----------|------|
| 主控板 → 手机App | WiFi AP + TCP:8888 | JSON | 传感器数据(激光+超声波+camera_ip) |
| 手机App → 主控板 | WiFi AP + TCP:8888 | JSON | 控制命令(模式切换/校准/重启) |
| 摄像板 → 手机App | WiFi STA + HTTP:80 | MJPEG | /video 视频流, /capture 单帧 |
| SDM10 → 主控板 | UART1 (460800bps) | 私有协议 | 50Hz连续输出 |
| HC-SR04 → 主控板 | GPIO (Trig/Echo) | 脉冲宽度 | 20Hz轮询 |

### 传感器JSON格式（主控→手机）
```json
{
  "type": "sensor",
  "ts": 123456,
  "laser_front": 2.50,
  "ultrasonic": 1.80,
  "camera_ip": "192.168.4.10",
  "battery": -1,
  "mode": 2,
  "level": 0
}
```

## APP界面功能 (v3.0)

### 主界面 (MainFragment)
- 大字体/高对比度/大按钮 — 视障友好设计
- 连接状态/当前模式/风险等级显示
- 障碍物信息与避障建议
- 模式切换/紧急停止/语音控制按钮

### 开发者调试面板 (DebugFragment)
- **HC-SR04 雷达可视化** — 扇形雷达视图，显示障碍物距离/方位
- **SDM10 激光距离** — 数字显示前方障碍物精确距离
- **AI视觉检测框叠加** — 视频预览 + TFLite检测框实时叠加
- **原图直出** — HTTP MJPEG原始画面，无叠加
- **传感器原始数据** — 激光/超声波/camera_ip/时间戳
- **处理后数据** — 危险等级/最近距离/防抖/检测数量
- **网络统计** — TCP状态/HTTP摄像头帧率
- **运行日志** — 时间戳日志

## APP工作模式

| 模式 | 描述 | 能耗 |
|------|------|------|
| 传感器模式 | 仅处理HC-SR04+SDM10数据，危险时才播报 | 低 |
| 自动模式 | 语音指令引导 + 障碍物信息播报（类型+方位） | 中 |
| 风险播报模式 | 只播报风险等级：安全/注意/危险 | 中 |

## 快速开始

### 1. ESP32 固件编译

```bash
# 安装 Arduino IDE 和 ESP32 开发板支持
# 工具 → 开发板管理器 → 搜索 "esp32" → 安装

# 主控板:
# 1. 打开 ESP32_Main/ESP32_Main.ino
# 2. 开发板选择: ESP32S3 Dev Module
# 3. 设置: USB CDC On Boot=Enabled, Flash=16MB, PSRAM=OPI 8MB
# 4. 编译上传

# 摄像板:
# 1. 打开 ESP32_Camera/ESP32_Camera.ino
# 2. 同样配置开发板
# 3. 编译上传
```

### 2. Android APP 编译

```bash
# 使用 Android Studio 打开 Android_App/ 目录
# 等待 Gradle 同步完成
# 连接手机 → 编译运行

# 注意: 需要放入 TFLite 模型文件到
# app/src/main/assets/blind_guide_model.tflite
```

### 3. 启动流程

1. 主控板上电 → RGB蓝→蜂鸣器短响→绿灯(AP就绪)
2. 摄像板上电 → 自动连接主控AP(192.168.4.10)→启动HTTP服务
3. 手机连接WiFi "BlindGuide_AP" (密码12345678)
4. 打开App → 自动连接TCP → 获取camera_ip → 自动拉取视频流
5. 调试页(连续点标题5次): 查看雷达图/AI叠加/原图/传感器数据

## 调试接口

| 调试方式 | 说明 |
|----------|------|
| 串口调试 | Arduino IDE Serial Monitor（`#ifdef DEBUG` 开关） |
| APP调试页 | 主界面标题连续点击5次激活开发者模式 |
| Web配置页 | 手机浏览器访问 `http://192.168.4.1` (主控) |
| 摄像板Web | 浏览器访问 `http://192.168.4.10` (摄像板首页) |
| 摄像板状态 | `http://192.168.4.10/status` (JSON状态) |
| 单帧快照 | `http://192.168.4.10/capture` (JPEG图像) |

## 模型训练（可选）

```bash
# 安装 Python 依赖
pip install -r requirements.txt

# 使用 YOLOv8 训练障碍物检测模型
# 转换为 TFLite 格式
# 放入 APP assets 目录
```

## 版本历史

| 版本 | 日期 | 主要变化 |
|------|------|----------|
| v1.0 | 2024-07 | 初始版本: Rd-03D×2 + SPI图传 + UDP |
| v2.0 | 2024-07 | HC-SR04替代前雷达，删除后雷达 |
| **v3.0** | **2024-07** | **彻底移除Rd-03D/SPI，摄像板WiFi独立直传，手机HTTP拉流，HC-SR04雷达可视化** |

## 开源协议

本项目采用 [GNU General Public License v3](LICENSE) 开源协议。
