@echo off
chcp 65001 >nul
title 固定目标 Wi-Fi - Locke
setlocal enabledelayedexpansion

net session >nul 2>&1
if errorlevel 1 (
    echo 需要管理员权限，正在提权重启...
    powershell -NoProfile -Command "Start-Process -FilePath '%~f0' -ArgumentList '%*' -Verb RunAs"
    exit /b
)

echo ============================================
echo   固定目标 Wi-Fi：目标自动连、其它手动连
echo ============================================

REM 接口名：优先用第 2 个参数（由外部传入），否则自动探测
set "WIFI=%~2"
if not defined WIFI (
    powershell -NoProfile -Command "Get-NetAdapter -Physical | Where-Object { $_.PhysicalMediaType -like '*802.11*' -and $_.Status -eq 'Up' } | Select-Object -First 1 -ExpandProperty Name" > "%TEMP%\locke_wifi_if.txt" 2>nul
    set /p WIFI=<"%TEMP%\locke_wifi_if.txt"
)
if not defined WIFI (
    echo 未检测到已连接的无线网卡。
    echo 可手动指定接口名，例如:  锁定WiFi.cmd silent "WLAN"
    if /i not "%~1"=="silent" pause
    exit /b 1
)

REM 目标热点固定为 TS_F1_8825：换一台电脑或当前连在别的网络时也不会定错
set "TARGET=TS_F1_8825"
REM 可选：第 3 个参数覆盖目标 SSID
if not "%~3"=="" set "TARGET=%~3"

if /i not "%~1"=="silent" (
    echo.
    echo 无线接口: %WIFI%
    echo 目标热点: %TARGET%
    netsh wlan show interfaces
    echo.
)

REM 目标配置必须已经存在（首次使用需先手动连接一次并保存密码）
netsh wlan show profiles name="%TARGET%" >nul 2>&1
if errorlevel 1 (
    echo 未找到 "%TARGET%" 的 Wi-Fi 配置。
    echo 请先手动连接一次 "%TARGET%"（输入密码并保存），再运行本脚本。
    if /i not "%~1"=="silent" pause
    exit /b 1
)

echo [1/3] 保持 WLAN 自动配置开启（掉线后可自动重连）...
netsh wlan set autoconfig enabled=yes interface="%WIFI%"
if errorlevel 1 (
    echo    失败：请确认无线网卡已启用，WLAN AutoConfig 服务正在运行。
) else (
    echo    完成：已保持自动配置开启，掉线后 Windows 会自动重连。
)

echo.
echo [2/3] 固定目标热点：%TARGET% 自动连接，其它 Wi-Fi 改为手动...
set "COUNT=0"
for /f "tokens=1,* delims=:" %%A in ('netsh wlan show profiles ^| findstr /r /c:"^[ ]*All User Profile" /c:"^[ ]*所有用户配置文件"') do (
    set "PROFILE=%%B"
    for /f "tokens=* delims= " %%C in ("!PROFILE!") do set "PROFILE=%%C"
    if /i "!PROFILE!"=="!TARGET!" (
        netsh wlan set profileparameter name="!PROFILE!" connectionmode=auto >nul 2>&1
    ) else (
        netsh wlan set profileparameter name="!PROFILE!" connectionmode=manual >nul 2>&1
        set /a COUNT+=1
    )
)
echo    完成：目标热点 "%TARGET%" 自动连接，其它 !COUNT! 个配置已改为手动。

echo.
echo [3/3] 无线网卡电源模式设为最高性能...
powercfg /setacvalueindex SCHEME_CURRENT 19cbb8fa-5279-450e-9fac-8a3d5fedd0c1 12bbebe6-58d6-4636-95bb-3217ef867c1a 0 >nul
powercfg /setdcvalueindex SCHEME_CURRENT 19cbb8fa-5279-450e-9fac-8a3d5fedd0c1 12bbebe6-58d6-4636-95bb-3217ef867c1a 0 >nul
powercfg /setactive SCHEME_CURRENT >nul
echo    完成。

echo.
echo 说明：
echo   1. 不再禁用 WLAN 自动配置，掉线后 Windows 会自动重连。
echo   2. 只把当前热点设为“自动连接”，其它保存的 Wi-Fi 改为“手动连接”，
echo      因此不会自动跳到别的网络；需要时可在系统里手动连其它网络。
echo   3. 以后新增了别的 Wi-Fi，想让它们保持手动，重新运行本脚本即可。
echo   4. 想恢复所有网络自动连接，在 Windows 的“管理已知网络”里逐个改回。
echo.
if /i "%~1"=="silent" exit /b 0
pause
