# 自己確認（--selftest=ecs）を、ジョブのワーカー数を変えて何度も実行する。
#   - どの実行も PASS（ワールド値が以前の計算と 1 ビットも違わない）であること
#   - すべての実行でハッシュが一致すること（ワーカーの数・実行の順番に結果が依存しない）
# を確かめる。アプリは専用の一時フォルダへコピーして動かすので、プロジェクトの Saved / imgui.ini には触らない。
#
#   .\Tools\RunSelfTests.ps1                       # Release、ワーカー 0 / 1 / 4 / 15
#   .\Tools\RunSelfTests.ps1 -Configuration Debug  # Debug（アサートも有効）
#   .\Tools\RunSelfTests.ps1 -ShowBench            # 結果の行（SELFTEST）と計測（BENCH）の行も表示する
#   .\Tools\RunSelfTests.ps1 -Suite assets         # アセット・アニメーション・オーディオ・フォントの確認（--selftest=assets）
#   .\Tools\RunSelfTests.ps1 -Suite world          # ワールド（.world）・ビルボード・当たり判定の確認（--selftest=world）
param(
    [ValidateSet('ecs', 'assets', 'world')] [string]$Suite = 'ecs',
    [ValidateSet('Release', 'Debug')] [string]$Configuration = 'Release',
    [int[]]$WorkerCounts = @(0, 1, 4, 15),
    [int]$TimeoutSeconds = 120,
    [switch]$ShowBench
)
$ErrorActionPreference = 'Stop'
$projectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$buildDirectory = Join-Path $projectRoot "x64\$Configuration"
if (-not (Test-Path -LiteralPath (Join-Path $buildDirectory 'project.exe'))) { throw "project.exe was not found in $buildDirectory. Build the $Configuration configuration first." }

# 一時フォルダに、実行ファイル・DLL・Config をコピーする（作業フォルダをここにすると、ログなどもここに出る）
$sandbox = Join-Path ([IO.Path]::GetTempPath()) ('FrameworkSelfTest_' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path (Join-Path $sandbox 'bin') | Out-Null
try {
    Copy-Item -Path (Join-Path $buildDirectory '*.exe') -Destination (Join-Path $sandbox 'bin')
    Get-ChildItem -LiteralPath $buildDirectory -Filter *.dll | Copy-Item -Destination (Join-Path $sandbox 'bin')
    Copy-Item -LiteralPath (Join-Path $projectRoot 'Config') -Destination (Join-Path $sandbox 'Config') -Recurse

    $results = @()
    foreach ($workers in $WorkerCounts) {
        Remove-Item -LiteralPath (Join-Path $sandbox 'Saved') -Recurse -Force -ErrorAction SilentlyContinue
        $process = Start-Process -FilePath (Join-Path $sandbox 'bin\project.exe') -ArgumentList "--selftest=$Suite --job-threads=$workers" `
            -WorkingDirectory $sandbox -PassThru
        if (-not $process.WaitForExit($TimeoutSeconds * 1000)) {
            $process.Kill()
            $results += [pscustomobject]@{ Workers = $workers; Result = 'TIMEOUT'; Hash = '' }
            continue
        }

        $logFile = Get-ChildItem -Path (Join-Path $sandbox 'Saved\Logs\*.log') -ErrorAction SilentlyContinue | Sort-Object LastWriteTime | Select-Object -Last 1
        $line = if ($logFile) { Get-Content -LiteralPath $logFile.FullName -Encoding UTF8 | Where-Object { $_ -match "SELFTEST $Suite" } | Select-Object -Last 1 } else { $null }
        if ($ShowBench -and $logFile) {
            Get-Content -LiteralPath $logFile.FullName -Encoding UTF8 | Where-Object { $_ -match 'SelfTest\]' } | ForEach-Object { Write-Host ("[$workers worker(s)] " + ($_ -replace '^.*?(SELFTEST|BENCH:)', '$1' -replace '\s*\|\s*\S+\.cpp.*$', '')) }
        }
        if ($line -match 'SELFTEST \w+ PASS: (\d+) .*hash ([0-9a-f]+)') {
            $results += [pscustomobject]@{ Workers = $workers; Result = 'PASS'; Hash = $Matches[2]; Checked = $Matches[1] }
        } elseif ($line -match 'SELFTEST \w+ PASS: (\d+)') {
            $results += [pscustomobject]@{ Workers = $workers; Result = 'PASS'; Hash = '-'; Checked = $Matches[1] }
        } elseif ($line) {
            $results += [pscustomobject]@{ Workers = $workers; Result = 'FAIL'; Hash = ''; Checked = '' }
        } else {
            $results += [pscustomobject]@{ Workers = $workers; Result = "NO RESULT (exit code $($process.ExitCode))"; Hash = ''; Checked = '' }
        }
    }

    $results | Format-Table Workers, Result, Checked, Hash -AutoSize | Out-String | Write-Host

    $allPass = @($results | Where-Object { $_.Result -ne 'PASS' }).Count -eq 0
    $hashes = @($results | Where-Object { $_.Result -eq 'PASS' } | ForEach-Object { $_.Hash } | Select-Object -Unique)
    if (-not $allPass) { Write-Host 'SELF TEST FAILED' -ForegroundColor Red; exit 1 }
    if ($hashes.Count -ne 1) { Write-Host 'SELF TEST FAILED: the results differ between worker counts.' -ForegroundColor Red; exit 1 }
    Write-Host "SELF TEST PASSED (identical results with $($WorkerCounts -join ' / ') worker(s))" -ForegroundColor Green
}
finally {
    Remove-Item -LiteralPath $sandbox -Recurse -Force -ErrorAction SilentlyContinue
}
