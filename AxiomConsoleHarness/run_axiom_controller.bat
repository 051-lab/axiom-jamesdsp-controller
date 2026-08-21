@echo off
pushd "%~dp0AxiomJamesDSPController"
dotnet run -c Release --artifacts-path "%TEMP%\jamesdsp-controller-artifacts"
set "RUN_EXIT=%errorlevel%"
popd
exit /b %RUN_EXIT%
