// SPDX-License-Identifier: MIT
#pragma once
#include "Library/Platform/System/WindowsInclude.h"
#include "Library/Core/Types/TypeDefinition.h"
#include <d3d12.h>
#include <wrl/client.h>

// 一つのGPUキューの完了番号と通知を所有する。描画・UI・ウィンドウを知らない。
class CDX12QueueCompletion
{
public:
	// 完了通知の所有先を複写しない。
	CDX12QueueCompletion() = default;
	CDX12QueueCompletion( const CDX12QueueCompletion& ) = delete;
	CDX12QueueCompletion& operator=( const CDX12QueueCompletion& ) = delete;
	// 所有側はGPU完了または喪失を確認してから破棄する。
	~CDX12QueueCompletion();
	// 同じGPUのDIRECTキューへ接続する。二重初期化・不正な組合せはfalse。
	bool Initialize( ID3D12Device* InDevice, ID3D12CommandQueue* InQueue );
	// 通知機構が作成済みならtrue。
	bool IsInitialized() const;
	// 先行提出に完了番号を付けて待つ。過去の通知では成功にせず、最大60000msでfalseを返す。
	bool Wait( uint32 TimeoutMs = 10000 );
	// GPU完了または喪失後にだけ通知機構を戻す。
	void ResetAfterGPUIdle();
private:
	Microsoft::WRL::ComPtr<ID3D12Device> Device; // 喪失の確認先。
	Microsoft::WRL::ComPtr<ID3D12CommandQueue> Queue; // 完了番号を提出するキュー。
	Microsoft::WRL::ComPtr<ID3D12Fence> Fence; // GPUの完了番号。
	HANDLE FenceEvent = nullptr; // CPU待機の通知先。
	uint64 FenceValue = 0; // 今回までに提出した完了番号。
	uint64 NotifiedValue = 0; // まだ完了していない通知の番号。
};
