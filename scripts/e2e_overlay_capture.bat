@echo off
REM scripts\e2e_overlay_capture.bat
REM 本地端到端流程测试 Windows cmd 版本。逻辑与 .sh 一致：
REM 启动 GUI -> 后台连拍 -> 模拟快捷键触发 overlay -> 模拟鼠标拖拽
REM -> ESC 取消。产物是一串 PNG，尺寸分段可观察 overlay 是否被触发。
REM
REM 前置：在 build\ 下已跑过 cmake --build . --config Release
REM 限制：必须本机（有真实桌面会话）执行；headless CI 无法接收 SendInput。
REM 干扰：运行期间 overlay 会短暂显示，别动鼠标键盘。
REM
REM 用法：
REM   scripts\e2e_overlay_capture.bat
REM   scripts\e2e_overlay_capture.bat D:\path\to\out

setlocal enabledelayedexpansion

set "ROOT=%~dp0.."
pushd "%ROOT%" >nul
set "ROOT=%CD%"
popd >nul

set "GUI=%ROOT%\build\Release\AdvancedPaste.exe"
set "CLI=%ROOT%\build\Release\AdvancedPasteCli.exe"

if "%~1"=="" (
    set "OUT=%ROOT%\build\Release\e2e_out"
) else (
    set "OUT=%~1"
)

set LOOP_COUNT=15
set LOOP_INTERVAL=250

REM ── 1. 检查构建产物 ────────────────────────────────────────
if not exist "%GUI%" goto :nobuild
if not exist "%CLI%" goto :nobuild

REM ── 2. 清理 + 建立输出目录 ─────────────────────────────────
if exist "%OUT%" rd /s /q "%OUT%"
mkdir "%OUT%"

REM ── 3. 确保 GUI 在跑 ───────────────────────────────────────
tasklist /FI "IMAGENAME eq AdvancedPaste.exe" 2>nul | find /i "AdvancedPaste.exe" >nul
if errorlevel 1 (
    echo [启动] GUI 主程序...
    start "" "%GUI%"
    powershell -Command "Start-Sleep -Milliseconds 1500" >nul
) else (
    echo [复用] 已运行中的 GUI 主程序
)

REM ── 4. 后台连拍（start /b 不开新窗口）─────────────────────
echo [连拍] %LOOP_COUNT% 帧 × %LOOP_INTERVAL%ms -^> %OUT%
start /b "" "%CLI%" capture --monitor 1 --loop %LOOP_COUNT% --interval %LOOP_INTERVAL% --out-dir "%OUT%" --out-prefix f_

REM ── 5. 注入键鼠（亚秒级 sleep 用 PowerShell）──────────────
powershell -Command "Start-Sleep -Milliseconds 300" >nul
echo [注入] Ctrl+Alt+X （触发 overlay）
"%CLI%" send-keys "ctrl+alt+x"

powershell -Command "Start-Sleep -Milliseconds 600" >nul
echo [注入] 鼠标拖拽 (400,300) -^> (1300,800) 【主屏局部坐标】
"%CLI%" send-mouse drag 400,300 1300,800 --monitor 1

powershell -Command "Start-Sleep -Milliseconds 800" >nul
echo [注入] ESC （取消 overlay）
"%CLI%" send-keys "esc"

REM ── 6. 等连拍结束（15×250ms=3.75s，已过去 ~1.7s，再等 2.5s）
powershell -Command "Start-Sleep -Milliseconds 2500" >nul

REM ── 7. 基本断言 ───────────────────────────────────────────
set COUNT=0
for %%f in ("%OUT%\f_*.png") do set /a COUNT+=1
echo ---
echo 产出 PNG: !COUNT! / %LOOP_COUNT%

if not "!COUNT!"=="%LOOP_COUNT%" (
    echo FAIL: 预期 %LOOP_COUNT% 张 PNG，实际 !COUNT!
    exit /b 1
)

for %%I in ("%OUT%\f_000.png") do set SIZE_A=%%~zI
for %%I in ("%OUT%\f_005.png") do set SIZE_B=%%~zI

if !SIZE_A! equ !SIZE_B! (
    echo FAIL: 首帧和中间帧大小都是 !SIZE_A! 字节，overlay 可能没触发
    echo       检查：GUI 进程是否在跑、Ctrl+Alt+X 是否被其他程序抢占、屏幕是否被锁定
    exit /b 2
)

echo PASS: f_000=!SIZE_A!B, f_005=!SIZE_B!B，画面确有变化
echo 帧目录: %OUT%
echo 提示:   打开 f_005.png 可看到 overlay 的 ST_EDIT 态
exit /b 0

:nobuild
echo ERR: 找不到构建产物
echo   %GUI%
echo   %CLI%
echo 请先在 build\ 目录下执行：cmake --build . --config Release
exit /b 1
