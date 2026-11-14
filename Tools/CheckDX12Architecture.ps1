# SPDX-License-Identifier: MIT
# DX12と直結する描画型を、機能C／データFと責務ごとの配置で保つ。
param([string]$Root = '')
$ErrorActionPreference = 'Stop'
$rootPath = if ($Root) { [IO.Path]::GetFullPath($Root) } else { [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..')) } # 今回検査する作業コピー。
$types = @(
    @{ Path = 'Library/Platform/DX12/Render/Game/DX12GameSurface.h'; Type = 'CDX12GameSurface'; Role = '機能' },
    @{ Path = 'Library/Core/Render/RenderCommandContext.h'; Type = 'FRenderCommandContext'; Role = 'データ' },
    @{ Path = 'Library/Framework/Render/Thread/RenderCommandThread.h'; Type = 'CRenderCommandThread'; Role = '機能' },
    @{ Path = 'Library/Framework/Render/Thread/ThreadedRenderSurface.h'; Type = 'CThreadedRenderSurface'; Role = '機能' },
    @{ Path = 'Library/Platform/DX12/Command/Compute/DX12ComputeQueue.h'; Type = 'CDX12ComputeQueue'; Role = '機能' },
    @{ Path = 'Library/Platform/DX12/Device/DX12Device.h'; Type = 'CDX12Device'; Role = '機能' },
    @{ Path = 'Library/Platform/DX12/Device/DX12QueueCompletion.h'; Type = 'CDX12QueueCompletion'; Role = '機能' },
    @{ Path = 'Library/Platform/DX12/ImGui/Frame/DX12ImGuiFrameRenderer.h'; Type = 'CDX12ImGuiFrameRenderer'; Role = '機能' },
    @{ Path = 'Library/Platform/DX12/ImGui/Render/DX12ImGuiRenderer.h'; Type = 'CDX12ImGuiRenderer'; Role = '機能' },
    @{ Path = 'Library/Platform/DX12/Pipeline/Compute/DX12ComputePipelineStore.h'; Type = 'CDX12ComputePipelineStore'; Role = '機能' },
    @{ Path = 'Library/Platform/DX12/Pipeline/Geometry/DX12GeometryPipelineStore.h'; Type = 'CDX12GeometryPipelineStore'; Role = '機能' },
    @{ Path = 'Library/Platform/DX12/Pipeline/Graphics/DX12GraphicsPipelineStore.h'; Type = 'CDX12GraphicsPipelineStore'; Role = '機能' },
    @{ Path = 'Library/Platform/DX12/Render/Capture/DX12CaptureTarget.h'; Type = 'CDX12CaptureTarget'; Role = '機能' },
    @{ Path = 'Library/Platform/DX12/Render/Game/DX12GameCapture.h'; Type = 'CDX12GameCapture'; Role = '機能' },
    @{ Path = 'Library/Platform/DX12/Render/Game/DX12GameRenderer.h'; Type = 'CDX12GameRenderer'; Role = '機能' },
    @{ Path = 'Library/Platform/DX12/Render/Geometry/DX12GeometryRenderer.h'; Type = 'CDX12GeometryRenderer'; Role = '機能' },
    @{ Path = 'Library/Platform/DX12/Render/Target/DX12RenderTargetPass.h'; Type = 'CDX12RenderTargetPass'; Role = '機能' },
    @{ Path = 'Library/Platform/DX12/Resource/Buffer/DX12BufferStore.h'; Type = 'CDX12BufferStore'; Role = '機能' },
    @{ Path = 'Library/Platform/DX12/Resource/Descriptor/DX12DescriptorPool.h'; Type = 'CDX12DescriptorPool'; Role = '機能' },
    @{ Path = 'Library/Platform/DX12/Resource/Readback/DX12TextureReadback.h'; Type = 'CDX12TextureReadback'; Role = '機能' },
    @{ Path = 'Library/Platform/DX12/Resource/Texture/DX12TextureStore.h'; Type = 'CDX12TextureStore'; Role = '機能' },
    @{ Path = 'Library/Platform/DX12/Resource/Timestamp/DX12TimestampStore.h'; Type = 'CDX12TimestampStore'; Role = '機能' },
    @{ Path = 'Library/Platform/DX12/Resource/Upload/DX12UploadArena.h'; Type = 'CDX12UploadArena'; Role = '機能' },
    @{ Path = 'Library/Platform/ImGui/Render/ImGuiCanvas2D.h'; Type = 'CImGuiCanvas2D'; Role = '機能' },
    @{ Path = 'Library/Platform/ImGui/Render/ImGuiViewportScheduler.h'; Type = 'CImGuiViewportScheduler'; Role = '機能' },
    @{ Path = 'Library/Platform/Render/Canvas/CanvasDrawExecutor.h'; Type = 'CCanvasDrawExecutor'; Role = '機能' },
    @{ Path = 'Library/Platform/Render/Canvas/GeometryCanvas2D.h'; Type = 'CGeometryCanvas2D'; Role = '機能' },
    @{ Path = 'Library/Platform/Render/Geometry/GeometryBufferStore.h'; Type = 'CGeometryBufferStore'; Role = '機能' },
    @{ Path = 'Library/Platform/Render/Geometry/GeometryCommandExecutor.h'; Type = 'CGeometryCommandExecutor'; Role = '機能' },
    @{ Path = 'Library/Platform/Render/Geometry/GeometryQuadBuilder.h'; Type = 'CGeometryQuadBuilder'; Role = '機能' },
    @{ Path = 'Library/Platform/Render/Geometry/GeometryShapeBuilder.h'; Type = 'CGeometryShapeBuilder'; Role = '機能' },
    @{ Path = 'Library/Platform/Render/Model/StaticModelStore.h'; Type = 'CStaticModelStore'; Role = '機能' },
    @{ Path = 'Library/Platform/Render/Texture/TextureAssetStore.h'; Type = 'CTextureAssetStore'; Role = '機能' },
    @{ Path = 'Library/Platform/Windows/Text/WindowsTextRasterizer.h'; Type = 'CWindowsTextRasterizer'; Role = '機能' },
    @{ Path = 'Library/Platform/DX12/Resource/Buffer/DX12BufferBinding.h'; Type = 'FDX12BufferBinding'; Role = 'データ' },
    @{ Path = 'Library/Platform/DX12/Resource/Texture/DX12TextureBinding.h'; Type = 'FDX12TextureBinding'; Role = 'データ' },
    @{ Path = 'Library/Platform/DX12/Pipeline/Compute/DX12ComputePipelineBinding.h'; Type = 'FDX12ComputePipelineBinding'; Role = 'データ' },
    @{ Path = 'Library/Platform/DX12/Pipeline/Graphics/DX12GraphicsPipelineBinding.h'; Type = 'FDX12GraphicsPipelineBinding'; Role = 'データ' },
    @{ Path = 'Library/Core/Render/GeometryBufferDraw.h'; Type = 'FGeometryBufferDraw'; Role = 'データ' },
    @{ Path = 'Library/Core/Render/GpuTimestampCapabilities.h'; Type = 'FGpuTimestampCapabilities'; Role = 'データ' },
    @{ Path = 'Library/Core/Render/GpuTimestampHandle.h'; Type = 'FGpuTimestampHandle'; Role = 'データ' },
    @{ Path = 'Library/Core/Render/GpuTimestampResult.h'; Type = 'FGpuTimestampResult'; Role = 'データ' },
    @{ Path = 'Library/Core/Render/GraphicsBufferSlot.h'; Type = 'FGraphicsBufferSlot'; Role = 'データ' },
    @{ Path = 'Library/Core/Render/GraphicsInputElement.h'; Type = 'FGraphicsInputElement'; Role = 'データ' },
    @{ Path = 'Library/Core/Render/GraphicsPipelineDesc.h'; Type = 'FGraphicsPipelineDesc'; Role = 'データ' },
    @{ Path = 'Library/Core/Render/GraphicsPipelineDraw.h'; Type = 'FGraphicsPipelineDraw'; Role = 'データ' },
    @{ Path = 'Library/Core/Render/GraphicsPipelineHandle.h'; Type = 'FGraphicsPipelineHandle'; Role = 'データ' },
    @{ Path = 'Library/Core/Render/GraphicsTextureSlot.h'; Type = 'FGraphicsTextureSlot'; Role = 'データ' },
    @{ Path = 'Library/Core/Render/ComputeBufferSlot.h'; Type = 'FComputeBufferSlot'; Role = 'データ' },
    @{ Path = 'Library/Core/Render/ComputeTextureCapabilities.h'; Type = 'FComputeTextureCapabilities'; Role = 'データ' },
    @{ Path = 'Library/Core/Render/ComputeTextureSlot.h'; Type = 'FComputeTextureSlot'; Role = 'データ' },
    @{ Path = 'Library/Core/Render/RenderTargetCapabilities.h'; Type = 'FRenderTargetCapabilities'; Role = 'データ' },
    @{ Path = 'Library/Core/Render/RenderTargetFormat.h'; Type = 'FRenderTargetColorAttachment'; Role = 'データ' },
    @{ Path = 'Library/Core/Render/RenderTargetPassDesc.h'; Type = 'FRenderTargetPassDesc'; Role = 'データ' }
) # class／structの構文でなく、所有する責務で分類した主要型。
$failed = $false # すべての問題をまとめて報告する。
foreach ($type in $types)
{
    $path = Join-Path $rootPath $type.Path # 役割と配置が確定したヘッダー。
    if (-not (Test-Path -LiteralPath $path))
    {
        Write-Output "Missing architecture source: $($type.Path)"
        $failed = $true
        continue
    }
    $code = [regex]::Replace([IO.File]::ReadAllText($path), '//[^\r\n]*|/\*[\s\S]*?\*/', ' ') # 説明文で型の不在を隠さない。
    $name = [regex]::Escape($type.Type) # 型の宣言だけを調べる。
    if ($code -notmatch "\b(class|struct)\s+$name\b")
    {
        Write-Output "Invalid role prefix: $($type.Path) requires $($type.Type) ($($type.Role))"
        $failed = $true
    }
}
foreach ($relative in @('Library/Platform/DX12/Render', 'Library/Platform/DX12/ImGui'))
{
    $directory = Join-Path $rootPath $relative # 直下への実装の再集積を検出する場所。
    if (Test-Path -LiteralPath $directory)
    {
        foreach ($file in Get-ChildItem -LiteralPath $directory -File | Where-Object { $_.Extension -in @('.h', '.cpp', '.inl') })
        {
            Write-Output "Unclassified DX12 source: $relative/$($file.Name)"
            $failed = $true
        }
    }
}
if ($failed) { exit 1 }
Write-Output "DX12 ARCHITECTURE PASS ($($types.Count) role types)"
