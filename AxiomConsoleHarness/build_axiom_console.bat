@echo off
setlocal

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
  echo Visual Studio Installer vswhere.exe was not found.
  exit /b 1
)
set "PATH=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer;%PATH%"

set "VSINSTALL="
set "VSINSTALL_FILE=%TEMP%\jamesdsp-vsinstall-%RANDOM%-%RANDOM%.txt"
"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath > "%VSINSTALL_FILE%"
if errorlevel 1 (
  del /q "%VSINSTALL_FILE%" >nul 2>&1
  exit /b 1
)
set /p "VSINSTALL="<"%VSINSTALL_FILE%"
del /q "%VSINSTALL_FILE%" >nul 2>&1
if not defined VSINSTALL (
  echo Visual Studio C++ Build Tools were not found.
  exit /b 1
)

call "%VSINSTALL%\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64
if errorlevel 1 exit /b %errorlevel%

pushd "%~dp0.."
if errorlevel 1 exit /b %errorlevel%

cmake -S AxiomConsoleHarness -B build-axiom-console -G Ninja -DCMAKE_BUILD_TYPE=Release
if errorlevel 1 goto build_failed

cmake --build build-axiom-console --config Release
set "BUILD_EXIT=%errorlevel%"
popd
exit /b %BUILD_EXIT%

:build_failed
set "BUILD_EXIT=%errorlevel%"
popd
exit /b %BUILD_EXIT%
