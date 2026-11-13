# ゲームの配布物（人に渡せるフォルダー。zip にもできる）を作る。
#   powershell -NoProfile -ExecutionPolicy Bypass -File Tools\PackageGame.ps1 [-ProjectRoot C:\dev\MyGame] [-Executable <exe>] [-Name MyGame] [-Version 1.0.0]
#                                                                             [-Configuration Release] [-Destination <folder>] [-Zip] [-NoVerify] [-Force]
#
#   先に、ゲームをビルドしておく（既定は Release）。このスクリプトは、ビルドしない。
#   <Destination>\<Name>[-<Version>] に、次を集める:
#     <Name>.exe            ゲームの実行ファイル（<ProjectRoot>\x64\<Configuration> から。名前は -Name に変える）
#     SDL3.dll              実行ファイルの隣にある DLL（すべて）
#     Config\               <ProjectRoot>\Config（autoexec.cfg は、Debug ビルド用なので、入れない）
#     Assets\               <ProjectRoot>\Assets（.gitkeep は、入れない）
#     EngineContent\        エンジン組み込みの素材（このリポジトリの EngineContent）
#     Licenses\             このリポジトリの LICENSE・第三者のソフトウェアの一覧（DxLib の著作権表示を含む）・VendorModules の各ライセンス文
#     README.txt            起動のしかたと、ライセンスの場所
#   -ProjectRoot: ゲームのプロジェクトのフォルダー（Config・Assets・x64 がある所）。既定は、このリポジトリ（project.exe）。サンプル集なら Samples。
#   -Executable:  実行ファイル。既定は、<ProjectRoot>\x64\<Configuration> にある、ただ 1 つの .exe（Editor.exe と Samples.exe は除く。2 つ以上あれば、指定する）
#   -Zip:         フォルダーと同じ名前の .zip も作る
#   -NoVerify:    集めたあとの確認（配布物のフォルダーで、画面を出さずに 30 フレーム動かす。--backend=null）を、しない
#   -Force:       同じ名前の配布物があれば、消して作り直す（無ければ、止まる）
#   実行に要るもの: Windows 10 / 11（64 ビット）と、DirectX 11 が動くグラフィックス。Visual C++ のランタイムは、実行ファイルに入っている（別に配らなくてよい）。
#   素材は、ファイルのまま入れる（1 つのアーカイブに束ねる・暗号化する機能は、無い）。
param(
    [string]$ProjectRoot = '',
    [string]$Executable = '',
    [string]$Name = '',
    [string]$Version = '',
    [string]$Configuration = 'Release',
    [string]$Destination = '',
    [switch]$Zip,
    [switch]$NoVerify,
    [switch]$Force
)

$ErrorActionPreference = 'Stop'
$frameworkRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
if ($ProjectRoot -eq '') { $ProjectRoot = $frameworkRoot }
$ProjectRoot = [IO.Path]::GetFullPath($ProjectRoot)
if (-not (Test-Path -LiteralPath $ProjectRoot -PathType Container)) { throw "The project folder does not exist: $ProjectRoot" }

# 実行ファイルを決める
if ($Executable -eq '') {
    # サンプル集（Samples）は、リポジトリのルートの x64 にビルドされる
    $buildFolders = @((Join-Path $ProjectRoot "x64\$Configuration"), (Join-Path (Split-Path -Parent $ProjectRoot) "x64\$Configuration")) | Where-Object { Test-Path -LiteralPath $_ }
    $projectName = Split-Path -Leaf $ProjectRoot
    $found = @()
    foreach ($folder in $buildFolders) {
        $named = Join-Path $folder ($projectName + '.exe')
        if (Test-Path -LiteralPath $named) { $found = @($named); break }
        $candidates = @(Get-ChildItem -LiteralPath $folder -Filter *.exe -File | Where-Object { $_.Name -ne 'Editor.exe' -and $_.Name -ne 'Samples.exe' } | ForEach-Object { $_.FullName })
        if ($candidates.Count -gt 0) { $found = $candidates; break }
    }
    if ($found.Count -eq 0) { throw "No executable was found under $ProjectRoot\x64\$Configuration. Build the game first (configuration $Configuration), or pass -Executable." }
    if ($found.Count -gt 1) { throw ("More than one executable was found; pass -Executable: " + ($found -join ', ')) }
    $Executable = $found[0]
}
$Executable = [IO.Path]::GetFullPath($Executable)
if (-not (Test-Path -LiteralPath $Executable -PathType Leaf)) { throw "The executable does not exist: $Executable" }
if ($Name -eq '') { $Name = [IO.Path]::GetFileNameWithoutExtension($Executable) }
if ($Name -notmatch '^[A-Za-z0-9][A-Za-z0-9_. -]*$') { throw "-Name may contain only letters, digits, space, '_', '.' and '-' (got '$Name')." }
if ($Version -ne '' -and $Version -notmatch '^[A-Za-z0-9][A-Za-z0-9_.+-]*$') { throw "-Version may contain only letters, digits, '_', '.', '+' and '-' (got '$Version')." }

