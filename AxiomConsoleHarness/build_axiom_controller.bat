@echo off
setlocal

pushd "%~dp0AxiomJamesDSPController"
if errorlevel 1 exit /b %errorlevel%
dotnet build -c Release --artifacts-path "%TEMP%\jamesdsp-controller-artifacts"
set "BUILD_EXIT=%errorlevel%"
popd
exit /b %BUILD_EXIT%
