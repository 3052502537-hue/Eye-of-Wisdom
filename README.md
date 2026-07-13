# “智慧之眼”导盲头环 — 基于大模型的互动式导盲头环设计与实现

> 为视障人士设计的智能导盲头环，集成激光测距、毫米波雷达、AI视觉识别，通过语音引导用户安全出行。

## 项目简介

本项目是一个基于 ESP32 + Android 的智能导盲头环系统，通过多传感器融合（激光测距 + 毫米波雷达）和 AI 视觉识别（盲道/红绿灯/障碍物/路口/斑马线），为视障用户提供实时环境感知和语音避障引导。

## 系统架构

```
┌─────────────────────────────────────────────────────────┐
│                      手机 APP (Android)                   │
│  ┌──────────┐  ┌──────────┐  ┌──────────┐  ┌─────────┐ │
│  │ TCP接收   │  │ UDP接收   │  │ TFLite   │  │ TTS语音 │ │
│  │ 传感器JSON│  │ 图像JPEG  │  │ 视觉识别  │  │ 播报    │ │
│  └────┬─────┘  └────┬─────┘  └────┬─────┘  └────┬────┘ │
│       │              │              │              │      │
│       └──────────────┴──────────────┘              │      │
│                      │                             │      │
│              ┌───────┴───────┐                     │      │
│              │ 避障决策引擎   │────────────────────┘      │
│              └───────────────┘                            │
└──────────────────────┬──────────────────────────────────┘
                       │ WiFi (AP模式)
┌──────────────────────┴──────────────────────────────────┐
│              ESP32-S3-N16R8 主控板                       │
│  ┌─────────┐ ┌─────────┐ ┌─────────┐ ┌───────────────┐ │
│  │SDM10驱动│ │Rd-03D   │ │SPI主机  │ │WiFi AP+TCP/UDP│ │
│  │激光测距 │ │毫米波×2  │ │接收图像  │ │Web配置页面    │ │
│  └─────────┘ └─────────┘ └────┬────┘ └───────────────┘ │
│  ┌─────────┐ ┌─────────┐      │                        │
│  │蜂鸣器   │ │RGB LED  │      │ SPI                    │
│  │报警     │ │状态指示  │      │                        │
│  └─────────┘ └─────────┘      │                        │
└───────────────────────────────┼────────────────────────┘
                                │
┌───────────────────────────────┴────────────────────────┐
│              ESP32-S3-WROOM 摄像头板                    │
│  ┌─────────────────┐  ┌─────────────┐                  │
│  │ OV2640 摄像头    │  │ SPI从机     │                  │
│  │ VGA 640x480 JPEG │  │ 数据就绪中断│                  │
│  └─────────────────┘  └─────────────┘                  │
└─────────────────────────────────────────────────────────┘
```

## 目录结构

