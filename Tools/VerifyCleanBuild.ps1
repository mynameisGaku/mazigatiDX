# まっさらな環境で、リポジトリがそのままビルドできて、動くかを確かめる（公開前の確認。M9）。
#   powershell -NoProfile -ExecutionPolicy Bypass -File Tools\VerifyCleanBuild.ps1 [-Source <リポジトリの場所または URL>] [-Branch dev] [-Configuration Debug] [-WorkDirectory <作業フォルダー>]
#
#   1. リポジトリを、新しいフォルダーへ clone する（コミットしたものだけ。作業フォルダーの、未コミットの変更や、追跡されていないファイルは入らない）
#   2. プロジェクトのファイル一覧を作る（GenerateProjectFiles）
#   3. project.sln をビルドする（mazigatiDX / project / Samples / Editor の 4 つ）。警告・エラーが無いことを確かめる
#   4. Null バックエンド（画面なし）で、project と Samples の、いくつかのサンプルを動かして、終了コード 0 を確かめる
#   DxLib は、リポジトリに入っていないので、先に FetchDxLib.bat で、ビルドが探す場所へ置いておく（無ければ、止まる）。
#   -Source を省くと、このスクリプトのあるリポジトリの origin を使う。まだ push していない変更を確かめるなら、-Source にリポジトリのフォルダーを渡す
#   （ローカルの clone になるので、コミットしたものだけが入る）。
#   -RunChecks を付けると、そのあと、同じ clone へ、検査のリポジトリを取り付けて（Tools\SetupSelfTests.ps1）、検査（Tools\RunAllChecks.ps1 -Quick）も通す
#   （検査のリポジトリは、作業フォルダーの隣の「<作業フォルダー>_tests」へ clone する）。
#   作業フォルダーは、作業のたびに作り直す（既定は %TEMP%\mazigati_clean_verify。同じ名前のフォルダーが、すでにあれば、消す）。
param(
    [string]$Source = '',
    [string]$Branch = 'dev',
    [ValidateSet('Debug', 'Release')][string]$Configuration = 'Debug',
    [string]$WorkDirectory = '',
    [switch]$RunChecks
)

$ErrorActionPreference = 'Stop'
$frameworkRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
if ($Source -eq '') { $Source = (& git -C $frameworkRoot remote get-url origin).Trim() }
if ($WorkDirectory -eq '') { $WorkDirectory = Join-Path ([IO.Path]::GetTempPath()) 'mazigati_clean_verify' }
$WorkDirectory = [IO.Path]::GetFullPath($WorkDirectory)

$failures = New-Object System.Collections.Generic.List[string]
function Check([bool]$Ok, [string]$Message) {
    if ($Ok) { Write-Host "  ok   $Message" } else { Write-Host "  FAIL $Message" -ForegroundColor Red; $script:failures.Add($Message) }
}

# DxLib が、ビルドの探す場所にあるか（Build\mazigatiDX.props の MazigatiDxLibDir。環境変数で上書きできる）
$dxDir = if ($env:MazigatiDxLibDir) { $env:MazigatiDxLibDir } else { 'C:\DxLib' }
if (-not (Test-Path -LiteralPath (Join-Path $dxDir 'DxLib.h'))) {
    throw "DxLib was not found in $dxDir. Run FetchDxLib.bat first."
}

