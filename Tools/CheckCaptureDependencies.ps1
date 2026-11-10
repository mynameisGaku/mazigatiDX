# SPDX-License-Identifier: MIT
# GPU撮影先とImGuiの撮影記録が、接続層を飛び越えて依存しないか確認する。
param([string]$Root = '')
$ErrorActionPreference = 'Stop'
$rootPath = if ($Root) { [IO.Path]::GetFullPath($Root) } else { [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..')) } # 検査する作業コピー。
$groups = @(
    @{ Directory = 'Library/Platform/DX12/Render'; Names = @('DX12CaptureTarget', 'DX12TextureReadback', 'DX12QueueCompletion'); Forbidden = '(?i)imgui|dxlib|DX12Device|Framework/|Core/Interfaces/(Asset|Platform)|IRenderSurface' },
    @{ Directory = 'Library/Platform/ImGui/Render'; Names = @('ImGuiCaptureDrawList', 'ImGuiCanvas2D'); Forbidden = '(?i)Platform/(DX12|DxLib|Windows)|d3d\d|dxgi|wincodec|WindowsInclude|imgui_impl_|windows\.h|wrl/' }
) # 両方を知る実装はDX12/ImGuiの接続層へ置く。
$failed = $false # 見つかった違反を一度に報告する。
foreach ($group in $groups)
{
    foreach ($name in $group.Names)
    {
        foreach ($extension in @('.h', '.cpp'))
        {
            $relative = "$($group.Directory)/$name$extension" # 所有する実装だけを対象にする。
            $path = Join-Path $rootPath $relative # 第三者のコードは書き換えない。
            $line = 0 # 違反の場所を示す行番号。
            foreach ($text in [IO.File]::ReadAllLines($path))
            {
                ++$line
                if ($text -match '^\s*#\s*include\s*[<"]([^>"]+)[>"]' -and $Matches[1].Replace('\', '/') -match $group.Forbidden)
                {
                    Write-Output "Forbidden include: ${relative}:$line $text"
                    $failed = $true
                }
            }
        }
    }
}
if ($failed) { exit 1 }
Write-Output 'CAPTURE DEPENDENCIES PASS'
