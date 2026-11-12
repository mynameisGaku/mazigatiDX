// SPDX-License-Identifier: MIT
#pragma once
#include "Library/Core/Types/TypeDefinition.h"
#include "Library/Core/Render/ComputeTextureFormat.h"
#include "Library/Math/Color.h"
#include "Library/Platform/System/WindowsInclude.h"
#include <d3d12.h>
#include <wrl/client.h>

// compute提出や描画先パスが画像の更新・登録解除後も保持する実体。描画用SRVの借用とは分ける。
struct FDX12TextureBinding
{
	// GPU処理の完了まで保持する画像。
	Microsoft::WRL::ComPtr<ID3D12Resource> Resource;
	// 描画先RTVまたは深度DSVのCPU記述子ヒープ（通常画像ではnullptr）。
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> TargetHeap;
	// 描画用SRVのGPU記述子（深度では0）。自己参照競合の検査にも使う。
	uint64 SrvGpuDescriptor = 0;
	// 検査済みの画像の寸法。
	int32 Width = 0, Height = 0;
	// 作成時に指定した初期消去色（色描画先の場合）。
	FColor32 InitialClearColor = FColor32( 0, 0, 0, 255 );
	// 作成時に指定した初期深度（読み取れる深度の場合）。
	float32 InitialClearDepth = 1.0f;
	// computeシェーダーと照合する画像の形式。
	EComputeTextureFormat Format = EComputeTextureFormat::RGBA8;
};
