@echo off
REM scripts\e2e_overlay_capture.bat
REM End-to-end flow test (cmd version). Same logic as the .sh version:
REM   start GUI -> loop-capture in background -> send hotkey to trigger overlay
REM   -> simulate mouse drag -> send ESC. Output is a PNG sequence;
REM   size variation between the first frame and the middle frame proves
REM   the overlay was actually triggered.
REM
REM Prereq:  cmake --build . --config Release was run under build\
REM Local only: needs a real desktop session; headless CI cannot receive SendInput.
REM During the run an overlay appears on screen for ~3s; do not touch keyboard/mouse.
REM
REM Usage:
REM   scripts\e2e_overlay_capture.bat
REM   scripts\e2e_overlay_capture.bat D:\path\to\out
REM
REM NOTE: kept ASCII-only on purpose. Windows cmd default codepage (e.g. CP936)
REM does not play well with UTF-8 source bytes, which breaks line parsing.

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

REM --- 1. Check build artifacts ---
if not exist "%GUI%" goto :nobuild
if not exist "%CLI%" goto :nobuild

REM --- 2. Reset output dir ---
if exist "%OUT%" rd /s /q "%OUT%"
mkdir "%OUT%"

REM --- 3. Ensure GUI is running ---
tasklist /FI "IMAGENAME eq AdvancedPaste.exe" 2>nul | find /i "AdvancedPaste.exe" >nul
if errorlevel 1 (
    echo [start] GUI main process...
    start "" "%GUI%"
    powershell -Command "Start-Sleep -Milliseconds 1500" >nul
) else (
    echo [reuse] GUI main process already running
)

REM --- 4. Loop-capture in background (start /b: no new window) ---
echo [loop]  %LOOP_COUNT% frames x %LOOP_INTERVAL%ms -^> %OUT%
start /b "" "%CLI%" capture --monitor 1 --loop %LOOP_COUNT% --interval %LOOP_INTERVAL% --out-dir "%OUT%" --out-prefix f_

REM --- 5. Inject input (sub-second sleeps via PowerShell) ---
powershell -Command "Start-Sleep -Milliseconds 300" >nul
echo [inject] Ctrl+Alt+X (trigger overlay)
"%CLI%" send-keys "ctrl+alt+x"

powershell -Command "Start-Sleep -Milliseconds 600" >nul
echo [inject] mouse drag (400,300) -^> (1300,800) [monitor-local coords]
"%CLI%" send-mouse drag 400,300 1300,800 --monitor 1

powershell -Command "Start-Sleep -Milliseconds 800" >nul
echo [inject] ESC (cancel overlay)
"%CLI%" send-keys "esc"

REM --- 6. Wait for the loop-capture to finish.
REM     Total loop window = 15 x 250ms = 3.75s; ~1.7s elapsed above, so wait ~2.5s more.
powershell -Command "Start-Sleep -Milliseconds 2500" >nul

REM --- 7. Basic assertions ---
set COUNT=0
for %%f in ("%OUT%\f_*.png") do set /a COUNT+=1
echo ---
echo PNG count: !COUNT! / %LOOP_COUNT%

if not "!COUNT!"=="%LOOP_COUNT%" (
    echo FAIL: expected %LOOP_COUNT% PNG, got !COUNT!
    exit /b 1
)

for %%I in ("%OUT%\f_000.png") do set SIZE_A=%%~zI
for %%I in ("%OUT%\f_005.png") do set SIZE_B=%%~zI

if !SIZE_A! equ !SIZE_B! (
    echo FAIL: first and middle frames are both !SIZE_A! bytes - overlay may not have triggered
    echo        check: GUI process, Ctrl+Alt+X not stolen by another app, screen not locked
    exit /b 2
)

echo PASS: f_000=!SIZE_A!B, f_005=!SIZE_B!B - frames differ as expected
echo frames dir: %OUT%
echo hint: open f_005.png to see overlay ST_EDIT state (selection + orange border + toolbar)
exit /b 0

:nobuild
echo ERR: build artifacts not found
echo   %GUI%
echo   %CLI%
echo Please run under build\:  cmake --build . --config Release
exit /b 1
