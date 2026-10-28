@echo off
chcp 65001 > nul
setlocal

set "PROJECT_ROOT=%~dp0"
if "%PROJECT_ROOT:~-1%"=="\" set "PROJECT_ROOT=%PROJECT_ROOT:~0,-1%"

echo ============================================
echo  GenerateProjectFiles
echo  Root: %PROJECT_ROOT%
echo ============================================
echo.

rem The projects in the root folder (the framework and the game) and in the Samples folder.
for %%R in ("%PROJECT_ROOT%" "%PROJECT_ROOT%\Samples") do (
    powershell -NoProfile -ExecutionPolicy Bypass -File "%PROJECT_ROOT%\Tools\GenerateProjectFiles.ps1" -Root "%%~R"
    if errorlevel 1 goto failed
)

echo.
echo Press any key to close...
pause > nul
endlocal
exit /b 0

:failed
echo.
echo [ERROR] Project file generation failed.
pause
exit /b 1
