# 智慧之眼导盲头环 - 摄像头板 (firmware_cam)

> **硬件**: ESP32-S3 WROOM 开发板  
> **Flash**: 8MB QIO  
> **PSRAM**: 8MB (必须，用于图像缓冲)  
> **框架**: Arduino + ESP-IDF 混合  
> **摄像头**: OV2640 (200万像素，DVP接口)

---

## 一、功能职责

摄像头板是系统的"眼睛"，负责图像采集和高速传输：

| 模块 | 文件 | 功能说明 |
|------|------|---------|
| OV2640 驱动 | `drivers/ov2640_cam` | 摄像头初始化、图像采集、参数配置 |
| 图像预处理 | `image_proc/img_preprocess` | 缩放、裁剪、格式转换（预留） |
| SPI主机通信 | `comm/spi_master` | 分块发送图像数据到主控板 |
| 指令处理 | 主循环 | 接收主控板指令（调整分辨率/帧率） |
| 心跳保活 | 任务 | 每秒发送心跳，监控链路状态 |

---

## 二、硬件连接 (引脚分配)

### 2.1 OV2640 摄像头 (DVP 接口)
| OV2640 信号 | ESP32-S3 | 说明 |
|------------|----------|------|
| VCC | 3.3V | 3.3V 供电（注意：不要接5V！） |
| GND | GND | 共地 |
| D0 | GPIO 17 | 数据位 0 (LSB) |
| D1 | GPIO 16 | 数据位 1 |
| D2 | GPIO 15 | 数据位 2 |
| D3 | GPIO 14 | 数据位 3 |
| D4 | GPIO 13 | 数据位 4 |
| D5 | GPIO 12 | 数据位 5 |
| D6 | GPIO 11 | 数据位 6 |
| D7 | GPIO 10 | 数据位 7 (MSB) |
| PCLK | GPIO 9 | 像素时钟输入 |
| VSYNC | GPIO 7 | 帧同步 |
| HREF | GPIO 8 | 行同步 |
| SCL (SCCB) | GPIO 6 | 配置时钟 (I2C SCL) |
| SDA (SCCB) | GPIO 5 | 配置数据 (I2C SDA) |
| XCLK | GPIO 4 | 主时钟输出 (24MHz) |
| PWDN | GPIO 2 | 掉电控制 |
| RESET | GPIO 3 | 复位 |

> **重要**: 摄像头引脚占用 GPIO 2~17，共 16 个引脚。  
> SPI 通信使用 GPIO 35~38，与摄像头完全独立，无引脚冲突。

### 2.2 SPI 主机 (连接主控板)
| 信号 | ESP32-S3 | 说明 |
|------|----------|------|
| MOSI | GPIO 35 | 主机输出 → 主控板MOSI |
| MISO | GPIO 36 | 主控板MISO → 主机输入 |
| SCLK | GPIO 37 | SPI时钟（主机提供） |
| CS | GPIO 38 | 片选输出（低电平有效） |
| GND | GND | 共地（必须！） |

> **接线方法**: 两块板子相同名称的引脚直连即可：
> - 摄像头板 MOSI(35) → 主控板 MOSI(35)
> - 摄像头板 MISO(36) → 主控板 MISO(36)
> - 摄像头板 SCLK(37) → 主控板 SCLK(37)
> - 摄像头板 CS(38) → 主控板 CS(38)
> - 摄像头板 GND ↔ 主控板 GND （必须连接！）

### 2.3 状态 LED
| LED | ESP32-S3 | 说明 |
|-----|----------|------|
| 状态LED | GPIO 48 | 运行状态指示 |

---

## 三、FreeRTOS 任务分配

| 任务名 | 核心 | 优先级 | 栈大小 | 周期 | 职责 |
|-------|------|--------|--------|------|------|
| cam_capture | Core1 | 6 | 8KB | ~15fps | 摄像头图像采集 |
| img_preprocess | Core1 | 5 | 4KB | 事件驱动 | 图像预处理（缩放/裁剪） |
| spi_send | Core0 | 6 | 4KB | 事件驱动 | SPI分块发送图像 |
| comm_handler | Core0 | 4 | 2KB | 事件驱动 | 处理主控板下发指令 |
| heartbeat | Core0 | 2 | 1KB | 1秒 | 心跳保活 |

---

## 四、编译与烧录

### 方法一：VS Code + PlatformIO (推荐)

1. 安装 VS Code 中的 PlatformIO 插件
2. 打开 `firmware_cam` 文件夹（注意：必须打开项目根目录，不是上一级）
3. 等待 PlatformIO 自动安装依赖（首次较慢）
4. 点击底部状态栏的 **→ Upload** 按钮烧录
5. 点击 **插头图标 (Serial Monitor)** 查看串口输出

### 方法二：命令行 (PlatformIO CLI)

