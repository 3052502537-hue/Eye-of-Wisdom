# 智慧之眼导盲头环 - 代码实现总览

> **项目名称**: "智慧之眼"导盲头环——基于大模型的互动式导盲头环设计与实现  
> **架构**: 双 ESP32-S3 + SPI 高速通信 + 传统CV视觉算法  
> **开发框架**: Arduino + ESP-IDF 混合 (PlatformIO)  
> **版本**: v1.0

---

## 📦 项目结构

```
03_代码实现/
├── firmware_main/              # ===== 主控板固件 =====
│   ├── platformio.ini          # 项目配置
│   ├── README.md               # 详细说明文档（必看）
│   ├── include/
│   │   ├── config.h            # 引脚定义 + 所有可调参数
│   │   ├── types.h             # 全局数据结构
│   │   └── comm_protocol.h     # SPI通信协议（与摄像头板共享）
│   └── src/
│       ├── main.cpp            # 程序入口
│       ├── drivers/            # 硬件驱动层
│       │   ├── rd03d.h/.cpp    # RD03D毫米波雷达驱动
│       │   ├── buzzer.h/.cpp   # 蜂鸣器PWM驱动
│       │   ├── vibrator.h/.cpp # 4路振动马达驱动
│       │   └── status_led.h/.cpp
│       ├── perception/         # 感知算法层
│       │   ├── radar_front.h/.cpp    # 前雷达数据处理
│       │   ├── radar_rear.h/.cpp     # 后雷达数据处理
│       │   ├── lane_detection.h/.cpp # 盲道检测（待填充算法）
│       │   ├── traffic_light.h/.cpp  # 红绿灯识别（待填充算法）
│       │   └── crosswalk.h/.cpp      # 斑马线识别（待填充算法）
│       ├── comm/               # 通信层
│       │   └── spi_slave.h/.cpp     # SPI从机（接收图像）
│       └── services/           # 服务层
│           ├── warning_service.h/.cpp  # 预警决策引擎
│           ├── state_manager.h/.cpp    # 系统状态机
│           └── debug_log.h/.cpp        # 调试日志（串口命令）
│
└── firmware_cam/               # ===== 摄像头板固件 =====
    ├── platformio.ini          # 项目配置
    ├── README.md               # 详细说明文档（必看）
    ├── include/
    │   ├── config.h            # 引脚定义 + 参数
    │   └── comm_protocol.h     # SPI通信协议（必须与主控板一致）
    └── src/
        ├── main.cpp            # 程序入口
        ├── drivers/
        │   └── ov2640_cam.h/.cpp   # OV2640摄像头驱动
        ├── comm/
        │   └── spi_master.h/.cpp    # SPI主机（发送图像）
        └── image_proc/
            └── img_preprocess.h/.cpp # 图像预处理（预留）
```

---

## 🎯 双板分工总览

| 项目 | 硬件 | 核心职责 | 对外接口 |
|------|------|---------|---------|
| **firmware_main** | ESP32-S3 N16R8 | 雷达数据解析、视觉算法、融合决策、反馈输出 | 2×UART(雷达) + SPI从机 + 蜂鸣器 + 4×振动马达 |
| **firmware_cam** | ESP32-S3 WROOM | 摄像头采集、图像预处理、SPI图像传输 | DVP(摄像头) + SPI主机 |

---

## 🚀 快速上手（3步烧录）

### 第一步：准备环境
1. 安装 **VS Code**
2. 安装 **PlatformIO IDE** 插件
3. 准备两块 ESP32-S3 开发板和两根 USB 线

### 第二步：烧录摄像头板（先烧这块）
```bash
cd firmware_cam
pio run -t upload     # 编译并烧录
pio device monitor    # 查看串口输出（115200波特率）
```
详细说明见 [firmware_cam/README.md](./firmware_cam/README.md)

### 第三步：烧录主控板（后烧这块）
```bash
cd firmware_main
pio run -t upload     # 编译并烧录
pio device monitor    # 查看串口输出（115200波特率）
```
详细说明见 [firmware_main/README.md](./firmware_main/README.md)

---

## 🔌 两块板子连接方式（SPI）

| 信号 | 主控板 (SPI从机) | 摄像头板 (SPI主机) | 方向 | 说明 |
|------|----------------|-------------------|------|------|
| MOSI | GPIO 35 | GPIO 35 | 摄像头→主控 | 主机发，从机收 |
| MISO | GPIO 36 | GPIO 36 | 主控→摄像头 | 从机发，主机收 |
| SCLK | GPIO 37 | GPIO 37 | 摄像头→主控 | 时钟（主机提供） |
| CS | GPIO 38 | GPIO 38 | 摄像头→主控 | 片选（低电平有效） |
| GND | GND | GND | - | **必须共地！** |

> ⚠️ **重要提醒**:
> 1. 两块板子的 GND 必须连在一起，否则 SPI 通信会异常甚至烧芯片
> 2. 相同名称的引脚直连即可（MOSI接MOSI，MISO接MISO）
> 3. 如果 SPI 通信不稳定，可在 `config.h` 中降低 `SPI_CLOCK_SPEED`

---

## 🔧 各模块开发进度

| 模块 | 状态 | 说明 |
|------|------|------|
| RD03D雷达驱动 | ⚠️ 框架完成 | 协议解析需根据数据手册完善 |
| 蜂鸣器驱动 | ✅ 完成 | PWM音调控制 |
| 振动马达驱动 | ✅ 完成 | 4路方向振动 |
| 前雷达数据处理 | ✅ 完成 | 分区、分级、防抖 |
| 后雷达数据处理 | ✅ 完成 | 接近速度检测 |
| 盲道检测 | ⚠️ 接口预留 | 算法需填充 |
| 红绿灯识别 | ⚠️ 接口预留 | 算法需填充 |
| 斑马线识别 | ⚠️ 接口预留 | 算法需填充 |
| SPI从机通信 | ⚠️ 框架完成 | 需实际联调 |
| SPI主机通信 | ⚠️ 框架完成 | 需实际联调 |
| OV2640摄像头驱动 | ⚠️ 框架完成 | 基于esp_camera库 |
| 预警决策服务 | ✅ 完成 | 优先级仲裁+防抖+队列 |
| 状态管理 | ✅ 完成 | 五态状态机 |
| 调试日志 | ✅ 完成 | 串口命令交互 |

---

## 📝 开发建议顺序

1. **第一阶段：驱动验证**
   - 测试蜂鸣器、振动马达、LED
   - 测试雷达数据读取（先验证一块）
   - 测试摄像头采集

2. **第二阶段：通信联调**
   - SPI 通信验证（先发简单数据包）
   - 图像传输验证（分块传输+CRC校验）

3. **第三阶段：算法开发**
   - 盲道检测算法（传统CV：HSV+边缘+霍夫直线）
   - 红绿灯识别算法
   - 斑马线识别算法

4. **第四阶段：系统集成**
   - 多传感器融合调试
   - 预警策略调优
   - 整体功能测试

---

## 💡 调试技巧

- 两个项目都可以用串口命令 `help` 查看可用调试命令
- 先用 `buzzer 1000` 和 `vib front` 验证基础硬件
- 用 `radar` 命令查看雷达原始数据
- 所有阈值参数都在 `include/config.h` 中，方便调优

---

## 📚 相关文档

- 项目规划书: [../00_项目规划/项目规划书.md](../00_项目规划/项目规划书.md)
- 软件架构设计: [../02_软件设计/架构设计_双ESP32SPI通信.md](../02_软件设计/架构设计_双ESP32SPI通信.md)
