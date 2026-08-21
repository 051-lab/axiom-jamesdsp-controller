@echo off
setlocal
title JamesDSP Controller 8-Hour Qualification Test

set "SCRIPT_DIR=%~dp0"
set "SEED=%LOCALAPPDATA%\JamesDSP\Qualification\earpods-controller-state.json"
if not exist "%SEED%" if exist "%LOCALAPPDATA%\Axiom\Qualification\earpods-controller-state.json" set "SEED=%LOCALAPPDATA%\Axiom\Qualification\earpods-controller-state.json"

echo JamesDSP Controller 8-Hour Qualification Test
echo.
echo This will run the installed JamesDSP Controller for 8 hours.
echo Route seed: %SEED%
echo.
echo Before continuing:
echo   1. Plug in the USB-C EarPods.
echo   2. Close JamesDSP Controller if it is open.
echo   3. Keep the laptop on AC power.
echo   4. Do not unplug audio devices or change Windows audio routes during the run.
echo.
pause

if not exist "%SEED%" (
  echo.
  echo ERROR: Route seed was not found:
  echo %SEED%
  echo.
  echo Open JamesDSP Controller, select VB-CABLE to EarPods,
  echo then ask Codex to recreate the qualification route seed.
  pause
  exit /b 1
)

powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%SCRIPT_DIR%tests\run_overnight_gate.ps1" -DurationMinutes 480 -ConfigReloadCount 24 -StateSeedPath "%SEED%"
set "EXITCODE=%ERRORLEVEL%"

echo.
if "%EXITCODE%"=="0" (
  echo JamesDSP Controller 8-hour qualification finished successfully.
) else (
  echo JamesDSP Controller 8-hour qualification ended with errors. Check the newest folder under:
  echo %LOCALAPPDATA%\JamesDSP\SoakTests
)
echo.
pause
exit /b %EXITCODE%
