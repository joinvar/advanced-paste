@echo off
REM ====================================================================
REM  scripts\run_all_tests.bat
REM  One-shot: build + unit tests + all e2e flow tests
REM ====================================================================
REM  ASCII only (cmd parses .bat using system ANSI code page).
REM
REM  Steps (sequential; abort on build/unit failure):
REM    1 build     cmake --build . --config Release
REM    2 unit      ctest -C Release  (~2 sec, no side effect)
REM    3 e2e_cap   cross-monitor capture flow (~5 sec, takes over
REM                keyboard/mouse, kills AdvancedPaste.exe)
REM    4 e2e_hov   single-monitor hover flow  (~5 sec, same caveats)
REM
REM  Outputs:
REM    build\Release\e2e_capture_out\  (5 PNGs from step 3)
REM    build\Release\e2e_hover_out\    (4 PNGs from step 4)
REM
REM  Total wall time: ~12-15 sec. During steps 3-4 do NOT touch input.
REM  At the end prints PASS/FAIL summary; exit code != 0 if anything
REM  failed. Build / unit failure aborts before e2e (no point running
REM  e2e against a broken binary).
REM
REM  Usage:
REM    scripts\run_all_tests.bat
REM ====================================================================

setlocal enabledelayedexpansion

set "ROOT=%~dp0.."
pushd "%ROOT%" >nul
set "ROOT=%CD%"
popd >nul

set "BUILD_DIR=%ROOT%\build"
set "SCRIPTS_DIR=%ROOT%\scripts"
set "CAP_OUT=%BUILD_DIR%\Release\e2e_capture_out"
set "HOV_OUT=%BUILD_DIR%\Release\e2e_hover_out"

if not exist "%BUILD_DIR%\CMakeCache.txt" (
    echo ERR: build directory not configured. Run first:
    echo   cmake -S . -B build
    exit /b 1
)

set FAIL_UNIT=0
set FAIL_CAP=0
set FAIL_HOV=0

echo ========================================
echo  STEP 1/4: build ^(Release^)
echo ========================================
cmake --build "%BUILD_DIR%" --config Release
if errorlevel 1 (
    echo.
    echo FAIL: build failed, abort.
    exit /b 1
)

echo.
echo ========================================
echo  STEP 2/4: unit tests ^(ctest^)
echo ========================================
pushd "%BUILD_DIR%" >nul
ctest -C Release --output-on-failure
if errorlevel 1 set FAIL_UNIT=1
popd >nul

if !FAIL_UNIT!==1 (
    echo.
    echo FAIL: unit tests failed, abort e2e.
    goto :summary
)

echo.
echo ========================================
echo  STEP 3/4: e2e overlay capture ^(cross-monitor^)
echo ========================================
echo NOTE: do NOT touch keyboard/mouse for ~5 seconds.
call "%SCRIPTS_DIR%\e2e_overlay_capture.bat" "%CAP_OUT%"
if errorlevel 1 set FAIL_CAP=1

echo.
echo ========================================
echo  STEP 4/4: e2e overlay hover ^(single-monitor^)
echo ========================================
echo NOTE: do NOT touch keyboard/mouse for ~5 seconds.
call "%SCRIPTS_DIR%\e2e_overlay_hover.bat" "%HOV_OUT%"
if errorlevel 1 set FAIL_HOV=1

:summary
echo.
echo ========================================
echo  Summary
echo ========================================
if !FAIL_UNIT!==0 (echo   [PASS] unit tests) else (echo   [FAIL] unit tests)
if !FAIL_CAP!==0  (echo   [PASS] e2e overlay capture - %CAP_OUT%) else (echo   [FAIL] e2e overlay capture)
if !FAIL_HOV!==0  (echo   [PASS] e2e overlay hover   - %HOV_OUT%) else (echo   [FAIL] e2e overlay hover)

set /a TOTAL_FAIL=!FAIL_UNIT!+!FAIL_CAP!+!FAIL_HOV!
if !TOTAL_FAIL! GTR 0 (
    echo.
    echo OVERALL: FAIL ^(!TOTAL_FAIL! suite^(s^) failed^)
    exit /b 1
)
echo.
echo OVERALL: PASS
exit /b 0
