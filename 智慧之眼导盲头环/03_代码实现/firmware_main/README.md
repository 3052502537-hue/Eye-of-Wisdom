# 智慧之眼导盲头环 - 主控板 (firmware_main)

> **硬件**: ESP32-S3 N16R8 开发板  
> **Flash**: 16MB QIO  
> **PSRAM**: 8MB (必须)  
> **框架**: Arduino + ESP-IDF 混合  

---

## 一、功能职责

主控板是整个系统的"大脑"，负责所有感知融合和决策输出：

| 模块 | 文件 | 功能说明 |
|------|------|---------|
| 前雷达驱动 | `drivers/rd03d` | UART读取前方 RD03D 毫米波雷达数据 |
| 后雷达驱动 | `drivers/rd03d` | UART读取后方 RD03D 毫米波雷达数据 |
| 前雷达处理 | `perception/radar_front` | 目标分区、距离分级、防抖 |
| 后雷达处理 | `perception/radar_rear` | 后方接近速度检测、快速接近预警 |
| 盲道检测 | `perception/lane_detection` | HSV颜色分割 + 边缘检测 + 霍夫直线 |
| 红绿灯识别 | `perception/traffic_light` | 颜色分割 + 圆形检测 + 状态判断 |
| 斑马线识别 | `perception/crosswalk` | 边缘检测 + 平行条纹计数 |
| SPI从机通信 | `comm/spi_slave` | 接收摄像头板传输的图像数据 |
| 预警决策 | `services/warning_service` | 多传感器融合、优先级仲裁、预警输出 |
| 状态管理 | `services/state_manager` | 系统状态机（INIT→STANDBY→WALKING→WARNING） |
| 蜂鸣器 | `drivers/buzzer` | PWM 音调控制，不同频率代表不同危险等级 |
| 振动马达 | `drivers/vibrator` | 4路方向振动，指示障碍物方向 |
| 调试日志 | `services/debug_log` | 串口命令交互，实时查看各模块状态 |

---

## 二、硬件连接 (引脚分配)

### 2.1 前雷达 RD03D (UART1)
| RD03D | ESP32-S3 | 说明 |
|-------|----------|------|
| VCC | 5V / VBUS | 5V 供电 |
| GND | GND | 共地 |
| TX | GPIO 17 | 雷达发送 → ESP32 接收 |
| RX | GPIO 18 | ESP32 发送 → 雷达接收 |

### 2.2 后雷达 RD03D (UART2)
| RD03D | ESP32-S3 | 说明 |
|-------|----------|------|
| VCC | 5V / VBUS | 5V 供电 |
| GND | GND | 共地 |
| TX | GPIO 19 | 雷达发送 → ESP32 接收 |
| RX | GPIO 20 | ESP32 发送 → 雷达接收 |

### 2.3 SPI 从机 (连接摄像头板)
| 信号 | ESP32-S3 | 说明 |
|------|----------|------|
| MOSI | GPIO 35 | 摄像头板MOSI → 主控板MOSI（输入） |
| MISO | GPIO 36 | 主控板MISO → 摄像头板MISO（输出） |
| SCLK | GPIO 37 | SPI时钟（输入，由摄像头板提供） |
| CS | GPIO 38 | 片选（输入，低电平有效） |
| GND | GND | 共地（必须！） |

> **接线方法**: 两块板子相同名称的引脚直连即可：
> - 摄像头板 MOSI(35) → 主控板 MOSI(35)
> - 摄像头板 MISO(36) → 主控板 MISO(36)
> - 摄像头板 SCLK(37) → 主控板 SCLK(37)
> - 摄像头板 CS(38) → 主控板 CS(38)
> - 摄像头板 GND ↔ 主控板 GND （必须连接！）

### 2.4 蜂鸣器 + 振动马达 + LED
| 模块 | ESP32-S3 | 说明 |
|------|----------|------|
| 蜂鸣器 PWM | GPIO 47 | 无源蜂鸣器正极 |
| 前振动马达 | GPIO 42 | NPN三极管驱动 |
| 后振动马达 | GPIO 43 | NPN三极管驱动 |
| 左振动马达 | GPIO 44 | NPN三极管驱动 |
| 右振动马达 | GPIO 45 | NPN三极管驱动 |
| 状态LED | GPIO 48 | 运行状态指示 |

> **引脚使用情况总结**（确保无冲突）:
> - 雷达: GPIO 17,18,19,20
> - SPI:  GPIO 35,36,37,38
> - 振动马达: GPIO 42,43,44,45
> - 蜂鸣器: GPIO 47
> - LED:  GPIO 48
> 所有模块引脚完全独立，无复用冲突。

---

## 三、FreeRTOS 任务分配

| 任务名 | 核心 | 优先级 | 栈大小 | 周期 | 职责 |
|-------|------|--------|--------|------|------|
| radar_front | Core1 | 5 | 2KB | 50ms | 前雷达数据读取解析 |
| radar_rear | Core1 | 5 | 2KB | 50ms | 后雷达数据读取解析 |
| spi_receive | Core1 | 6 | 4KB | 事件驱动 | SPI图像接收 |
| vision_process | Core1 | 4 | 16KB | ~33fps | 三个视觉算法串行处理 |
| warning_decision | Core0 | 6 | 4KB | 100ms | 融合决策、优先级仲裁 |
| feedback_output | Core0 | 5 | 2KB | 事件驱动 | 蜂鸣器+振动马达输出 |
| state_manager | Core0 | 3 | 2KB | 500ms | 状态机管理 |
| debug_log | Core0 | 2 | 4KB | 100ms | 串口命令处理 |

