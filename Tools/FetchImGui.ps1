# Dear ImGui (docking ブランチのリリースタグ) を固定版・固定ハッシュで取得し、本体とバックエンドを VendorModules/ImGui へ配置する。
# 自前のファイル（imgui_impl_dxlib.hpp / imgui_ja_gryph_ranges.cpp など）は上書きしない。
#
# 版を上げるとき: $imguiTag を新しいタグ（例: v1.93.1-docking）にして $archiveHash を空にして実行すると、
# 取得したアーカイブのハッシュが表示されるので、内容を確認したうえで $archiveHash に書き戻す。
$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'
$projectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$imguiTag = 'v1.93.0-docking'
$archiveHash = '302CCB19CF6FF803EBC970B4F96D59C95DD3A32169453AD5C503CAB67C057992'
$destination = Join-Path $projectRoot 'VendorModules\ImGui'
$versionFile = Join-Path $destination 'VERSION.txt'

# 本体（リポジトリ直下）と、使うバックエンド（backends 配下）。imgui_demo.cpp は使わないので入れない。
$coreFiles = @('imgui.h', 'imgui.cpp', 'imgui_internal.h', 'imgui_draw.cpp', 'imgui_tables.cpp', 'imgui_widgets.cpp',
               'imstb_rectpack.h', 'imstb_textedit.h', 'imstb_truetype.h', 'imconfig.h', 'LICENSE.txt')
$backendFiles = @('imgui_impl_win32.h', 'imgui_impl_win32.cpp', 'imgui_impl_dx11.h', 'imgui_impl_dx11.cpp',
                  'imgui_impl_dx9.h', 'imgui_impl_dx9.cpp')

if ((Test-Path -LiteralPath $versionFile) -and ((Get-Content -LiteralPath $versionFile -Raw).Trim() -eq $imguiTag)) {
    $missing = @($coreFiles + $backendFiles | Where-Object { -not (Test-Path -LiteralPath (Join-Path $destination $_)) })
    if ($missing.Count -eq 0) {
        Write-Host "Dear ImGui $imguiTag is already installed."
        exit 0
    }
}

$staging = Join-Path ([IO.Path]::GetTempPath()) ('FrameworkImGui_' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $staging | Out-Null
try {
    $archive = Join-Path $staging 'imgui.zip'
    Invoke-WebRequest -Uri "https://github.com/ocornut/imgui/archive/refs/tags/$imguiTag.zip" -OutFile $archive -UseBasicParsing
    $actualHash = (Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash
    if ($archiveHash -eq '') { Write-Host "Archive SHA256: $actualHash"; throw 'archiveHash is empty. Verify the archive, then set it in this script.' }
    if ($actualHash -ne $archiveHash) { throw "ImGui archive checksum mismatch ($actualHash)." }

    Expand-Archive -LiteralPath $archive -DestinationPath (Join-Path $staging 'unpacked')
    $package = Get-ChildItem -LiteralPath (Join-Path $staging 'unpacked') -Directory | Select-Object -First 1
    if ($null -eq $package) { throw 'ImGui archive is empty.' }

    $sources = @{}
    foreach ($name in $coreFiles) { $sources[$name] = Join-Path $package.FullName $name }
    foreach ($name in $backendFiles) { $sources[$name] = Join-Path $package.FullName ('backends\' + $name) }
    foreach ($name in $sources.Keys) {
        if (-not (Test-Path -LiteralPath $sources[$name])) { throw "Missing in archive: $name" }
    }

    New-Item -ItemType Directory -Force -Path $destination | Out-Null
    foreach ($name in $sources.Keys) { Copy-Item -LiteralPath $sources[$name] -Destination (Join-Path $destination $name) -Force }
    [IO.File]::WriteAllText($versionFile, $imguiTag + [Environment]::NewLine)
    Write-Host "Installed Dear ImGui $imguiTag. Run GenerateProjectFiles.bat next."
} finally {
    # 作成した一時ディレクトリーだけを消す。
    $resolvedStaging = [IO.Path]::GetFullPath($staging)
    $temporaryRoot = [IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd('\') + '\'
    if ($resolvedStaging.StartsWith($temporaryRoot, [StringComparison]::OrdinalIgnoreCase) -and (Split-Path $resolvedStaging -Leaf).StartsWith('FrameworkImGui_')) { Remove-Item -LiteralPath $resolvedStaging -Recurse -Force }
}
