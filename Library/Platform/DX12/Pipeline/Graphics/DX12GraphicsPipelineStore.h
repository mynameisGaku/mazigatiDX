// SPDX-License-Identifier: MIT
#pragma once
#include "Library/Core/Interfaces/Render/IGraphicsPipelineResources.h"
#include "Library/Core/Collection/Map.h"
#include "Library/Platform/System/WindowsInclude.h"
#include <d3d12.h>
#include <wrl/client.h>
#include "DX12GraphicsPipelineBinding.h"

// カスタムグラフィックスパイプラインの検証・所有・遅延破棄を担当する。ImGui文脈に依存しない。
class CDX12GraphicsPipelineStore : public IGraphicsPipelineResources
{
  public:
	// 所有するGPU状態は複写しない。
	CDX12GraphicsPipelineStore() = default;
	CDX12GraphicsPipelineStore( const CDX12GraphicsPipelineStore & ) = delete;
	CDX12GraphicsPipelineStore &operator=( const CDX12GraphicsPipelineStore & ) = delete;
	// 所有者はGPU完了またはデバイス喪失を保証してから破棄する。
	~CDX12GraphicsPipelineStore() override;
	// 作成先のdeviceを固定する。二重初期化・無効deviceはfalse。
	bool Initialize( ID3D12Device *InDevice );
	// GPU完了後に登録解除済みの所有物だけを戻す。未完了提出がある間は呼ばない。
	void CollectAfterGPUIdle();
	// GPU完了またはデバイス喪失後に全所有物を戻す。番号は再利用しない。
	void ResetAfterGPUIdle();
	// 呼び出し側と同じdeviceへ接続されているか。
	bool UsesDevice( ID3D12Device *InDevice ) const;
	// VS／PS DXBCと入力・スロット・出力描画先・状態を照合して登録する。失敗時は無効値。
	FGraphicsPipelineHandle CreateGraphicsPipeline( const FGraphicsPipelineDesc &Desc ) override;
	// 番号を直ちに無効にし、GPU完了後にPSOとRootSignatureを解放する。
	void ReleaseGraphicsPipeline( FGraphicsPipelineHandle Pipeline ) override;
	// 描画記録時に参照できる実体を取得する。無効・解放済み・device異常時はOutを保ってfalse。
	bool AcquirePipeline( FGraphicsPipelineHandle Pipeline, FDX12GraphicsPipelineBinding &Out ) const;

  private:
	Microsoft::WRL::ComPtr<ID3D12Device> Device; // 作成先のGPU。
	TMap<uint64, FDX12GraphicsPipelineBinding> Pipelines; // 有効な番号と所有PSO。
	TArray<FDX12GraphicsPipelineBinding> Retired; // GPU完了まで保持する登録解除済みPSO。
	static constexpr int32 MaxPipelines = 64; // 解除待ちを含む同時保持上限。
	static constexpr uint32 MaxBytecodeBytes = 4u * 1024u * 1024u; // 1シェーダーの上限。
	static constexpr int32 MaxInputElements = 16; // 頂点属性の上限。
	static constexpr int32 MaxSlots = 16; // バッファと画像の合計スロット上限。
	static constexpr uint32 MaxVertexStride = 2048; // 1頂点バイト幅の上限。
};
