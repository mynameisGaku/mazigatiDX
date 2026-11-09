# すべての検査（ビルド・書式・自己確認・サンプル・テンプレート・掃除）を、順に実行して、結果をまとめる。
#   powershell -NoProfile -ExecutionPolicy Bypass -File Tools\RunAllChecks.ps1 [-Configurations Debug,Release] [-SkipBuild] [-Retries 3] [-Quick]
#
#   先に、Tools\SetupSelfTests.ps1 で、検査のリポジトリ（framework-test）を、Source\SelfTest へ取り付けておく（DxLib は、FetchDxLib.bat で）。
#   順番:
#     1. ビルド（構成ごとに project.sln。警告・エラーが 0）。-SkipBuild で飛ばせる
#     2. バックエンド互換台帳の確認、書式の検査（CheckSourceFormat.ps1）と、その検査自身の確認、掃除（Clean.bat）の確認
#     3. 自己確認（RunSelfTests.ps1）: 構成ごとに ecs / assets / world
#     4. サンプル集の確認（RunSamplesTest.ps1）と、テンプレートの確認（RunTemplateTest.ps1）。どちらも、自分でビルドして動かす（時間がかかる）。-Quick で飛ばせる
#   結果は、1 行ずつ出して、最後に PASS / FAIL をまとめる（失敗があれば、終了コード 1）。各ステップのログは、一時フォルダーに残す。
#   -Retries: 自己確認が失敗したとき、最大この回数まで、やり直す。環境によっては、グラフィックスドライバーのクラッシュで、まれに（assets で
#   2 回に 1 回ほど）失敗する（アプリのコードの問題ではない）。やり直しても通らなければ、失敗として数える。
#   実行中に、このフォルダーの x64 の下の Samples.exe などが動いていると、リンクできないので、先に閉じる（動いていれば、止まる）。
param(
    [string[]]$Configurations = @('Debug', 'Release'),
    [switch]$SkipBuild,
    [int]$Retries = 3,
    [switch]$Quick
)

$ErrorActionPreference = 'Continue'
$frameworkRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$selfTest = Join-Path $frameworkRoot 'Source\SelfTest'
if (-not (Test-Path -LiteralPath (Join-Path $selfTest 'RunSelfTests.ps1'))) {
    throw 'Source\SelfTest is not mounted. Run Tools\SetupSelfTests.ps1 first.'
}

# 動いている実行ファイルが、ビルドの出力を、掴んでいないか
$running = @(Get-CimInstance Win32_Process | Where-Object { $_.ExecutablePath -and $_.ExecutablePath.StartsWith((Join-Path $frameworkRoot 'x64'), [StringComparison]::OrdinalIgnoreCase) })
if ($running.Count -gt 0) {
    throw ("These programs are running from the build folder and would block the build (close them first): " + (($running | ForEach-Object { "$($_.Name) (pid $($_.ProcessId))" }) -join ', '))
}

$logDirectory = Join-Path ([IO.Path]::GetTempPath()) ('mazigati_checks_' + (Get-Date -Format 'yyyyMMdd_HHmmss'))
New-Item -ItemType Directory -Path $logDirectory | Out-Null
$results = New-Object System.Collections.Generic.List[object]

# 1 つのステップを、子の PowerShell で実行する。出力は、ログへ。終了コードが 0 なら、成功
function Invoke-Step([string]$Name, [string]$ScriptPath, [string[]]$Arguments = @(), [int]$MaxAttempts = 1) {
    $safeName = ($Name -replace '[^A-Za-z0-9_.-]', '_')
    $started = Get-Date
    $attempt = 0
    $code = 1
    while ($attempt -lt $MaxAttempts -and $code -ne 0) {
        ++$attempt
        $log = Join-Path $logDirectory "$safeName.log"
        & powershell -NoProfile -ExecutionPolicy Bypass -File $ScriptPath @Arguments *> $log
        $code = $LASTEXITCODE
    }
    $seconds = [int]((Get-Date) - $started).TotalSeconds
    $note = if ($attempt -gt 1) { " (attempt $attempt of $MaxAttempts)" } else { '' }
    if ($code -eq 0) {
        Write-Host ("  ok   {0}  {1} s{2}" -f $Name, $seconds, $note)
    } else {
        Write-Host ("  FAIL {0}  {1} s{2}  (log: {3})" -f $Name, $seconds, $note, $log) -ForegroundColor Red
        Get-Content -LiteralPath $log -Tail 8 -ErrorAction SilentlyContinue | ForEach-Object { Write-Host ('       ' + $_) }
    }
    $results.Add([pscustomobject]@{ Name = $Name; Passed = ($code -eq 0) })
}

