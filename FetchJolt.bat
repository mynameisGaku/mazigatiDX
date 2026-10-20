@echo off
chcp 65001 > nul
setlocal

rem ============================================================================
rem  FetchJolt
rem    Jolt Physics のソースを GitHub から取得し VendorModules/Jolt に展開する。
rem
rem    使い方:
rem      FetchJolt.bat            ... 最新リリースを取得
rem      FetchJolt.bat v5.2.0     ... タグを指定して取得
rem
rem    取得後は GenerateProjectFiles.bat を実行して .cpp を vcxproj に登録すること。
rem ============================================================================

set "PROJECT_ROOT=%~dp0"
if "%PROJECT_ROOT:~-1%"=="\" set "PROJECT_ROOT=%PROJECT_ROOT:~0,-1%"
set "_BATFILE=%~f0"
set "JOLT_TAG=%~1"

echo ============================================
echo  FetchJolt
echo  Root: %PROJECT_ROOT%
echo ============================================
echo.

powershell -NoProfile -ExecutionPolicy Bypass -Command ^
  "$c=[IO.File]::ReadAllText($env:_BATFILE,[Text.Encoding]::UTF8);" ^
  "$s=$c.LastIndexOf('##PS_START##')+12;" ^
  "$e=$c.LastIndexOf('##PS_END##');" ^
  "Invoke-Expression $c.Substring($s,$e-$s)"

if %ERRORLEVEL% neq 0 (
    echo.
    echo [ERROR] Failed to fetch Jolt Physics.
    pause
    exit /b 1
)

echo.
echo Next: run GenerateProjectFiles.bat to add Jolt sources to the project.
echo.
echo Press any key to close...
pause > nul
endlocal
exit /b 0

##PS_START##
$ErrorActionPreference = 'Stop'
$ProgressPreference    = 'SilentlyContinue'
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12

$root    = $env:PROJECT_ROOT
$tag     = $env:JOLT_TAG
$repo    = 'jrouwe/JoltPhysics'
$destDir = Join-Path $root 'VendorModules\Jolt'
$headers = @{ 'User-Agent' = 'FetchJolt' }

# ---- Resolve release ----
if ([string]::IsNullOrWhiteSpace($tag)) {
    Write-Host 'Resolving latest release...'
    $release = Invoke-RestMethod -Uri "https://api.github.com/repos/$repo/releases/latest" -Headers $headers
    $tag = $release.tag_name
}
Write-Host "  Tag : $tag"
Write-Host ''

# ---- Download ----
$tempDir = Join-Path ([IO.Path]::GetTempPath()) ("FetchJolt_" + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $tempDir | Out-Null
try {
    $zipPath = Join-Path $tempDir 'jolt.zip'
    $zipUrl  = "https://github.com/$repo/archive/refs/tags/$tag.zip"
    Write-Host "Downloading $zipUrl ..."
    Invoke-WebRequest -Uri $zipUrl -OutFile $zipPath -Headers $headers -UseBasicParsing
    Write-Host '  OK' -ForegroundColor Green

    # ---- Extract ----
    Write-Host 'Extracting...'
    $extractDir = Join-Path $tempDir 'src'
    Expand-Archive -Path $zipPath -DestinationPath $extractDir -Force
    $repoDir = Get-ChildItem -Path $extractDir -Directory | Select-Object -First 1
    $srcJolt = Join-Path $repoDir.FullName 'Jolt'
    if (-not (Test-Path (Join-Path $srcJolt 'Jolt.h'))) {
        Write-Error "Jolt/Jolt.h not found in archive: $zipUrl"
        exit 1
    }
    Write-Host '  OK' -ForegroundColor Green

    # ---- Install ----
    # VendorModules/Jolt = リポジトリの Jolt/ ディレクトリ
    # (インクルードパス $(SolutionDir)VendorModules により <Jolt/Jolt.h> で参照できる)
    Write-Host "Installing to $destDir ..."
    if (Test-Path $destDir) {
        Remove-Item -Path $destDir -Recurse -Force
    }
    Copy-Item -Path $srcJolt -Destination $destDir -Recurse
    Copy-Item -Path (Join-Path $repoDir.FullName 'LICENSE') -Destination (Join-Path $destDir 'LICENSE') -ErrorAction SilentlyContinue
    [IO.File]::WriteAllText((Join-Path $destDir 'VERSION.txt'), "$tag`r`n")
    Write-Host '  OK' -ForegroundColor Green
}
finally {
    Remove-Item -Path $tempDir -Recurse -Force -ErrorAction SilentlyContinue
}

Write-Host ''
Write-Host '============================================' -ForegroundColor Cyan
Write-Host "  Jolt Physics $tag installed!" -ForegroundColor Cyan
Write-Host '============================================' -ForegroundColor Cyan
##PS_END##
