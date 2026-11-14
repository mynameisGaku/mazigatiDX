// SPDX-License-Identifier: MIT
#pragma once
#include "Library/Core/Interfaces/Render/IComputeQueue.h"
#include "Library/Platform/DX12/Resource/Buffer/DX12BufferStore.h"
#include "Library/Platform/DX12/Pipeline/Compute/DX12ComputePipelineStore.h"
#include "Library/Platform/DX12/Resource/Texture/DX12TextureStore.h"
class CDX12TimestampStore;

// compute専用の記録領域とサイズ付きの記述子を提出ごとに所有し、完了まで借用実体を保持する。
class CDX12ComputeQueue : public IComputeQueue
{
  public:
	// 提出先の識別番号と使用中のGPU参照は複写しない。
	CDX12ComputeQueue() = default;
	CDX12ComputeQueue( const CDX12ComputeQueue & ) = delete;
	CDX12ComputeQueue &operator=( const CDX12ComputeQueue & ) = delete;
	// GPU完了またはデバイス喪失後に所有者が破棄する。
	~CDX12ComputeQueue() override;
	// 同じデバイス・DIRECTキューの所有先だけを接続する。失敗時はfalse。
	bool Initialize( ID3D12Device *InDevice, ID3D12CommandQueue *InQueue, CDX12BufferStore &InBuffers, CDX12ComputePipelineStore &InPipelines, CDX12TextureStore *InTextures = nullptr, CDX12TimestampStore *InTimestamps = nullptr );
	// 同じキューの完了後だけ呼ぶ。資源所有先より先に提出時の参照を戻す。
	void CollectAfterGPUIdle();
	// GPU完了またはデバイス喪失後に全参照を戻し、過去の提出番号を無効にする。
	void ResetAfterGPUIdle();
	// 全入力を検査してから、寿命を保持した命令列を待たずに提出する。
	FComputeSubmission SubmitCompute( TArrayView<const FComputeDispatch> Dispatches ) override;
	// GPUタイムスタンプ計測ペアを前後へ積んで同じDIRECTキューへ提出する。
	FComputeSubmission SubmitComputeWithTimestamp( TArrayView<const FComputeDispatch> Dispatches, FGpuTimestampHandle Timestamp ) override;
	// 有効な提出の状態だけを返す。失敗時はOutCompletedを保つ。
	bool GetComputeCompletion( FComputeSubmission Submission, bool &OutCompleted ) override;
	// 完了済みなら直ちに成功する。タイムアウト時も提出資源を保持する。
	bool WaitCompute( FComputeSubmission Submission, uint32 TimeoutMs ) override;

  private:
	// 一命令の実体の所有コピー。記録後も参照を完了まで残す。
	struct FDispatchRecord
	{
		FDX12ComputePipelineBinding Pipeline; // 実行状態と配置の参照。
		TArray<FDX12BufferBinding> Buffers; // 登録解除後も保持する実体。
		TArray<FDX12TextureBinding> Textures; // 更新・登録解除後も保持する画像の版。
		uint32 GroupsX = 0, GroupsY = 0, GroupsZ = 0; // 検査済みのグループ数。
	};
	// コマンド領域・記述子・借用参照を一つの提出に固定する。
	struct FSubmissionRecord
	{
		Microsoft::WRL::ComPtr<ID3D12CommandAllocator> Allocator; // 提出が完了するまで再使用しない領域。
		Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> Commands; // 記録済みのcompute命令。
		Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> Descriptors; // この提出専用のSRV/UAV。
		TArray<FDispatchRecord> Dispatches; // 実体を保持する命令の配列。
		uint64 Value = 0; // GPUに通知する完了番号。
	};
	// 通常の提出とタイムスタンプ付き提出を共有経路で検証・実行する。
	FComputeSubmission SubmitComputeInternal( TArrayView<const FComputeDispatch> Dispatches, FGpuTimestampHandle Timestamp );
	// 全命令を検査してから記録する。失敗時はGPUへ提出せず、Outを保つ。
	bool Record( TArrayView<const FComputeDispatch> Dispatches, FSubmissionRecord &Out, const uint32 *TimestampBeginSlot = nullptr );
	// 完了済みだけを回収する。デバイス喪失時は保持したまま再使用を禁止する。
	bool CollectCompleted();
	// 現在の提出先から発行した番号かを確認する。
	bool IsKnown( FComputeSubmission Submission ) const;
	Microsoft::WRL::ComPtr<ID3D12Device> Device; // 記録領域を作るデバイス。
	Microsoft::WRL::ComPtr<ID3D12CommandQueue> Queue; // 描画・転送と同じ提出先。
	Microsoft::WRL::ComPtr<ID3D12Fence> Fence; // computeの完了番号。
	CDX12BufferStore *Buffers = nullptr; // より長く生存するバッファの所有先。
	CDX12ComputePipelineStore *Pipelines = nullptr; // より長く生存するプログラムの所有先。
	CDX12TextureStore *Textures = nullptr; // 任意接続の画像所有先。提出先より長く生存する。
	CDX12TimestampStore *Timestamps = nullptr; // 任意接続のGPUタイムスタンプ所有者。提出先より長く生存する。
	TArray<FSubmissionRecord> Pending; // 未完了の実体と記述子を保持する。
	TArray<uint64> Notifications; // 同じ未完了番号へ待機通知を重複登録しないための値。
	HANDLE FenceEvent = nullptr; // 待機用の自動解除イベント。
	uint64 QueueSerial = 0; // 再初期化で再利用しない提出先の識別番号。
	uint64 LastSubmitted = 0; // 最後に成功した通知番号。
	uint64 NextValue = 0; // 通知失敗時も再利用しない番号。
	bool bHealthy = false; // 提出・GPUに失敗した後は再使用しない。
	static constexpr int32 MaxPending = 8; // 同時に保持する未完了の提出数。
	static constexpr int32 MaxDispatches = 32; // 一度に提出するcompute命令数。
};
