// SPDX-License-Identifier: MIT
#pragma once
#include "Library/Core/Types/TypeDefinition.h"
#include "Library/Platform/DX12/Device/DX12QueueCompletion.h"
#include "Library/Platform/System/WindowsInclude.h"
#include <d3d12.h>
#include <wrl/client.h>

// 資源の作成・転送・読み戻しに使う、短いGPU作業を、同期で（提出して完了まで待って）実行する。描画の記録先とは別の領域と命令列を持つ。
//   ID3D12GraphicsCommandList *Commands = Submitter.Begin();
//   ...Commands へ記録...
//   Submitter.Submit( Resource, Upload ); // 閉じて提出し、完了まで待つ
// 待機に失敗した（GPUが止まった・失われた）ときは、GPUが参照しうる資源を保持したまま、以後の作業を拒否する。
class CDX12SyncSubmitter
{
  public:
	// 領域・命令列・完了待機は複写しない。
	CDX12SyncSubmitter() = default;
	CDX12SyncSubmitter( const CDX12SyncSubmitter & ) = delete;
	CDX12SyncSubmitter &operator=( const CDX12SyncSubmitter & ) = delete;
	// 同じキューのGPU完了後に所有者が破棄する。
	~CDX12SyncSubmitter();
	// 同じデバイスのDIRECTキューへ接続する。失敗したら何も保持しない。
	bool Initialize( ID3D12Device *InDevice, ID3D12CommandQueue *InQueue );
	// 待機に失敗して保持していた資源を戻し、デバイスが生きていれば、再び受け付ける。同じキューのGPU完了後だけ呼ぶ。
	void CollectAfterGPUIdle();
	// 同じキューのGPU完了またはデバイス喪失後だけ呼ぶ。全部を解放する。
	void ResetAfterGPUIdle();
	// 受け付けられる状態か（初期化済み・待機に失敗していない）。
	bool IsHealthy() const { return bHealthy; }
	// この提出器の記録先を開く。受け付けられない・リセットに失敗したときは nullptr（失敗なら、以後も拒否する）。
	ID3D12GraphicsCommandList *Begin();
	// 記録を閉じて提出し、完了まで待つ。待つ間とタイムアウトのあとも、KeepAlive の 2 つは、GPUの完了が分かるまで保持する。失敗でfalse。
	bool Submit( ID3D12Resource *KeepAliveFirst, ID3D12Resource *KeepAliveSecond = nullptr );
	// 記録を中止する（提出しない）。命令列は閉じる。
	void Abort();

  private:
	Microsoft::WRL::ComPtr<ID3D12Device> Device; // 所有GPUの参照。
	Microsoft::WRL::ComPtr<ID3D12CommandQueue> Queue; // 描画と同じキューの参照。
	Microsoft::WRL::ComPtr<ID3D12CommandAllocator> Allocator; // 同期転送だけに使う領域。
	Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> Commands; // 同期転送だけに使う記録先。
	CDX12QueueCompletion Completion; // 古い通知を排除して同じキューの転送完了を待つ。
	Microsoft::WRL::ComPtr<ID3D12Resource> KeepAlive[2]; // 待機失敗時もGPU参照を保持する。
	bool bHealthy = false; // 待機などに失敗した場合は再利用しない。
};