```bash
# 进入项目目录
cd firmware_cam

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

编译产物在 `.pio/build/esp32-s3-wroom/` 目录下：
- `bootloader.bin` → 偏移 0x1000
- `partitions.bin` → 偏移 0x8000
- `firmware.bin` → 偏移 0x10000

烧录命令：
```bash
esptool.py ^
  --chip esp32s3 ^
  --port COM4 ^
  --baud 921600 ^
  write_flash -z ^
  0x1000 bootloader.bin ^
  0x8000 partitions.bin ^
  0x10000 firmware.bin
```

> 将 COM4 替换为实际串口号。两块板子的串口号不同，注意区分。

---

## 五、SPI 通信协议

### 5.1 帧格式

```
┌────────┬──────┬────────┬────────┬──────────┬────────┐
│ 帧头   │ 命令 │ 数据长度│ 数据   │ 校验和   │ 帧尾   │
│ 2字节  │ 1字节│ 2字节  │ N字节  │ 1字节    │ 2字节  │
├────────┼──────┼────────┼────────┼──────────┼────────┤
│ 0xAA55 │ cmd  │ 大端   │ 变长   │ CRC8     │ 0x55AA │
└────────┴──────┴────────┴────────┴──────────┴────────┘
```

### 5.2 命令字定义

| 命令字 | 名称 | 方向 | 说明 |
|-------|------|------|------|
| 0x01 | IMG_FRAME_START | 摄像头→主控 | 图像帧开始：宽+高+格式+总大小 |
| 0x02 | IMG_FRAME_DATA | 摄像头→主控 | 图像数据块：块序号+数据 |
| 0x03 | IMG_FRAME_END | 摄像头→主控 | 图像帧结束：总块数 |
| 0x10 | CMD_SET_RESOLUTION | 主控→摄像头 | 设置分辨率 |
| 0x11 | CMD_SET_FPS | 主控→摄像头 | 设置帧率 |
| 0x20 | ACK | 双向 | 确认应答 |
| 0x30 | HEARTBEAT | 双向 | 心跳保活 |

### 5.3 图像传输流程

```
摄像头板                          主控板
   │                                │
   │── IMG_FRAME_START ──────────► │  准备接收缓冲区
   │                                │
   │── IMG_FRAME_DATA (块0) ──────►│
   │── IMG_FRAME_DATA (块1) ──────►│
   │── ...                         │
   │── IMG_FRAME_DATA (块N) ──────►│
   │                                │
   │── IMG_FRAME_END ─────────────►│  校验完整性，触发视觉处理
   │                                │
   │◄────── ACK ───────────────────│
```

---

## 六、代码结构

```
firmware_cam/
├── platformio.ini          # 项目配置
├── include/
│   ├── config.h            # 引脚定义 + 参数（可调）
│   └── comm_protocol.h     # SPI通信协议（与主控板共享，必须一致！）
└── src/
    ├── main.cpp            # 程序入口：初始化 + 任务创建
    ├── drivers/
    │   └── ov2640_cam.h/.cpp   # OV2640摄像头驱动
    ├── comm/
    │   └── spi_master.h/.cpp   # SPI主机通信
    └── image_proc/
        └── img_preprocess.h/.cpp  # 图像预处理（预留）
```

---

## 七、烧录顺序与注意事项

### 7.1 烧录顺序
1. **先烧录摄像头板** → 确保能正常输出图像
2. **再烧录主控板** → 接收图像并处理

### 7.2 两块板子区分
- 主控板固件: `firmware_main` → 接雷达 + 蜂鸣器 + 振动马达
- 摄像头板固件: `firmware_cam` → 接 OV2640 摄像头

### 7.3 常见问题
| 问题 | 可能原因 | 解决方法 |
|------|---------|---------|
| 摄像头初始化失败 | 接线错误 / 供电不足 | 检查接线，确保3.3V供电稳定 |
| SPI 通信不通 | 引脚接反 / 共地不良 | 检查 MOSI/MISO 是否接反，GND 是否共地 |
| 图像花屏 / 丢块 | SPI时钟太快 / 线太长 | 降低 SPI 时钟（config.h 中调整） |
| 内存不足 | PSRAM 未启用 | 确认开发板带 PSRAM，platformio.ini 中有 BOARD_HAS_PSRAM |

---

## 八、后续开发要点

1. **SPI 引脚冲突**: 当前 SPI 引脚与摄像头引脚可能有复用，需根据实际硬件在 `config.h` 中调整
2. **SPI 速度调优**: 默认 10MHz，可根据实际连线质量调整
3. **图像预处理**: `img_preprocess.cpp` 是预留模块，需要时可添加缩放/裁剪
4. **JPEG 模式**: 如需更高帧率，可考虑 JPEG 模式传输，主控端解码
