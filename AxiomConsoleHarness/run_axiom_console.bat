@echo off
pushd "%~dp0..\build-axiom-console"
JamesDSPConsole.exe --watch-config -c "%~dp0package-default.ini"
set "RUN_EXIT=%errorlevel%"
popd
exit /b %RUN_EXIT%
