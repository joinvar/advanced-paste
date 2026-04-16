@echo off
REM ====================================================================
REM  scripts\e2e_overlay_capture.bat
REM  Multi-monitor end-to-end test (Windows cmd version)
REM ====================================================================
REM  NOTE: this .bat intentionally uses ASCII only. Windows cmd reads
REM  .bat files using the system ANSI code page (e.g. CP936 on Chinese
REM  Windows), and `chcp 65001` only changes console I/O encoding, not
REM  how cmd decodes the .bat file itself. Non-ASCII characters in a
REM  UTF-8 .bat get parsed wrongly and break command recognition.
REM
REM  For the full Chinese-commented walkthrough of what each step does,
REM  read the sibling `e2e_overlay_capture.sh` under git-bash, which
REM  has identical logic but UTF-8 safe (bash handles UTF-8 natively).
REM
REM  Script (5 PNGs = 2 for the baseline + 3 for the overlay states):
REM    1 Initial state          each monitor one shot (baseline)
REM    2 Hotkey on monitor 0    cursor -> monitor 0 -> Ctrl+Alt+X,
REM                             then shot monitor 0 (overlay should
REM                             appear here because overlay is bound
REM                             to the monitor that held the cursor
REM                             at StartCapture time, and does not
REM                             follow the cursor afterwards)
REM    3 Cursor -> monitor 1    shot monitor 1
REM    4 Press left + drag      on monitor 1, shot monitor 1
REM    5 ESC to cancel          NOTE right-click cancel is buggy on
REM                             multi-monitor setups; always use ESC
REM
REM  Prereq: `cmake --build . --config Release` already run under build\
REM  Local only: needs a real desktop session; headless CI cannot
REM    receive SendInput.
REM  During the ~2s run an overlay appears; do not touch keyboard/mouse.
REM
REM  The script kills AdvancedPaste.exe at both the start (to guarantee
REM  a clean overlay state) and the end (so an overlay that outlives
REM  ESC cannot pollute subsequent runs). If you want to keep using the
REM  GUI normally, launch it again manually afterwards.
REM
REM  Usage:
REM    scripts\e2e_overlay_capture.bat
REM    scripts\e2e_overlay_capture.bat D:\path\to\out
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

REM --- Coordinates (monitor-local when --monitor N is given) ---
set MON_CENTER_X=500
set MON_CENTER_Y=500
set MON_DRAG_X=1200
set MON_DRAG_Y=800

REM === 1. Check build artifacts ===
if not exist "%GUI%" goto :nobuild
if not exist "%CLI%" goto :nobuild

REM === 2. Reset output dir (delete only this specific subdir + recreate) ===
if exist "%OUT%" rd /s /q "%OUT%"
mkdir "%OUT%"

REM === 3. Start GUI from a clean state (kill any stale instance first) ===
taskkill /IM AdvancedPaste.exe /F >nul 2>&1
powershell -Command "Start-Sleep -Milliseconds 300" >nul

echo [start] launching GUI main process (clean state)...
start "" "%GUI%"
powershell -Command "Start-Sleep -Milliseconds 1500" >nul

REM ================================================================
REM  Step 1: baseline, one shot per monitor
REM  (hard-coded 2 monitors; adjust if your setup differs)
REM ================================================================
echo.
echo === Step 1/5: initial state, one shot per monitor ===
"%CLI%" capture --monitor 0 --out "%OUT%\01_init_mon0.png" >nul
"%CLI%" capture --monitor 1 --out "%OUT%\01_init_mon1.png" >nul
echo   -^> 01_init_mon0.png  01_init_mon1.png

REM ================================================================
REM  Step 2: cursor -> monitor 0 -> Ctrl+Alt+X, shoot monitor 0
REM ================================================================
echo.
echo === Step 2/5: cursor -^> mon 0, Ctrl+Alt+X, shoot mon 0 ===
"%CLI%" send-mouse move %MON_CENTER_X%,%MON_CENTER_Y% --monitor 0
powershell -Command "Start-Sleep -Milliseconds 300" >nul
"%CLI%" send-keys "ctrl+alt+x"
powershell -Command "Start-Sleep -Milliseconds 500" >nul
"%CLI%" capture --monitor 0 --out "%OUT%\02_hotkey_mon0.png" >nul
echo   -^> 02_hotkey_mon0.png

REM ================================================================
REM  Step 3: cursor -> monitor 1, shoot monitor 1
REM ================================================================
echo.
echo === Step 3/5: cursor -^> mon 1, shoot mon 1 ===
"%CLI%" send-mouse move %MON_CENTER_X%,%MON_CENTER_Y% --monitor 1
powershell -Command "Start-Sleep -Milliseconds 500" >nul
"%CLI%" capture --monitor 1 --out "%OUT%\03_move_mon1.png" >nul
echo   -^> 03_move_mon1.png

REM ================================================================
REM  Step 4: left-down + move on monitor 1, shoot monitor 1
REM ================================================================
echo.
echo === Step 4/5: left-down + drag on mon 1, shoot mon 1 ===
"%CLI%" send-mouse down
powershell -Command "Start-Sleep -Milliseconds 100" >nul
"%CLI%" send-mouse move %MON_DRAG_X%,%MON_DRAG_Y% --monitor 1
powershell -Command "Start-Sleep -Milliseconds 300" >nul
"%CLI%" capture --monitor 1 --out "%OUT%\04_dragging_mon1.png" >nul
echo   -^> 04_dragging_mon1.png

REM ================================================================
REM  Step 5: ESC to cancel. Release mouse first, move cursor back to
REM  the overlay's monitor (mon 0), THEN send ESC. Even so, taskkill
REM  at the end is the only 100%% reliable cleanup (see header note).
REM ================================================================
echo.
echo === Step 5/5: ESC to cancel ===
"%CLI%" send-mouse up 2>nul
powershell -Command "Start-Sleep -Milliseconds 100" >nul
"%CLI%" send-mouse move %MON_CENTER_X%,%MON_CENTER_Y% --monitor 0
powershell -Command "Start-Sleep -Milliseconds 200" >nul
"%CLI%" send-keys "esc"
powershell -Command "Start-Sleep -Milliseconds 300" >nul

REM === Assertions ===
set COUNT=0
for %%f in ("%OUT%\*.png") do set /a COUNT+=1
echo.
echo ----------------------------------------
echo PNG count: !COUNT! / 5
dir /b "%OUT%"

if not "!COUNT!"=="5" (
    echo FAIL: expected 5, got !COUNT!
    "%CLI%" send-mouse up 2>nul
    taskkill /IM AdvancedPaste.exe /F >nul 2>&1
    exit /b 1
)

echo.
echo PASS: file count matches
echo frames dir: %OUT%
echo.
echo Recommended viewing order:
echo   01_init_mon0.png / 01_init_mon1.png   baseline desktop
echo   02_hotkey_mon0.png                    mon 0 after hotkey (overlay expected)
echo   03_move_mon1.png                      mon 1 after cursor moved there
echo   04_dragging_mon1.png                  mon 1 during drag
echo.
echo (See e2e_overlay_capture.sh for the full Chinese annotated version.)

REM --- Cleanup: release mouse + force-kill GUI to wipe any residual overlay ---
"%CLI%" send-mouse up 2>nul
taskkill /IM AdvancedPaste.exe /F >nul 2>&1
exit /b 0

:nobuild
echo ERR: build artifacts not found
echo   %GUI%
echo   %CLI%
echo Please run under build\:  cmake --build . --config Release
exit /b 1
