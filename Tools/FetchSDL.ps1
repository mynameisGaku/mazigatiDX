# SDL の公式配布物を固定版・固定ハッシュで取得する。
$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'
$projectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$sdlVersion = '3.4.18'
$archiveHash = '78d84602ae616cfe26b33a73b7d3b9b6b56e8a6650e53d92c9c6ca938f747acd'
$destination = Join-Path $projectRoot 'VendorModules\SDL3'
if (Test-Path -LiteralPath (Join-Path $destination 'VERSION.txt')) {
    if ((Get-Content -LiteralPath (Join-Path $destination 'VERSION.txt') -Raw).Trim() -eq $sdlVersion -and (Test-Path -LiteralPath (Join-Path $destination 'include\SDL3\SDL.h')) -and (Test-Path -LiteralPath (Join-Path $destination 'lib\x64\SDL3.dll'))) {
        Write-Host "SDL $sdlVersion is already installed."
        exit 0
    }
    throw 'VendorModules/SDL3 already contains another installation. Preserve it before replacing it.'
}
if (Test-Path -LiteralPath $destination) { throw 'VendorModules/SDL3 already exists. Preserve it before installing SDL.' }
$staging = Join-Path ([IO.Path]::GetTempPath()) ('FrameworkSDL_' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $staging | Out-Null
try {
    $archive = Join-Path $staging 'SDL.zip'
    Invoke-WebRequest -Uri "https://github.com/libsdl-org/SDL/releases/download/release-$sdlVersion/SDL3-devel-$sdlVersion-VC.zip" -OutFile $archive -UseBasicParsing
    if ((Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash -ne $archiveHash) { throw 'SDL archive checksum mismatch.' }
    Expand-Archive -LiteralPath $archive -DestinationPath (Join-Path $staging 'unpacked')
    $package = Join-Path $staging "unpacked\SDL3-$sdlVersion"
    if (-not (Test-Path -LiteralPath (Join-Path $package 'include\SDL3\SDL_gamepad.h'))) { throw 'SDL headers are missing.' }
    Copy-Item -LiteralPath $package -Destination $destination -Recurse
    [IO.File]::WriteAllText((Join-Path $destination 'VERSION.txt'), $sdlVersion + [Environment]::NewLine)
    Write-Host "Installed SDL $sdlVersion. Run GenerateProjectFiles.bat next."
} finally {
    # 作成した一時ディレクトリーだけを消す。
    $resolvedStaging = [IO.Path]::GetFullPath($staging)
    $temporaryRoot = [IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd('\') + '\'
    if ($resolvedStaging.StartsWith($temporaryRoot, [StringComparison]::OrdinalIgnoreCase) -and (Split-Path $resolvedStaging -Leaf).StartsWith('FrameworkSDL_')) { Remove-Item -LiteralPath $resolvedStaging -Recurse -Force }
}
