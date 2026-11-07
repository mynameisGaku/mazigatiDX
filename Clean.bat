@echo off
chcp 65001 > nul
setlocal

rem Deletes temporary files (logs, crash reports, profiler traces, ...) and, if asked, build outputs.
rem
rem   Clean.bat                    temporary files (shows what will be deleted, and asks first)
rem   Clean.bat --dry-run          only lists what would be deleted
rem   Clean.bat build              build outputs (x64, project, mazigatiDX, .vs ...). Close Visual Studio first
rem   Clean.bat all                temporary files and build outputs
rem   Options:
rem     --screenshots              also Saved\Screenshots
rem     --savegames                also Saved\SaveGames (the save data of the game: it cannot be restored!)
rem     --yes                      do not ask before deleting
rem     --no-pause                 do not wait for a key at the end (for scripts)
rem
rem Never touched: source files, Assets, Config, imgui.ini, Source\SelfTest. See Tools\Clean.ps1.

set "PROJECT_ROOT=%~dp0"
if "%PROJECT_ROOT:~-1%"=="\" set "PROJECT_ROOT=%PROJECT_ROOT:~0,-1%"

set "PS_ARGS="
set "NO_PAUSE="

:parse
if "%~1"=="" goto run
if /i "%~1"=="temp" (set "PS_ARGS=%PS_ARGS% -Level temp") & shift & goto parse
if /i "%~1"=="build" (set "PS_ARGS=%PS_ARGS% -Level build") & shift & goto parse
if /i "%~1"=="all" (set "PS_ARGS=%PS_ARGS% -Level all") & shift & goto parse
if /i "%~1"=="--dry-run" (set "PS_ARGS=%PS_ARGS% -DryRun") & shift & goto parse
if /i "%~1"=="--screenshots" (set "PS_ARGS=%PS_ARGS% -Screenshots") & shift & goto parse
if /i "%~1"=="--savegames" (set "PS_ARGS=%PS_ARGS% -SaveGames") & shift & goto parse
if /i "%~1"=="--yes" (set "PS_ARGS=%PS_ARGS% -Yes") & shift & goto parse
if /i "%~1"=="--no-pause" (set "NO_PAUSE=1") & shift & goto parse
echo Unknown argument: %~1
echo Usage: Clean.bat [temp^|build^|all] [--dry-run] [--screenshots] [--savegames] [--yes] [--no-pause]
set "EXIT_CODE=1"
goto finish

:run
echo ============================================
echo  Clean
echo ============================================
echo.
powershell -NoProfile -ExecutionPolicy Bypass -File "%PROJECT_ROOT%\Tools\Clean.ps1" %PS_ARGS%
set "EXIT_CODE=%errorlevel%"

:finish
echo.
if not defined NO_PAUSE pause
exit /b %EXIT_CODE%
