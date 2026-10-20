@echo off
setlocal
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0Tools\FetchImGui.ps1"
exit /b %ERRORLEVEL%
