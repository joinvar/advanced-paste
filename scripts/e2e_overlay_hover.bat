@echo off
REM ====================================================================
REM  scripts\e2e_overlay_hover.bat
REM  Single-monitor hover test on monitor 1 (Windows cmd version)
REM ====================================================================
REM  NOTE: ASCII only (cmd parses .bat using system ANSI code page;
REM  non-ASCII bytes break command recognition). For Chinese context
REM  read this header in the chat / commit message.
REM
REM  Purpose: stay on monitor 1 the whole time and watch how the
REM  overlay's hover-highlight rect changes as the cursor moves to
REM  different windows on the same screen. No cross-monitor migration
REM  is exercised here (see e2e_overlay_capture.bat for that).
REM
REM  Script (4 PNGs, all of monitor 1):
REM    1 Initial state          cursor at mon 1 center, shoot mon 1
REM    2 Ctrl+Alt+X             overlay should appear on mon 1
REM    3 Cursor -> circle 1     ~upper-left area, shoot mon 1
REM    4 Cursor -> circle 2     ~lower-right area, shoot mon 1
REM    5 ESC to cancel          (no screenshot)
REM
REM  Coordinates for circle 1 / circle 2 are estimated from a 596x336
REM  reference desktop screenshot of monitor 1 (1920x1080, see chat
REM  attachment). Adjust the constants below if your layout differs.
REM
REM  Prereq: build\Release\ already has AdvancedPaste.exe and
REM  AdvancedPasteCli.exe (run cmake --build . --config Release).
REM  Local only: needs a real desktop session.
REM  During the run an overlay appears on mon 1; do not touch input.
REM
REM  Usage:
REM    scripts\e2e_overlay_hover.bat
REM    scripts\e2e_overlay_hover.bat D:\path\to\out
REM ====================================================================

setlocal enabledelayedexpansion

REM --- Locate project root: script dir's parent ---
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

REM --- Coordinates (monitor-local; mon 1 is 1920x1080) ---
REM  Initial cursor position: roughly screen center, away from any edge
set MON_CENTER_X=960
set MON_CENTER_Y=540
REM  Circle 1: upper-left desktop icon area (~200, 260 on mon 1)
set MON_C1_X=200
set MON_C1_Y=260
REM  Circle 2: lower-right empty desktop area (~1740, 720 on mon 1)
set MON_C2_X=1740
set MON_C2_Y=720

REM === 1. Check build artifacts ===
if not exist "%GUI%" goto :nobuild
if not exist "%CLI%" goto :nobuild

REM === 2. Reset output dir ===
if exist "%OUT%" rd /s /q "%OUT%"
mkdir "%OUT%"

REM === 3. Start GUI from a clean state ===
taskkill /IM AdvancedPaste.exe /F >nul 2>&1
powershell -Command "Start-Sleep -Milliseconds 300" >nul

echo [start] launching GUI main process (clean state)...
start "" "%GUI%"
powershell -Command "Start-Sleep -Milliseconds 1500" >nul

REM ================================================================
REM  Step 1: cursor at mon 1 initial position, shoot mon 1 (baseline)
REM ================================================================
echo.
echo === Step 1/5: cursor at mon 1 center, shoot mon 1 (baseline) ===
"%CLI%" send-mouse move %MON_CENTER_X%,%MON_CENTER_Y% --monitor 1
powershell -Command "Start-Sleep -Milliseconds 300" >nul
"%CLI%" capture --monitor 1 --out "%OUT%\01_init_mon1.png" >nul
echo   -^> 01_init_mon1.png

REM ================================================================
REM  Step 2: Ctrl+Alt+X, shoot mon 1 (overlay should appear here)
REM ================================================================
echo.
echo === Step 2/5: Ctrl+Alt+X, shoot mon 1 (overlay expected) ===
"%CLI%" send-keys "ctrl+alt+x"
powershell -Command "Start-Sleep -Milliseconds 500" >nul
"%CLI%" capture --monitor 1 --out "%OUT%\02_hotkey_mon1.png" >nul
echo   -^> 02_hotkey_mon1.png

REM ================================================================
REM  Step 3: cursor -> circle 1 (upper-left), shoot mon 1
REM  Hover-highlight rect should follow the window under the cursor
REM ================================================================
echo.
echo === Step 3/5: cursor -^> circle 1, shoot mon 1 ===
"%CLI%" send-mouse move %MON_C1_X%,%MON_C1_Y% --monitor 1
powershell -Command "Start-Sleep -Milliseconds 400" >nul
"%CLI%" capture --monitor 1 --out "%OUT%\03_circle1_mon1.png" >nul
echo   -^> 03_circle1_mon1.png

REM ================================================================
REM  Step 4: cursor -> circle 2 (lower-right), shoot mon 1
REM ================================================================
echo.
echo === Step 4/5: cursor -^> circle 2, shoot mon 1 ===
"%CLI%" send-mouse move %MON_C2_X%,%MON_C2_Y% --monitor 1
powershell -Command "Start-Sleep -Milliseconds 400" >nul
"%CLI%" capture --monitor 1 --out "%OUT%\04_circle2_mon1.png" >nul
echo   -^> 04_circle2_mon1.png

REM ================================================================
REM  Step 5: ESC to cancel overlay (no screenshot)
REM  Cursor is already on mon 1 where the overlay lives, so ESC
REM  reaches the overlay window. Final taskkill at the bottom is
REM  the only 100%% reliable cleanup.
REM ================================================================
echo.
echo === Step 5/5: ESC to cancel ===
"%CLI%" send-keys "esc"
powershell -Command "Start-Sleep -Milliseconds 300" >nul

REM === Assertions ===
set COUNT=0
for %%f in ("%OUT%\*.png") do set /a COUNT+=1
echo.
echo ----------------------------------------
echo PNG count: !COUNT! / 4
dir /b "%OUT%"

if not "!COUNT!"=="4" (
    echo FAIL: expected 4, got !COUNT!
    "%CLI%" send-mouse up 2>nul
    taskkill /IM AdvancedPaste.exe /F >nul 2>&1
    exit /b 1
)

echo.
echo PASS: file count matches
echo frames dir: %OUT%
echo.
echo Recommended viewing order:
echo   01_init_mon1.png       baseline desktop, cursor at center
echo   02_hotkey_mon1.png     overlay just appeared on mon 1
echo   03_circle1_mon1.png    cursor at upper-left (circle 1)
echo   04_circle2_mon1.png    cursor at lower-right (circle 2)

REM --- Cleanup: release mouse + force-kill GUI ---
"%CLI%" send-mouse up 2>nul
taskkill /IM AdvancedPaste.exe /F >nul 2>&1
exit /b 0

:nobuild
echo ERR: build artifacts not found
echo   %GUI%
echo   %CLI%
echo Please run under build\:  cmake --build . --config Release
exit /b 1