$msbuild = & "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe" -latest -find MSBuild\**\Bin\MSBuild.exe | Select-Object -First 1
if (-not $SkipBuild) {
    Write-Host '== build'
    foreach ($configuration in $Configurations) {
        $log = Join-Path $logDirectory "build_$configuration.log"
        $arguments = @((Join-Path $frameworkRoot 'project.sln'), "/p:Configuration=$configuration", '/p:Platform=x64', '/nologo', '/v:minimal')
        & $msbuild @arguments '/m' *> $log
        $code = $LASTEXITCODE
        if ($code -ne 0) {
            # リンクが、まれに、デバッグ情報の食い違い（LNK1103 / LNK1236）で失敗する（再試行で通る）。1 回だけ、順番に作り直す
            & $msbuild @arguments '/m:1' *> $log
            $code = $LASTEXITCODE
        }
        # MSB8029（一時フォルダーの下で作ると出る警告）は、確認用の clone でだけ出る既知のものなので、数えない
        $problems = @(Select-String -Path $log -Pattern 'error|warning' | ForEach-Object { $_.Line } | Where-Object { $_ -notmatch 'MSB8029' } | Sort-Object -Unique)
        $ok = $code -eq 0 -and $problems.Count -eq 0
        if ($ok) { Write-Host "  ok   build $configuration (0 warnings, 0 errors)" } else { Write-Host "  FAIL build $configuration (exit $code, $($problems.Count) warning/error line(s); log: $log)" -ForegroundColor Red }
        $results.Add([pscustomobject]@{ Name = "build $configuration"; Passed = $ok })
    }
}

# 公開機能の追加・削除を、互換台帳へ反映し忘れていないか。
Invoke-Step 'backend compatibility inventory' (Join-Path $frameworkRoot 'Tools\UpdateBackendCompatibility.ps1') @('-Check')

Write-Host '== source format and clean'
Invoke-Step 'source format' (Join-Path $selfTest 'CheckSourceFormat.ps1')
Invoke-Step 'source format check (self)' (Join-Path $selfTest 'RunSourceFormatCheckTest.ps1')
Invoke-Step 'Clean.bat' (Join-Path $selfTest 'RunCleanTest.ps1')

Write-Host '== self tests'
foreach ($configuration in $Configurations) {
    foreach ($suite in 'ecs', 'assets', 'world') {
        Invoke-Step "self test $suite $configuration" (Join-Path $selfTest 'RunSelfTests.ps1') @('-Suite', $suite, '-Configuration', $configuration) $Retries
    }
    Invoke-Step "render buffer contracts $configuration" (Join-Path $selfTest 'RunRenderBufferTests.ps1') @('-FrameworkRoot', $frameworkRoot, '-Configuration', $configuration, '-Platform')
    Invoke-Step "compute contracts $configuration" (Join-Path $selfTest 'RunComputeTests.ps1') @('-FrameworkRoot', $frameworkRoot, '-Configuration', $configuration, '-Platform')
    Invoke-Step "DX12 window contracts $configuration" (Join-Path $selfTest 'RunDX12WindowTests.ps1') @('-FrameworkRoot', $frameworkRoot, '-Configuration', $configuration, '-Platform')
}

if (-not $Quick) {
    Write-Host '== samples and template (they build and run by themselves; this takes a while)'
    Invoke-Step 'samples' (Join-Path $selfTest 'RunSamplesTest.ps1')
    Invoke-Step 'template project' (Join-Path $selfTest 'RunTemplateTest.ps1')
}

$failed = @($results | Where-Object { -not $_.Passed })
Write-Host ''
Write-Host "Logs: $logDirectory"
if ($failed.Count -eq 0) {
    Write-Host "ALL CHECKS PASS ($($results.Count) steps)" -ForegroundColor Green
    exit 0
}
Write-Host "CHECKS FAIL ($($failed.Count) of $($results.Count) steps: $(($failed | ForEach-Object { $_.Name }) -join ', '))" -ForegroundColor Red
exit 1
