# SPDX-License-Identifier: MIT
# ゲーム描画・GPU・OS文字・UIの依存が責務の境界を越えないか確認する。
param([string]$Root = '')
$ErrorActionPreference = 'Stop'
$rootPath = if ($Root) { [IO.Path]::GetFullPath($Root) } else { [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..')) } # 検査する作業コピー。
$groups = @(
    @{ Directory = 'Library/Framework/Render/Thread'; Names = @('RenderCommandThread', 'ThreadedRenderSurface'); Forbidden = '(?i)imgui|dxlib|Platform/|d3d|dxgi|WindowsInclude|wrl/' },
    @{ Directory = 'Library/Platform/DX12/Render/Capture'; Names = @('DX12CaptureTarget'); Forbidden = '(?i)imgui|dxlib|DX12Device|Framework/|Core/Interfaces/(Asset|Platform)|IRenderSurface' },
    @{ Directory = 'Library/Platform/DX12/Resource/Readback'; Names = @('DX12TextureReadback'); Forbidden = '(?i)imgui|dxlib|DX12Device|Framework/|Core/Interfaces/(Asset|Platform)|IRenderSurface' },
    @{ Directory = 'Library/Platform/DX12/Resource/Timestamp'; Names = @('DX12TimestampStore'); Forbidden = '(?i)imgui|dxlib|DX12Device|Framework/|Core/Interfaces/(Asset|Platform)|IRenderSurface' },
    @{ Directory = 'Library/Platform/DX12/Device'; Names = @('DX12QueueCompletion'); Forbidden = '(?i)imgui|dxlib|DX12Device|Framework/|Core/Interfaces/(Asset|Platform)|IRenderSurface' },
    @{ Directory = 'Library/Platform/DX12/Render/Geometry'; Names = @('DX12GeometryRenderer'); Forbidden = '(?i)imgui|dxlib|Framework/|Platform/Windows' },
    @{ Directory = 'Library/Platform/DX12/Pipeline/Geometry'; Names = @('DX12GeometryPipelineStore'); Forbidden = '(?i)imgui|dxlib|Framework/|Platform/Windows|IRenderSurface|DX12Device|DX12GeometryRenderer|DX12GameRenderer|DX12BufferStore' },
    @{ Directory = 'Library/Platform/DX12/Pipeline/Graphics'; Names = @('DX12GraphicsPipelineStore'); Forbidden = '(?i)imgui|dxlib|Framework/|Platform/Windows|IRenderSurface|DX12Device|DX12GeometryRenderer|DX12GameRenderer|DX12BufferStore' },
    @{ Directory = 'Library/Platform/DX12/Render/Target'; Names = @('DX12RenderTargetPass'); Forbidden = '(?i)imgui|dxlib|Framework/|Platform/Windows' },
    @{ Directory = 'Library/Platform/DX12/Render/Game'; Names = @('DX12GameRenderer', 'DX12GameCapture', 'DX12GameSurface'); Forbidden = '(?i)imgui|dxlib|Framework/|Platform/Windows' },
    @{ Directory = 'Library/Platform/Render/Canvas'; Names = @('GeometryCanvas2D'); Forbidden = '(?i)imgui|dxlib|Framework/|Platform/(DX12|Windows)|d3d|dxgi|WindowsInclude|wrl/' },
    @{ Directory = 'Library/Platform/Windows/Text'; Names = @('WindowsTextRasterizer'); Forbidden = '(?i)imgui|dxlib|Framework/|Platform/DX12|d3d|dxgi|wrl/' },
    @{ Directory = 'Library/Platform/ImGui/Render'; Names = @('ImGuiCanvas2D'); Forbidden = '(?i)Platform/(DX12|DxLib|Windows)|d3d\d|dxgi|wincodec|WindowsInclude|imgui_impl_|windows\.h|wrl/' }
) # 資源・パイプライン・描画記録・OS文字・UIは必要な参照だけを持つ。
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
                elseif ($group.Directory -ne 'Library/Platform/ImGui/Render' -and ($text -replace '//.*$', '') -match '\b(ImGui\w*|ImDraw\w*|ImTexture\w*)\b')
                {
                    Write-Output "Forbidden UI type: ${relative}:$line $text"
                    $failed = $true
                }
            }

        }
    }
}
if ($failed) { exit 1 }
Write-Output 'GAME RENDER DEPENDENCIES PASS'