```
Eye-of-Wisdom/
├── ESP32_Main/                 # 主控板代码（ESP32-S3-N16R8）
│   ├── ESP32_Main.ino          # 主程序入口
│   ├── config.h                # 引脚定义和全局配置
│   ├── protocol.h              # 通信协议定义（JSON/UDP/SPI帧格式）
│   ├── sdm10_driver.h/.cpp     # SDM10 激光测距驱动（UART）
│   ├── rd03d_driver.h/.cpp     # Rd-03D 毫米波雷达驱动（前/后两路）
│   ├── spi_master_comm.h/.cpp  # SPI主机通信（接收摄像头JPEG）
│   ├── wifi_manager.h/.cpp     # WiFi AP + TCP/UDP服务器
│   ├── web_server.h/.cpp       # Web配置页面（调试/校准）
│   ├── alarm_manager.h/.cpp    # 蜂鸣器 + WS2812 RGB控制
│   └── task_manager.h/.cpp     # FreeRTOS 5任务调度
│
├── ESP32_Camera/               # 摄像头板代码（ESP32-S3-WROOM）
│   ├── ESP32_Camera.ino        # 主程序入口
│   ├── config.h                # 引脚定义和图像参数
│   ├── camera_driver.h/.cpp    # OV2640驱动（VGA + JPEG压缩）
│   └── spi_slave_comm.h/.cpp   # SPI从机通信 + 数据就绪信号
│
├── Android_App/                # 安卓APP代码（Java）
│   └── app/
│       ├── build.gradle        # Gradle配置（依赖管理）
│       └── src/main/
│           ├── AndroidManifest.xml
│           ├── java/com/smarteye/blindguide/
│           │   ├── MainActivity.java       # 主Activity + 底部导航
│           │   ├── ui/                     # 3个页面Fragment
│           │   │   ├── MainFragment.java   # 主界面（大字体/高对比度）
│           │   │   ├── SettingsFragment.java
│           │   │   └── DebugFragment.java  # 调试页面（隐藏入口）
│           │   ├── network/                # WiFi通信
│           │   │   ├── TCPClient.java      # TCP接收传感器JSON
│           │   │   ├── UDPReceiver.java    # UDP接收图像JPEG
│           │   │   └── Protocol.java       # 协议解析
│           │   ├── ai/
│           │   │   └── TFLiteClassifier.java  # TFLite视觉推理
│           │   ├── tts/
│           │   │   └── TTSManager.java     # 语音播报（3级优先级）
│           │   ├── voice/
│           │   │   └── VoiceControl.java   # 语音控制（可选）
│           │   ├── logic/
│           │   │   └── ObstacleAnalyzer.java  # 避障决策引擎
│           │   └── data/
│           │       └── AppConfig.java      # 全局配置
│           └── res/                        # 布局资源
│
├── 智慧之眼导盲头环/            # 项目规划文档
│   ├── 00_项目规划/
│   ├── 01_硬件设计/
│   ├── 02_软件设计/
│   ├── 03_代码实现/            # 早期ESP-IDF版本代码
│   ├── 04_测试验证/
│   └── 05_参考资料/            # 芯片手册/规格书
│
├── requirements.txt            # Python工具链依赖（模型训练用）
├── library_dependencies.txt    # Arduino库依赖清单
├── LICENSE                     # GPLv3 开源协议
├── .gitignore
└── README.md
```

## 硬件清单

| 组件 | 型号 | 数量 | 说明 |
|------|------|------|------|
| 主控板 | ESP32-S3-N16R8 | 1 | 16MB Flash, 8MB PSRAM, 系统主控 |
| 摄像头板 | ESP32-S3-WROOM | 1 | 连接OV2640摄像头 |
| 摄像头 | OV2640 | 1 | 200万像素, DVP接口 |
| 激光测距 | SDM10 | 1 | 前方, 22m, UART接口, ±1cm |
| 毫米波雷达 | Rd-03D 24G | 2 | 前/后各1, UART, 测距/测角/测速 |
| 蜂鸣器 | 有源蜂鸣器 | 1 | GPIO47, PWM控制 |
| RGB LED | WS2812（板载） | 1 | GPIO48, 状态指示 |
| 电池 | 18650锂电池 | 1 | 3.7V, PCB已分5V/3.3V两路 |

## 技术栈

### ESP32 固件
- **开发环境**: Arduino IDE
- **开发板**: ESP32S3 Dev Module
- **框架**: ESP32 Arduino Core + FreeRTOS
- **配置**: USB CDC On Boot=Enabled, Flash=16MB, PSRAM=OPI 8MB

### Android APP
- **语言**: Java
- **IDE**: Android Studio
- **目标SDK**: 33 / 最低SDK: 24
- **AI推理**: TensorFlow Lite
- **UI**: Material Design, 视障友好（大字体/高对比度/大按钮）

