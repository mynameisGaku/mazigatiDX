@echo off
chcp 65001 > nul
setlocal

rem Run this after adding, removing or moving a .cpp / .h file: it rewrites the file lists of the project.
set "PROJECT_ROOT=%~dp0"
if "%PROJECT_ROOT:~-1%"=="\" set "PROJECT_ROOT=%PROJECT_ROOT:~0,-1%"
rem The folder of the framework (mazigatiDX), relative to this folder.
set "FRAMEWORK_ROOT=%PROJECT_ROOT%\__FRAMEWORK_RELPATH__"

echo ============================================
echo  GenerateProjectFiles
echo  Root: %PROJECT_ROOT%
echo ============================================
echo.

powershell -NoProfile -ExecutionPolicy Bypass -File "%FRAMEWORK_ROOT%\Tools\GenerateProjectFiles.ps1" -Root "%PROJECT_ROOT%"

if %ERRORLEVEL% neq 0 (
    echo.
    echo [ERROR] Project file generation failed.
    pause
    exit /b 1
)

echo.
echo Press any key to close...
pause > nul
endlocal
exit /b 0
