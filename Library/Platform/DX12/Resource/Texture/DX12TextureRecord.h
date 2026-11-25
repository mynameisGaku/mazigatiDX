// SPDX-License-Identifier: MIT
#pragma once
#include "DX12TextureBinding.h"
#include "Library/Core/Render/ComputeTextureFormat.h"
#include "Library/Core/Render/TextureMipMode.h"
#include "Library/Core/Types/TypeDefinition.h"
#include "Library/Math/Color.h"
#include "Library/Platform/DX12/Resource/Descriptor/DX12DescriptorPool.h"
#include "Library/Platform/System/WindowsInclude.h"
#include <d3d12.h>
#include <wrl/client.h>

// 同じハンドル番号空間で所有する画像資源の用途。
enum class EDX12TextureKind : uint8
{
	StandardRGBA8, // CPUから転送した読み取り用のRGBA8画像。
	ComputeRGBA8, // UAV書き込みも許可するRGBA8画像。
	RenderTargetRGBA8, // オフスクリーン描画先として使えるRGBA8画像。
	ComputeRGBA16F, // UAV読み書きも許可するRGBA16F HDR画像。
	RenderTargetRGBA16F, // オフスクリーン描画先として使えるRGBA16F HDR画像。
	RenderTargetR32Float, // オフスクリーン描画先として使えるR32Float単一成分カラー画像。
	DepthD32, // 描画パスとcompute R32Float読み取りで使える深度画像。
};

// 用途の分類（同じ判定を、複数の所有者が同じ意味で使う）。
namespace NDX12TextureKind
{
	// RGBA8 の色画像か（通常・compute・描画先）。
	bool IsRGBA8( EDX12TextureKind Kind );
	// RGBA16F の色画像か（compute・描画先）。
	bool IsRGBA16F( EDX12TextureKind Kind );
	// R32Float として読める画像か（R32Float 描画先・深度）。
	bool IsR32Float( EDX12TextureKind Kind );
}

// 登録した画像 1 枚の実体（GPU 資源・記述子・寸法・作成時の消去値）。COM参照を値へ包み、コンテナからoperator&を呼ばせない。
struct FDX12TextureRecord
{
	Microsoft::WRL::ComPtr<ID3D12Resource> Resource; // GPUが読む・描く画像。
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> TargetHeap; // 描画先RTVまたは深度DSVのCPUヒープ。
	CDX12DescriptorPool::FAllocation Descriptor; // GPU完了まで保持するSRV（深度では未使用）。
	int32 Width = 0, Height = 0; // 画像の寸法。
	int32 MipLevels = 1; // ミップマップの段の数（元の画像を含む）。
	ETextureMipMode MipMode = ETextureMipMode::Color; // 更新時にも使う縮小段の作り方。
	FColor32 InitialClearColor = FColor32( 0, 0, 0, 255 ); // 作成時に設定した初期消去色。
	FColor InitialClearColorF = FColor( 0.0f, 0.0f, 0.0f, 1.0f ); // RGBA16F作成時に設定したHDR初期消去色。
	float32 InitialClearDepth = 1.0f; // 作成時に設定した初期深度。
	EDX12TextureKind Kind = EDX12TextureKind::StandardRGBA8; // 用途の区別。

	// 提出や描画先パスが、更新・登録解除のあとも保持する参照を作る（検査は呼び出し側が済ませる）。
	FDX12TextureBinding MakeBinding( EComputeTextureFormat Format ) const;
};
