# 自己確認（検査）のリポジトリ framework-test を取得して、このリポジトリの Source\SelfTest へ取り付ける（ジャンクション）。
#   powershell -NoProfile -ExecutionPolicy Bypass -File Tools\SetupSelfTests.ps1 [-TestRepository <URL またはフォルダー>] [-Branch main] [-Destination <取得先>]
#
#   - 検査は、このリポジトリに含めず、別のリポジトリ（framework-test）で管理している。取り付けると、GenerateProjectFiles が、
#     Source\SelfTest の cpp を取り込み、起動の引数 --selftest=ecs|assets|world が使えるようになる。取り付けなくても、ビルドはできる。
#   - 取得先の既定は、このリポジトリのフォルダーの隣の framework-test（すでにあれば、取得し直さず、そのまま使う）。
#   - 取り付けたあとは、GenerateProjectFiles.bat を、もう一度実行する。検査は、Tools\RunAllChecks.ps1 で、まとめて実行できる。
#   - 取り外すときは、ジャンクションだけを消す（cmd /c rmdir Source\SelfTest）。中身ごと消さないよう、Remove-Item -Recurse は使わない。
param(
    [string]$TestRepository = 'https://github.com/mynameisGaku/framework-test.git',
    [string]$Branch = 'main',
    [string]$Destination = ''
)

$ErrorActionPreference = 'Stop'
$frameworkRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
if ($Destination -eq '') { $Destination = Join-Path (Split-Path -Parent $frameworkRoot) 'framework-test' }
$Destination = [IO.Path]::GetFullPath($Destination)
$mount = Join-Path $frameworkRoot 'Source\SelfTest'

if (Test-Path -LiteralPath $mount) {
    $item = Get-Item -LiteralPath $mount -Force
    if (($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -and (Test-Path -LiteralPath (Join-Path $mount 'RunSelfTests.ps1'))) {
        Write-Host "Source\SelfTest is already mounted (-> $($item.Target))."
        exit 0
    }
    throw "Source\SelfTest already exists and is not a mounted framework-test. Preserve it before mounting."
}

if (Test-Path -LiteralPath $Destination) {
    if (-not (Test-Path -LiteralPath (Join-Path $Destination 'RunSelfTests.ps1'))) { throw "$Destination already exists and is not framework-test." }
    Write-Host "Using the existing framework-test in $Destination."
} else {
    Write-Host "Cloning $TestRepository (branch $Branch) into $Destination ..."
    & git clone --quiet --branch $Branch --single-branch $TestRepository $Destination
    if ($LASTEXITCODE -ne 0) { throw 'git clone failed.' }
}

New-Item -ItemType Directory -Force -Path (Split-Path -Parent $mount) | Out-Null
& cmd /c "mklink /J `"$mount`" `"$Destination`"" | Out-Null
if ($LASTEXITCODE -ne 0 -or -not (Test-Path -LiteralPath (Join-Path $mount 'RunSelfTests.ps1'))) { throw 'Could not mount Source\SelfTest.' }
Write-Host "Mounted Source\SelfTest -> $Destination. Run GenerateProjectFiles.bat next, then Tools\RunAllChecks.ps1."
