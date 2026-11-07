@echo off
setlocal
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0Tools\FetchDxLib.ps1" %*
exit /b %ERRORLEVEL%