# 消してよいのは、この確認が作る、決まった名前のフォルダーだけ
$temporaryRoot = [IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd('\') + '\'
$leaf = Split-Path $WorkDirectory -Leaf
$testsDirectory = $WorkDirectory + '_tests'
foreach ($candidate in @($WorkDirectory, $testsDirectory)) {
    if (Test-Path -LiteralPath $candidate) {
        if (-not ($candidate.StartsWith($temporaryRoot, [StringComparison]::OrdinalIgnoreCase) -and (Split-Path $candidate -Leaf).StartsWith('mazigati_clean'))) {
            throw "The folder already exists and is not a temporary mazigati_clean* folder: $candidate"
        }
        # 取り付けたジャンクションがあれば、中身（検査のリポジトリ）ごと消さないよう、ジャンクションだけを、先に外す
        $mounted = Join-Path $candidate 'Source\SelfTest'
        if ((Test-Path -LiteralPath $mounted) -and ((Get-Item -LiteralPath $mounted -Force).Attributes -band [IO.FileAttributes]::ReparsePoint)) { & cmd /c "rmdir `"$mounted`"" | Out-Null }
        Remove-Item -LiteralPath $candidate -Recurse -Force
    }
}

Write-Host "== clone ($Source, branch $Branch)"
& git clone --quiet --branch $Branch --single-branch $Source $WorkDirectory
Check ($LASTEXITCODE -eq 0 -and (Test-Path -LiteralPath (Join-Path $WorkDirectory 'project.sln'))) 'the repository was cloned'
if ($failures.Count -gt 0) { exit 1 }

Write-Host '== GenerateProjectFiles'
Push-Location $WorkDirectory
try {
    & cmd /c ".\GenerateProjectFiles.bat < NUL" | Out-Null
    Check ($LASTEXITCODE -eq 0) 'the project files were generated'

    Write-Host "== build ($Configuration)"
    $msbuild = & "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe" -latest -find MSBuild\**\Bin\MSBuild.exe | Select-Object -First 1
    $log = Join-Path $WorkDirectory 'verify_build.txt'
    # 一時フォルダーに作るので、MSB8029（中間フォルダーが一時フォルダー）の警告を、抑える（確認の置き場所のせいで、リポジトリの問題ではない）
    $arguments = @('project.sln', "/p:Configuration=$Configuration", '/p:Platform=x64', '/p:IgnoreWarnIntDirInTempDetected=true', '/nologo', '/v:minimal')
    & $msbuild @arguments '/m' *> $log
    $code = $LASTEXITCODE
    if ($code -ne 0) {
        # リンクが、まれに、デバッグ情報の食い違いで失敗する（再試行で通る）。1 回だけ、順番に作り直す
        Write-Host '  (retrying once, serially)'
        & $msbuild @arguments '/m:1' *> $log
        $code = $LASTEXITCODE
    }
    $problems = @(Select-String -Path $log -Pattern 'error|warning' | ForEach-Object { $_.Line } | Sort-Object -Unique)
    Check ($code -eq 0) "msbuild exit code $code"
    Check ($problems.Count -eq 0) "no build warnings or errors ($($problems.Count))"
    foreach ($exe in 'project', 'Samples', 'Editor') {
        Check (Test-Path -LiteralPath (Join-Path $WorkDirectory "x64\$Configuration\$exe.exe")) "$exe.exe was built"
    }

    Write-Host '== run with the Null backend'
    $runs = @(
        @{ Exe = 'project'; Directory = $WorkDirectory; Arguments = '--backend=null --max-frames=30' },
        @{ Exe = 'Samples'; Directory = (Join-Path $WorkDirectory 'Samples'); Arguments = '--backend=null --max-frames=30' },
        @{ Exe = 'Samples'; Directory = (Join-Path $WorkDirectory 'Samples'); Arguments = '--backend=null --max-frames=30 --sample=Platformer3D' },
        @{ Exe = 'Samples'; Directory = (Join-Path $WorkDirectory 'Samples'); Arguments = '--backend=null --max-frames=30 --sample=Adventure' }
    )
    foreach ($run in $runs) {
        $exePath = Join-Path $WorkDirectory "x64\$Configuration\$($run.Exe).exe"
        if (-not (Test-Path -LiteralPath $exePath)) { continue }
        $process = Start-Process -FilePath $exePath -ArgumentList $run.Arguments -WorkingDirectory $run.Directory -PassThru -WindowStyle Hidden
        $done = $process.WaitForExit(120000)
        if (-not $done) { $process.Kill() }
        Check ($done -and $process.ExitCode -eq 0) "$($run.Exe) $($run.Arguments) exits with 0"
    }

    if ($RunChecks) {
        Write-Host '== self tests (mount framework-test, then Tools\RunAllChecks.ps1 -Quick)'
        & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $WorkDirectory 'Tools\SetupSelfTests.ps1') -Destination $testsDirectory | Out-Host
        Check ($LASTEXITCODE -eq 0 -and (Test-Path -LiteralPath (Join-Path $WorkDirectory 'Source\SelfTest\RunSelfTests.ps1'))) 'framework-test was fetched and mounted'
        & cmd /c ".\GenerateProjectFiles.bat < NUL" | Out-Null
        & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $WorkDirectory 'Tools\RunAllChecks.ps1') -Configurations $Configuration -Quick | Out-Host
        Check ($LASTEXITCODE -eq 0) 'Tools\RunAllChecks.ps1 -Quick passes'
    }
} finally { Pop-Location }

if ($failures.Count -eq 0) {
    Write-Host 'CLEAN BUILD PASS'
    exit 0
}
Write-Host "CLEAN BUILD FAIL ($($failures.Count) failure(s))" -ForegroundColor Red
exit 1
