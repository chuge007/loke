@echo off
chcp 65001 >nul
title 恢复 Wi-Fi 自动切换 - Locke
setlocal

net session >nul 2>&1
if errorlevel 1 (
    echo 需要管理员权限，正在提权重启...
    powershell -NoProfile -Command "Start-Process -FilePath '%~f0' -ArgumentList '%*' -Verb RunAs"
    exit /b
)

REM 接口名：优先用第 2 个参数，否则自动探测
set "WIFI=%~2"
if not defined WIFI (
    powershell -NoProfile -Command "Get-NetAdapter -Physical | Where-Object { $_.PhysicalMediaType -like '*802.11*' -and $_.Status -eq 'Up' } | Select-Object -First 1 -ExpandProperty Name" > "%TEMP%\locke_wifi_if.txt" 2>nul
    set /p WIFI=<"%TEMP%\locke_wifi_if.txt"
)
if not defined WIFI (
    echo 未检测到已连接的无线网卡。
    echo 可手动指定接口名，例如:  恢复WiFi.cmd silent "WLAN"
    if /i not "%~1"=="silent" pause
    exit /b 1
)

echo [1/2] 恢复 WLAN 自动配置...
netsh wlan set autoconfig enabled=yes interface="%WIFI%"
if errorlevel 1 (
    echo    失败：请检查无线网卡状态。
) else (
    echo    完成：已恢复自动扫描与自动连接。
)

echo.
echo [2/2] 无线网卡电源模式恢复为中等节能...
powercfg /setacvalueindex SCHEME_CURRENT 19cbb8fa-5279-450e-9fac-8a3d5fedd0c1 12bbebe6-58d6-4636-95bb-3217ef867c1a 2 >nul
powercfg /setdcvalueindex SCHEME_CURRENT 19cbb8fa-5279-450e-9fac-8a3d5fedd0c1 12bbebe6-58d6-4636-95bb-3217ef867c1a 2 >nul
powercfg /setactive SCHEME_CURRENT >nul
echo    完成。

echo.
if /i "%~1"=="silent" exit /b 0
pause
