@echo off
chcp 65001 >nul
title 摄像头板 - 一键编译烧录
echo =========================================
echo  智慧之眼导盲头环 - 摄像头板烧录工具
echo  硬件: ESP32-S3 WROOM + OV2640
echo =========================================
echo.

where pio >nul 2>nul
if %errorlevel% neq 0 (
    echo [错误] 未找到 PlatformIO CLI (pio)
    echo 请先安装 PlatformIO 并确保 pio 在 PATH 中
    echo.
    pause
    exit /b 1
)

echo [信息] 正在编译并烧录摄像头板固件...
echo.
pio run -t upload

if %errorlevel% equ 0 (
    echo.
    echo =========================================
    echo  [成功] 摄像头板固件烧录完成！
    echo =========================================
) else (
    echo.
    echo =========================================
    echo  [失败] 烧录失败，请检查：
    echo  1. 开发板是否正确连接
    echo  2. 串口号是否正确
    echo  3. 是否进入烧录模式（按住BOOT+按一下EN）
    echo =========================================
)

echo.
pause