if ($Destination -eq '') { $Destination = Join-Path $ProjectRoot 'Packages' }
$Destination = [IO.Path]::GetFullPath($Destination)
$packageName = if ($Version -ne '') { "$Name-$Version" } else { $Name }
$package = Join-Path $Destination $packageName
$zipPath = $package + '.zip'
# 配布物を、素材のフォルダーの中に作ると、自分自身を集めてしまう
foreach ($source in @((Join-Path $ProjectRoot 'Assets'), (Join-Path $ProjectRoot 'Config'), (Join-Path $frameworkRoot 'EngineContent'))) {
    if (($package + '\').StartsWith(([IO.Path]::GetFullPath($source) + '\'), [StringComparison]::OrdinalIgnoreCase)) { throw "The destination must not be inside $source" }
}
if (Test-Path -LiteralPath $package) {
    if (-not $Force) { throw "The package folder already exists (pass -Force to replace it): $package" }
    Remove-Item -LiteralPath $package -Recurse -Force
}
if ($Zip -and (Test-Path -LiteralPath $zipPath)) {
    if (-not $Force) { throw "The zip already exists (pass -Force to replace it): $zipPath" }
    Remove-Item -LiteralPath $zipPath -Force
}
New-Item -ItemType Directory -Force -Path $package | Out-Null

# フォルダーを、中身ごと写す（Exclude の名前のファイルは、入れない）
function Copy-Tree([string]$Source, [string]$Target, [string[]]$Exclude = @()) {
    if (-not (Test-Path -LiteralPath $Source -PathType Container)) { return 0 }
    $count = 0
    $root = [IO.Path]::GetFullPath($Source).TrimEnd('\')
    foreach ($file in Get-ChildItem -LiteralPath $root -Recurse -File -Force) {
        if ($Exclude -contains $file.Name) { continue }
        $relative = $file.FullName.Substring($root.Length + 1)
        $to = Join-Path $Target $relative
        New-Item -ItemType Directory -Force -Path (Split-Path -Parent $to) | Out-Null
        Copy-Item -LiteralPath $file.FullName -Destination $to
        ++$count
    }
    return $count
}

# 実行ファイルと、隣の DLL
Copy-Item -LiteralPath $Executable -Destination (Join-Path $package ($Name + '.exe'))
$dllCount = 0
foreach ($dll in Get-ChildItem -LiteralPath (Split-Path -Parent $Executable) -Filter *.dll -File) {
    Copy-Item -LiteralPath $dll.FullName -Destination (Join-Path $package $dll.Name)
    ++$dllCount
}
# 設定・素材・エンジン組み込みの素材
$configCount = Copy-Tree (Join-Path $ProjectRoot 'Config') (Join-Path $package 'Config') @('autoexec.cfg')
$assetCount = Copy-Tree (Join-Path $ProjectRoot 'Assets') (Join-Path $package 'Assets') @('.gitkeep')
$engineCount = Copy-Tree (Join-Path $frameworkRoot 'EngineContent') (Join-Path $package 'EngineContent') @('README.md')
if ($engineCount -eq 0) { throw "EngineContent was not found in the framework folder: $frameworkRoot" }

# ライセンス
$licenses = Join-Path $package 'Licenses'
New-Item -ItemType Directory -Force -Path $licenses | Out-Null
$licenseFiles = [ordered]@{
    'mazigatiDX-LICENSE.txt'    = 'LICENSE'
    'THIRD_PARTY_NOTICES.md'    = 'THIRD_PARTY_NOTICES.md'
    'Jolt-LICENSE.txt'          = 'VendorModules\Jolt\LICENSE'
    'ImGui-LICENSE.txt'         = 'VendorModules\ImGui\LICENSE.txt'
    'SDL3-LICENSE.txt'          = 'VendorModules\SDL3\LICENSE.txt'
    'nlohmann-json-LICENSE.txt' = 'VendorModules\nlohmann\LICENSE.MIT'
    'ufbx-LICENSE.txt'          = 'VendorModules\ufbx\LICENSE'
}
foreach ($entry in $licenseFiles.GetEnumerator()) {
    $from = Join-Path $frameworkRoot $entry.Value
    if (-not (Test-Path -LiteralPath $from)) { throw "A license file is missing in the framework folder: $from" }
    Copy-Item -LiteralPath $from -Destination (Join-Path $licenses $entry.Key)
}
# ゲーム自身のライセンス（プロジェクトのルートに LICENSE があれば）
if ($ProjectRoot -ne $frameworkRoot) {
    foreach ($own in @('LICENSE', 'LICENSE.txt', 'LICENSE.md')) {
        $from = Join-Path $ProjectRoot $own
        if (Test-Path -LiteralPath $from) { Copy-Item -LiteralPath $from -Destination (Join-Path $licenses ("$Name-" + $own)); break }
    }
}

$versionLine = if ($Version -ne '') { " $Version" } else { '' }
$readme = @"
$Name$versionLine

How to start
  Run $Name.exe in this folder. Keep the folders next to it (Config, Assets, EngineContent): the game reads them from here.
  Save data, logs and screenshots are written to the Saved folder that the game makes here.

Requirements
  Windows 10 / 11 (64-bit) and graphics that run DirectX 11.

Licenses
  This game is made with mazigatiDX and DxLib. The licenses and the copyright notices of the software it uses are in the Licenses folder
  (THIRD_PARTY_NOTICES.md lists them, with the notices that DxLib requires).
"@
[IO.File]::WriteAllText((Join-Path $package 'README.txt'), ($readme -replace "`r?`n", "`r`n"), (New-Object System.Text.UTF8Encoding($false)))

# 確認: 配布物のフォルダーだけで、動くか（画面を出さずに、30 フレーム）。確認で出来た Saved などは、消す
if (-not $NoVerify) {
    $before = @(Get-ChildItem -LiteralPath $package -Force | ForEach-Object { $_.Name })
    $process = Start-Process -FilePath (Join-Path $package ($Name + '.exe')) -ArgumentList '--backend=null', '--max-frames=30' -WorkingDirectory $package -PassThru -WindowStyle Hidden
    if (-not $process.WaitForExit(120000)) {
        $process.Kill()
        throw 'The packaged game did not finish in 120 seconds (--backend=null --max-frames=30).'
    }
    $code = $process.ExitCode
    foreach ($item in Get-ChildItem -LiteralPath $package -Force) {
        if ($before -notcontains $item.Name) { Remove-Item -LiteralPath $item.FullName -Recurse -Force }
    }
    if ($code -ne 0) { throw "The packaged game exited with code $code (--backend=null --max-frames=30)." }
}

if ($Zip) {
    Compress-Archive -Path $package -DestinationPath $zipPath -CompressionLevel Optimal
}

$size = (Get-ChildItem -LiteralPath $package -Recurse -File | Measure-Object -Property Length -Sum).Sum
Write-Host ("Packaged {0}: 1 executable, {1} DLL(s), {2} config file(s), {3} asset file(s), {4} engine content file(s), {5} license file(s), {6:N1} MB" -f $packageName, $dllCount, $configCount, $assetCount, $engineCount, @(Get-ChildItem -LiteralPath $licenses -File).Count, ($size / 1MB))
Write-Host ("  folder: $package")
if ($Zip) { Write-Host ("  zip:    $zipPath") }
if ($NoVerify) { Write-Host '  (not verified: -NoVerify)' } else { Write-Host '  verified: the packaged game ran 30 frames with --backend=null' }
