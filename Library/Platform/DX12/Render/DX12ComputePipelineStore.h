// SPDX-License-Identifier: MIT
#pragma once
#include "Library/Core/Interfaces/Render/IComputePipelineResources.h"
#include "Library/Core/Collection/Map.h"
#include "DX12ComputePipelineBinding.h"

// DXBC computeプログラムとその配置を検査し、GPUの実行状態を所有する。
class FDX12ComputePipelineStore : public IComputePipelineResources
{
public:
	// 所有するプログラムの番号とGPU参照を複写しない。
	FDX12ComputePipelineStore() = default;
	FDX12ComputePipelineStore( const FDX12ComputePipelineStore& ) = delete;
	FDX12ComputePipelineStore& operator=( const FDX12ComputePipelineStore& ) = delete;
	// 所有者はGPU完了またはデバイス喪失を保証してから破棄する。
	~FDX12ComputePipelineStore() override;
	// プログラムの作成先を固定する。二重初期化・無効なデバイスはfalse。
	bool Initialize( ID3D12Device* InDevice );
	// 提出済みのGPU処理の完了後に、登録解除したプログラムだけを回収する。
	void CollectAfterGPUIdle();
	// GPU完了またはデバイス喪失後に全資源を戻し、以後は再初期化を必要とする。
	void ResetAfterGPUIdle();
	// バイナリ・段・slotの一致を確認してからGPU資源を作る。失敗時は無効値。
	FComputePipelineHandle CreateComputePipeline( TArrayView<const uint8> Bytecode, TArrayView<const FComputeBufferSlot> Slots ) override;
	// 番号だけを無効にし、実体は提出完了後へ解放を延期する。
	void ReleaseComputePipeline( FComputePipelineHandle Pipeline ) override;
	// 命令の記録先と同じデバイスを使っているか確認する。
	bool UsesDevice( ID3D12Device* InDevice ) const;
	// 有効なプログラムを完了まで保持できる参照として渡す。失敗時はOutを保つ。
	bool AcquirePipeline( FComputePipelineHandle Pipeline, FDX12ComputePipelineBinding& Out ) const;
private:
	// 宣言とバイナリの検査が終わってからGPUへ渡す。失敗時はOutを保つ。
	bool Build( TArrayView<const uint8> Bytecode, TArrayView<const FComputeBufferSlot> Slots, FDX12ComputePipelineBinding& Out );
	Microsoft::WRL::ComPtr<ID3D12Device> Device; // 実行状態を作るデバイス。
	TMap<uint64, FDX12ComputePipelineBinding> Pipelines; // 有効な番号ごとのGPU実体。
	TArray<FDX12ComputePipelineBinding> Retired; // GPU完了後まで残す登録解除済みの実体。
	static constexpr int32 MaxPipelines = 64; // 登録解除済みを含む保持数の上限。
	static constexpr int32 MaxBytecodeBytes = 4 * 1024 * 1024; // 一つのバイナリの上限。
	static constexpr int32 MaxSlots = 16; // 一つのプログラムに渡すバッファ数の上限。
};
