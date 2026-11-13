// SPDX-License-Identifier: MIT
#pragma once
#include "Library/Core/Collection/Array.h"
#include "Library/Core/Render/GeometryDrawState.h"
#include "Library/Core/Render/GraphicsBufferSlot.h"
#include "Library/Core/Render/GraphicsPipelineDesc.h"
#include "Library/Core/Render/GraphicsTextureSlot.h"
#include "Library/Platform/System/WindowsInclude.h"
#include <d3d12.h>
#include <wrl/client.h>

// 描画命令がパイプライン解放後もGPU完了まで保持する実体。API共通層へは公開しない。
struct FDX12GraphicsPipelineBinding
{
	Microsoft::WRL::ComPtr<ID3D12PipelineState> Pipeline; // GPUの描画実行状態。
	Microsoft::WRL::ComPtr<ID3D12RootSignature> Root; // CBV／SRV／静的サンプラs0への対応。
	TArray<FGraphicsBufferSlot> BufferSlots; // 記録時に照合するバッファ宣言の所有コピー。
	TArray<FGraphicsTextureSlot> TextureSlots; // 記録時に照合する画像宣言の所有コピー。
	TArray<uint32> MinimumBytes; // BufferSlotsに対応。Constantのみシェーダー宣言の最小バイト数、他は0。
	uint32 VertexStride = 0; // 頂点バッファの1頂点バイト幅。
	uint32 NumRenderTargets = 1; // 同時色描画先の必須数（1〜8）。
	ERenderTargetColorFormat RenderTargetFormats[FGraphicsPipelineDesc::MaxRenderTargets] =
	{
		ERenderTargetColorFormat::RGBA8, ERenderTargetColorFormat::RGBA8, ERenderTargetColorFormat::RGBA8, ERenderTargetColorFormat::RGBA8,
		ERenderTargetColorFormat::RGBA8, ERenderTargetColorFormat::RGBA8, ERenderTargetColorFormat::RGBA8, ERenderTargetColorFormat::RGBA8
	}; // 各色描画先の必須画素形式。
	EGraphicsDepthFormat DepthFormat = EGraphicsDepthFormat::D32Float; // 結合すべきDSV形式の有無。
	EGeometryTopology Topology = EGeometryTopology::Triangles; // IASetPrimitiveTopologyへ渡す接続形式。
};