### 通信架构
| 链路 | 协议 | 数据格式 |
|------|------|----------|
| 摄像头板 → 主控板 | SPI（主从模式 + 数据就绪中断） | JPEG二进制帧 |
| 主控板 → 手机APP | WiFi AP + TCP | JSON（传感器数据） |
| 主控板 → 手机APP | WiFi AP + UDP | JPEG二进制（图像帧） |
| 传感器 → 主控板 | UART × 3 | 各传感器私有协议 |

## APP工作模式

| 模式 | 描述 | 能耗 |
|------|------|------|
| 传感器模式 | 仅处理传感器数据，危险时才播报 | 低 |
| 自动模式 | 语音指令引导 + 障碍物信息播报（类型+方位） | 中 |
| 风险播报模式 | 只播报风险等级：安全/注意/危险 | 中 |

## 引脚分配

### 主控板（ESP32-S3-N16R8）
| 功能 | 引脚 | 接口 |
|------|------|------|
| SDM10 激光测距 | TX=17, RX=18 | UART1 |
| 前方 Rd-03D 雷达 | TX=19, RX=20 | UART2 |
| 后方 Rd-03D 雷达 | TX=43, RX=44 | UART0（需开启USB CDC） |
| SPI MOSI | GPIO11 | SPI主机 |
| SPI MISO | GPIO13 | |
| SPI SCK | GPIO12 | |
| SPI CS | GPIO10 | |
| 数据就绪中断 | GPIO9 | 摄像头→主控 |
| 蜂鸣器 | GPIO47 | PWM |
| RGB LED | GPIO48 | WS2812 |

### 摄像头板（ESP32-S3-WROOM）
| 功能 | 引脚 |
|------|------|
| OV2640 DVP | GPIO4-18（标准DVP引脚组） |
| SPI从机 | MOSI=39, MISO=40, SCK=41, CS=42 |
| 数据就绪 | GPIO38 |

## 快速开始

### 1. ESP32 固件编译

```bash
# 安装 Arduino IDE 和 ESP32 开发板支持
# 工具 → 开发板管理器 → 搜索 "esp32" → 安装

# 按照库依赖清单安装库:
# 参见 library_dependencies.txt

# 主控板:
# 1. 打开 ESP32_Main/ESP32_Main.ino
# 2. 开发板选择: ESP32S3 Dev Module
# 3. 设置: USB CDC On Boot=Enabled, Flash=16MB, PSRAM=OPI 8MB
# 4. 编译上传

# 摄像头板:
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

### 3. 模型训练（可选）

```bash
# 安装 Python 依赖
pip install -r requirements.txt

# 使用 YOLOv8 训练障碍物检测模型
# 转换为 TFLite 格式
# 放入 APP assets 目录
```

## 调试接口

| 调试方式 | 说明 |
|----------|------|
| 串口调试 | Arduino IDE Serial Monitor（`#ifdef DEBUG` 开关） |
| APP调试页 | 主界面标题连续点击5次激活调试模式 |
| Web配置页 | 手机浏览器访问 `http://192.168.4.1` |

## 通信协议示例

### 传感器数据（TCP JSON）
```json
{
  "type": "sensor",
  "laser_front": 2.5,
  "radar_front": {"dist": 3.2, "speed": 0.5, "angle": 15},
  "radar_back": {"dist": 5.0, "speed": 2.5, "angle": -10},
  "battery": 85,
  "mode": 2
}
```

### 控制命令（TCP JSON）
```json
{"type": "cmd", "action": "set_mode", "value": 2}
```

### 图像数据（UDP二进制）
```
[帧号2B] [总包数2B] [当前包号2B] [数据长度2B] [JPEG数据...]
```

## 开源协议

本项目采用 [GNU General Public License v3](LICENSE) 开源协议。

## 项目状态

- [x] 代码框架搭建完成
- [x] 引脚分配方案确定
- [x] 通信协议设计完成
- [x] FreeRTOS任务架构设计
- [ ] 传感器驱动协议校对（SDM10 / Rd-03D）
- [ ] TFLite模型训练
- [ ] 硬件联调测试
- [ ] APP端功能完善
