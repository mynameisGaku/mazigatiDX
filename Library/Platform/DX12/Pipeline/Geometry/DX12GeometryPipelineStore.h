// SPDX-License-Identifier: MIT
#pragma once
#include "Library/Core/Render/GeometryDrawState.h"
#include "Library/Core/Collection/Map.h"
#include "Library/Platform/System/WindowsInclude.h"
#include <d3d12.h>
#include <d3dcompiler.h>
#include <wrl/client.h>

// 描画状態から決まる、保管の見出し。利用者が決めた混ぜ方（Custom）の係数は、別の整数に入れる（0 なら、使っていない）。
struct FGeometryPipelineKey
{
	uint64 State = 0;
	uint32 Custom = 0;
	bool operator==( const FGeometryPipelineKey &Other ) const { return State == Other.State && Custom == Other.Custom; }
};
inline uint32 GetTypeHash( const FGeometryPipelineKey &Key ) { return HashCombine( GetTypeHash( Key.State ), GetTypeHash( Key.Custom ) ); }

// 固定の同次頂点用シェーダー・資源配置・描画状態を作成し、GPU完了まで所有する。
class CDX12GeometryPipelineStore
{
  public:
	// 同じGPU状態の所有者を複写しない。
	CDX12GeometryPipelineStore() = default;
	CDX12GeometryPipelineStore( const CDX12GeometryPipelineStore & ) = delete;
	CDX12GeometryPipelineStore &operator=( const CDX12GeometryPipelineStore & ) = delete;
	// 所有者がGPU完了を保証してから破棄する。
	~CDX12GeometryPipelineStore() = default;
	// 同じdeviceで頂点色と画像のシェーダーを用意する。二重初期化・作成失敗はfalse。
	bool Initialize( ID3D12Device *InDevice );
	// 状態に合うPSOを取得または作成する。未初期化・不正値・128個超過・GPU失敗はnullptr。
	ID3D12PipelineState *GetOrCreatePipeline( const FGeometryDrawState &State, bool bTextured );
	// 描画時に資源配置を借りる。未初期化ではnullptr、参照はResetまで有効。
	ID3D12RootSignature *GetRootSignature() const;
	// GPU完了または喪失後に所有物を戻す。以後は再初期化が必要。
	void ResetAfterGPUIdle();

  private:
	// ComPtrの取得演算子をコンテナ操作と区別する、所有参照の値。
	struct FPipelineRecord
	{
		Microsoft::WRL::ComPtr<ID3D12PipelineState> Resource; // 状態ごとの作成済みPSO。
	};
	Microsoft::WRL::ComPtr<ID3D12Device> Device; // 作成先のGPU。
	Microsoft::WRL::ComPtr<ID3D12RootSignature> RootSignature; // t0画像とs0補間、マスクのt1とb0の配置。
	Microsoft::WRL::ComPtr<ID3DBlob> VertexShader, PixelShader, TexturedPixelShader; // 固定の28byte頂点を読む三つのバイナリ。
	Microsoft::WRL::ComPtr<ID3DBlob> InvertedPixelShader, InvertedTexturedPixelShader, InvertedMaskedPixelShader, InvertedMaskedTexturedPixelShader; // 描く色を反転する（Blend が Invert）四つ。
	Microsoft::WRL::ComPtr<ID3DBlob> MaskedPixelShader, MaskedTexturedPixelShader; // 2Dのマスク（t1の画像の値で薄める）つきの二つ。
	TMap<FGeometryPipelineKey, FPipelineRecord> Pipelines; // 深度・表裏・合成・画像・つなぎ方ごとのキャッシュ。
};
