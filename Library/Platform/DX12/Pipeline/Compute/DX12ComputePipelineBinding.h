// SPDX-License-Identifier: MIT
#pragma once
#include "Library/Core/Collection/Array.h"
#include "Library/Core/Render/ComputeBufferSlot.h"
#include "Library/Core/Render/ComputeTextureSlot.h"
#include "Library/Platform/System/WindowsInclude.h"
#include <d3d12.h>
#include <wrl/client.h>

// 提出済みの命令がプログラム解放後も保持する実体。API共通層へは公開しない。
struct FDX12ComputePipelineBinding
{
	Microsoft::WRL::ComPtr<ID3D12PipelineState> Pipeline; // GPUの実行状態。
	Microsoft::WRL::ComPtr<ID3D12RootSignature> Root; // slotからCBV／SRV／UAVへの対応。
	TArray<FComputeBufferSlot> Slots; // 記録時に照合する宣言の所有コピー。
	TArray<FComputeTextureSlot> TextureSlots; // バッファの後ろへ並べる画像宣言の所有コピー。
	TArray<uint32> MinimumBytes; // 定数でシェーダーが宣言した最小バイト数。その他は0。
};
