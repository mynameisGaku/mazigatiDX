// SPDX-License-Identifier: MIT
#pragma once
#include "Library/Core/Collection/Array.h"
#include "Library/Core/Collection/Map.h"
#include "Library/Core/Interfaces/Render/IGpuTimestampResources.h"
#include "Library/Platform/System/WindowsInclude.h"
#include <d3d12.h>
#include <wrl/client.h>

// DIRECTキュー上のGPUタイムスタンプQueryHeap・Readback・遅延解放を所有する。UIとウィンドウを知らない。
class CDX12TimestampStore : public IGpuTimestampResources
{
public:
	// 同時に作成して保持できる未解放タイムスタンプ計測ペアの上限。
	static constexpr uint32 MaxActiveQueries = 256;

	// GPU資源とFenceの所有先を複写しない。
	CDX12TimestampStore() = default;
	CDX12TimestampStore( const CDX12TimestampStore& ) = delete;
	CDX12TimestampStore& operator=( const CDX12TimestampStore& ) = delete;
	// 所有側はGPU完了またはデバイス喪失を確認してから破棄する。
	~CDX12TimestampStore() override;

	// 同じデバイス・DIRECTキューへ接続し、周波数取得・QueryHeap・Readback・Fenceを構築する。
	bool Initialize( ID3D12Device* InDevice, ID3D12CommandQueue* InQueue );
	// 指定したデバイスとDIRECTキューを共有しているか返す。
	bool UsesDeviceAndQueue( ID3D12Device* InDevice, ID3D12CommandQueue* InQueue ) const;
	// 完了済みタイムスタンプの結果キャッシュと遅延解放スロットを回収する。デバイス喪失時はfalse。
	bool CollectCompleted();
	// GPU完了またはデバイス喪失後にだけ呼出し、全計測資源と番号状態を初期化する。
	void ResetAfterGPUIdle();

	// IGpuTimestampResources
	bool GetGpuTimestampCapabilities( FGpuTimestampCapabilities& Out ) const override;
	FGpuTimestampHandle CreateGpuTimestampQuery() override;
	void ReleaseGpuTimestampQuery( FGpuTimestampHandle Query ) override;
	bool IsGpuTimestampReady( FGpuTimestampHandle Query, bool& OutReady ) override;
	bool WaitGpuTimestamp( FGpuTimestampHandle Query, uint32 TimeoutMs = 10000 ) override;
	bool GetGpuTimestampResult( FGpuTimestampHandle Query, FGpuTimestampResult& Out ) override;

	// グラフィックスコマンド列用のバッチ識別番号を発行する。異常時は0。
	uint64 BeginBatch();
	// 指定グラフィックスコマンド列とバッチで計測開始点を記録する。記録済み・他バッチ・不正状態ではfalse。
	bool BeginRecording( ID3D12GraphicsCommandList* Commands, uint64 BatchId, FGpuTimestampHandle Query );
	// 同じコマンド列とバッチで計測終了点を閉じ、ResolveQueryDataを記録する。
	bool EndRecording( ID3D12GraphicsCommandList* Commands, uint64 BatchId, FGpuTimestampHandle Query );
	// 指定バッチ内にEndRecording未呼び出しの計測スコープが残っているか返す。
	bool HasActiveRecording( uint64 BatchId ) const;
	// 提出直前に未閉じのまま残ったスコープだけをIdleへ戻し、未解決のResolveを防ぐ。
	void AbortUnclosedInBatch( uint64 BatchId );
	// 提出に失敗したバッチの記録中・未提出計測をすべて取り消す。
	void AbortBatch( uint64 BatchId );
	// ExecuteCommandLists後にGPU Fenceを打ち、同バッチの解決済み計測を完了待ちへ移す。
	bool CommitBatch( uint64 BatchId );

	// 単発compute提出向けに、ハンドルと空き状態を検証して使用スロット情報を取得する（状態はまだ変えない）。
	bool TryPrepareComputeSlot( FGpuTimestampHandle Query, uint32& OutBeginSlot );
	// 検査済みcomputeコマンド列の冒頭に開始点を記録する。
	void RecordBeginAtSlot( ID3D12GraphicsCommandList* Commands, uint32 BeginSlot ) const;
	// 検査済みcomputeコマンド列の末尾に終了点とResolveQueryDataを記録する。
	void RecordEndAndResolveAtSlot( ID3D12GraphicsCommandList* Commands, uint32 BeginSlot ) const;
	// compute提出のExecuteCommandLists直後にFenceを打ち、計測を提出済みへ確定する。
	bool CommitComputeSlot( FGpuTimestampHandle Query, uint32 BeginSlot );

private:
	// 1つの計測ハンドルが辿る状態遷移。
	enum class EQueryState : uint8
	{
		Idle,          // 作成直後またはAbort後の未記録状態。
		Recording,     // BeginRecording済みでEndRecording待ち。
		PendingSubmit, // EndRecording + ResolveQueryData記録済み、ExecuteCommandLists待ち。
		Submitted,     // ExecuteCommandLists + Signal済み、Fence完了待ち。
		Completed      // Fence完了済みでReadbackから結果を読み出し済み。
	};

	// 生きている計測ハンドル1件分の状態と物理スロット。
	struct FQueryRecord
	{
		uint32 Slot = 0;               // ペア番号 [0, MaxActiveQueries)。QueryHeapでは [Slot*2, Slot*2+1]。
		EQueryState State = EQueryState::Idle;
		uint64 BatchId = 0;            // グラフィックスバッチ所属ID。Compute直接提出やIdle時は0。
		uint64 FenceValue = 0;         // Submitted状態で完了を待つFence番号。
		FGpuTimestampResult CachedResult; // Completed時に一度だけ読み出した結果。
	};

	// 解放されたが使用中・提出前の物理スロットを安全に遅延回収するための控え。
	struct FRetiredSlot
	{
		uint32 Slot = 0;       // 解放待ちのペア番号。
		uint64 BatchId = 0;    // 未提出グラフィックスバッチに紐づく場合の番号。
		uint64 FenceValue = 0; // GPU提出済みの場合の完了待ちFence番号。
	};

	// 指定コマンドリストが同じID3D12Deviceに属するか検証する。
	bool IsCompatibleCommandList( ID3D12GraphicsCommandList* Commands ) const;
	// BatchIdに含まれるRecordingまたはPendingSubmitをIdleへ戻し、同バッチの解放控えを回収する。
	void ResetBatchInternal( uint64 BatchId, bool bIncludePendingSubmit );

	Microsoft::WRL::ComPtr<ID3D12Device> Device;
	Microsoft::WRL::ComPtr<ID3D12CommandQueue> Queue;
	Microsoft::WRL::ComPtr<ID3D12QueryHeap> QueryHeap;
	Microsoft::WRL::ComPtr<ID3D12Resource> ReadbackBuffer;
	Microsoft::WRL::ComPtr<ID3D12Fence> Fence;
	HANDLE FenceEvent = nullptr;
	uint64 FrequencyHz = 0;
	uint64 QueueSerial = 0;
	uint64 NextBatchId = 0;
	uint64 NextFenceValue = 0;
	bool bHealthy = false;
	TArray<uint32> FreeSlots;
	TMap<uint64, FQueryRecord> LiveQueries;
	TArray<FRetiredSlot> RetiredSlots;
	TArray<uint64> Notifications;
};