---

## 四、编译与烧录

### 方法一：VS Code + PlatformIO (推荐)

1. 安装 VS Code 中的 PlatformIO 插件
2. 打开 `firmware_main` 文件夹（注意：必须打开项目根目录，不是上一级）
3. 等待 PlatformIO 自动安装依赖（首次较慢）
4. 点击底部状态栏的 **→ Upload** 按钮烧录
5. 点击 **插头图标 (Serial Monitor)** 查看串口输出

### 方法二：命令行 (PlatformIO CLI)

```bash
# 进入项目目录
cd firmware_main

# 编译（不烧录）
pio run

# 编译并烧录
pio run -t upload

# 查看串口监视器
pio device monitor -b 115200

# 清理编译产物
pio run -t clean
```

### 方法三：esptool.py 烧录 bin 文件

先编译生成 bin 文件：
```bash
pio run
```

编译产物在 `.pio/build/esp32-s3-n16r8/` 目录下：
- `bootloader.bin` → 偏移 0x1000
- `partitions.bin` → 偏移 0x8000
- `firmware.bin` → 偏移 0x10000

烧录命令：
```bash
esptool.py ^
  --chip esp32s3 ^
  --port COM3 ^
  --baud 921600 ^
  write_flash -z ^
  0x1000 bootloader.bin ^
  0x8000 partitions.bin ^
  0x10000 firmware.bin
```

> 将 COM3 替换为实际串口号。Windows 下在设备管理器中查看。

---

## 五、调试命令

串口监视器中输入以下命令（115200 波特率）：

| 命令 | 功能 |
|------|------|
| `help` | 显示所有可用命令 |
| `radar` | 打印前后雷达目标数据 |
| `lane` | 打印盲道检测结果 |
| `light` | 打印红绿灯识别结果 |
| `warn` | 打印预警服务状态 |
| `state` | 打印系统状态 |
| `fps` | 显示帧率统计 |
| `buzzer 1000` | 测试蜂鸣器（1000Hz） |
| `vib front` | 测试前方振动马达 |
| `vib rear` | 测试后方振动马达 |
| `vib left` | 测试左侧振动马达 |
| `vib right` | 测试右侧振动马达 |

---

## 六、预警策略说明

### 6.1 优先级（从高到低）
1. 前方雷达危险（距离 < 1米）→ DANGER
2. 后方快速接近（速度 > 1.5m/s 且距离 < 5米）→ DANGER
3. 盲道丢失/严重偏离 → WARNING
4. 红灯检测 → WARNING
5. 斑马线提示 → ATTENTION

### 6.2 距离分级
| 距离 | 等级 | 蜂鸣器 | 振动 |
|------|------|--------|------|
| > 3m | SAFE | 无 | 无 |
| 2~3m | ATTENTION | 1kHz 间断 | 轻振动 |
| 1~2m | WARNING | 1.5kHz 较快间断 | 中振动 |
| < 1m | DANGER | 2kHz 急促连续 | 强振动 |

### 6.3 防抖机制
所有检测结果需连续 3 帧确认才触发预警，避免单帧误报。

---

## 七、代码结构

```
firmware_main/
├── platformio.ini          # 项目配置
├── include/
│   ├── config.h            # 引脚定义 + 阈值参数（可调）
│   ├── types.h             # 全局数据结构
│   └── comm_protocol.h     # SPI通信协议（与摄像头板共享）
└── src/
    ├── main.cpp            # 程序入口：初始化 + 任务创建
    ├── drivers/            # 硬件驱动层
    │   ├── rd03d.h/.cpp    # 毫米波雷达驱动
    │   ├── buzzer.h/.cpp   # 蜂鸣器驱动
    │   ├── vibrator.h/.cpp # 振动马达驱动
    │   └── status_led.h/.cpp
    ├── perception/         # 感知算法层
    │   ├── radar_front.h/.cpp
    │   ├── radar_rear.h/.cpp
    │   ├── lane_detection.h/.cpp   # ← 需填充算法
    │   ├── traffic_light.h/.cpp    # ← 需填充算法
    │   └── crosswalk.h/.cpp        # ← 需填充算法
    ├── comm/               # 通信层
    │   └── spi_slave.h/.cpp
    └── services/           # 服务层
        ├── warning_service.h/.cpp
        ├── state_manager.h/.cpp
        └── debug_log.h/.cpp
```

---

## 八、后续开发要点

1. **雷达协议解析**: `rd03d.cpp` 中 `parseFrame()` 函数需根据 RD03D 数据手册完善
2. **视觉算法填充**: 三个视觉检测模块的 `detect()` 函数当前是占位，需逐步实现
3. **引脚调整**: 根据实际开发板修改 `include/config.h` 中的引脚定义
4. **参数调优**: 预警阈值、防抖帧数等可在 `config.h` 中调整
