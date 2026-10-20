@echo off
setlocal
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0Tools\FetchSDL.ps1"
exit /b %ERRORLEVEL%
