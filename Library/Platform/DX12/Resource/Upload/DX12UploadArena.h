// SPDX-License-Identifier: MIT
#pragma once
#include "Library/Core/Collection/Array.h"
#include "Library/Core/Types/TypeDefinition.h"
#include "Library/Platform/System/WindowsInclude.h"
#include <d3d12.h>
#include <wrl/client.h>

// フレームおよび描画パスのUPLOADページを管理し、GPU完了前の領域再利用を防ぐ。ImGuiや描画記録器には依存しない。
class CDX12UploadArena
{
  public:
	// 1回のサブ割り当てで返すCPU書き込み先とGPU仮想アドレス。
	struct FAllocation
	{
		ID3D12Resource *Resource = nullptr; // 所有者がGPU完了まで保持するアップロードページ。
		uint64 Offset = 0; // ページ先頭からのバイト位置。
		uint64 Size = 0; // 要求した有効バイト数。
		uint8 *CpuAddress = nullptr; // write-combined書き込み専用アドレス（CPU読み取り禁止）。
		D3D12_GPU_VIRTUAL_ADDRESS GpuAddress = 0; // IA／CBVへ直接渡せるGPU仮想アドレス。
	};

	// 定常状態のゼロ再生成と回収挙動を検証する構成統計。
	struct FArenaStats
	{
		uint64 TotalBytes = 0; // 現在保持しているアップロードページの合計容量。
		uint64 CreateResourceCount = 0; // これまでに生成したページ数。
		uint64 RecycleCount = 0; // GPU完了後に先頭へ戻して再利用可能にしたページ回数。
		int32 PageCount = 0; // 現在保持しているページ枚数。
		int32 ActiveTicketCount = 0; // まだ提出または破棄されていないチケット数。
	};

	static constexpr uint64 DefaultPageBytes = 1024ull * 1024ull; // 通常フレーム用の1MiBページ単位。
	static constexpr uint64 MaxSingleAllocationBytes = 64ull * 1024ull * 1024ull; // 1回の要求上限。
	static constexpr uint64 MaxTotalBytes = 128ull * 1024ull * 1024ull; // アリーナ全体の最大保持容量。

	// ページとフェンスの所有は複写しない。
	CDX12UploadArena() = default;
	CDX12UploadArena( const CDX12UploadArena & ) = delete;
	CDX12UploadArena &operator=( const CDX12UploadArena & ) = delete;
	// 呼出側はGPU完了またはデバイス喪失後に破棄する。
	~CDX12UploadArena();

	// 対象デバイスとDIRECTキューへ接続する。二重初期化・別種キューはfalse。
	bool Initialize( ID3D12Device *InDevice, ID3D12CommandQueue *InQueue );
	// 接続先のGPU・キューと一致しているか確認する。
	bool UsesDeviceAndQueue( ID3D12Device *InDevice, ID3D12CommandQueue *InQueue ) const;
	// 現在のフェンス完了値を見て、使用済みページの先頭リセット可能なものを回収する。
	void CollectCompleted();
	// GPU完全停止またはデバイス喪失後だけ呼び、全ページとチケットを解放する。
	void ResetAfterGPUIdle();
	// 新しい記録区間のチケットを開く。未初期化・異常状態では0。
	uint64 BeginTicket();
	// チケットに属する一時UPLOAD領域をAlignment整列で確保する。不正入力・上限超過・異常時はOutを保ってfalse。
	bool Allocate( uint64 TicketId, uint64 Size, uint64 Alignment, FAllocation &Out );
	// GPU命令提出直後に呼び、触れたページへキューのフェンス完了条件を結びつける。
	bool CommitTicket( uint64 TicketId );
	// 提出前に失敗・中断したチケットの予約だけを解除する。
	void AbortTicket( uint64 TicketId );
	// 現在のアリーナ状態を返す（不変照会）。
	FArenaStats GetStats() const;

  private:
	struct FPage
	{
		uint64 Id = 0; // チケットから追跡する一意ページ番号。
		Microsoft::WRL::ComPtr<ID3D12Resource> Resource; // 永続マップ済みD3D12_HEAP_TYPE_UPLOAD資源。
		uint8 *Mapped = nullptr; // 永続マップ先ポインタ（解放前にUnmap）。
		D3D12_GPU_VIRTUAL_ADDRESS GpuBase = 0; // 先頭GPU仮想アドレス。
		uint64 Size = 0; // このページの容量。
		uint64 Offset = 0; // 線形バンプの現在位置。
		uint64 RetirementFence = 0; // この値をGPUが超えるまでOffsetを0へ戻さない。
		int32 OpenTicketCount = 0; // 現在記録中のチケットによるピン留め数。
		bool bCommittedWork = false; // ExecuteCommandLists後にCommitされた仕事を含むか。
	};

	struct FTicket
	{
		uint64 Id = 0; // 発行済みチケット番号。
		TArray<uint64> TouchedPageIds; // このチケットが触れたページID群（重複なし）。
	};

	// 指定サイズを収容できる新規ページを確保してPagesへ追加する。
	bool AllocatePage( uint64 MinBytes, int32 &OutPageIndex );
	// Idからページ配列の現在位置を探す。見つからなければ-1。
	int32 FindPageIndex( uint64 PageId ) const;
	// Idから開いているチケットの位置を探す。見つからなければ-1。
	int32 FindTicketIndex( uint64 TicketId ) const;
	// 1枚のページを安全にアンマップして解放する。
	void ReleasePageResource( FPage &Page );
	// チケットにページの予約を重複なく関連づける。
	void TouchPage( FTicket &Ticket, FPage &Page );

	Microsoft::WRL::ComPtr<ID3D12Device> Device; // 所有GPUへの参照。
	Microsoft::WRL::ComPtr<ID3D12CommandQueue> Queue; // フェンス完了を追跡するDIRECTキュー。
	Microsoft::WRL::ComPtr<ID3D12Fence> Fence; // 提出済みアップロードページの完了フェンス。
	TArray<FPage> Pages; // 保有するUPLOADページ群。
	TArray<FTicket> OpenTickets; // 現在開いているチケット一覧。
	uint64 NextPageId = 1; // 使い回さないページ識別子。
	uint64 NextTicketId = 1; // Reset後も増やして混線を避けるチケット識別子。
	uint64 FenceValue = 0; // 最後に発行したフェンス番号。
	uint64 TotalBytes = 0; // Pagesの合計バイト数。
	uint64 CreateResourceCount = 0; // 生成したページ累積回数。
	uint64 RecycleCount = 0; // GPU完了後に再利用リセットしたページ累積回数。
	bool bHealthy = false; // デバイス喪失やシグナル失敗後は新規割り当てを拒否する。
};
