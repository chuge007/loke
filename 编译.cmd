@echo off
chcp 65001 >nul
setlocal
REM ============================================================
REM  一键编译脚本 - Locke
REM  项目文件    : Locke.pro
REM  编译工具链  : MSVC2017_64 (x64) + Qt 5.12.4 + jom/nmake
REM  EXE 输出位置: D:\dev\Locke\Locke\release\Locke.exe
REM  说明: exe 默认输出到 pro 文件所在目录下的 release 子目录
REM ============================================================

REM ---- 1. 进入项目目录（脚本所在目录即 pro 所在目录）----
cd /d "%~dp0"

REM ---- 2. 加载 MSVC x64 编译环境 ----
call "C:\Program Files (x86)\Microsoft Visual Studio\2017\Enterprise\VC\Auxiliary\Build\vcvarsall.bat" x64
if errorlevel 1 (
    echo [错误] 加载 VS2017 编译环境失败，请检查 Visual Studio 2017 是否安装。
    pause
    exit /b 1
)

REM ---- 3. 将 Qt 的 qmake 加入 PATH ----
set "PATH=D:\qt\5.12.4\msvc2017_64\bin;%PATH%"

REM ---- 4. qmake 生成 Makefile（release 配置）----
qmake "Locke.pro" -spec win32-msvc "CONFIG+=release"
if errorlevel 1 (
    echo [错误] qmake 执行失败，请检查 pro 文件与 Qt 环境。
    pause
    exit /b 1
)

REM ---- 5. 编译（优先 jom，无 jom 则用 nmake）----
where jom >nul 2>nul
if not errorlevel 1 (
    jom
) else (
    nmake
)
if errorlevel 1 (
    echo [错误] 编译失败，请查看上方错误信息。
    pause
    exit /b 1
)

REM ---- 6. 编译完成，提示 exe 位置 ----
echo.
echo ============================================================
echo  编译成功！
echo  EXE 输出位置:
echo    D:\dev\Locke\Locke\release\Locke.exe
echo  （若 .pro 中自定义了 DESTDIR，则以实际输出为准）
echo ============================================================
pause
exit /b 0
