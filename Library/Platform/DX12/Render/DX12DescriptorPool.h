// SPDX-License-Identifier: MIT
#pragma once
#include "Library/Core/Collection/Array.h"
#include "Library/Platform/System/WindowsInclude.h"
#include <d3d12.h>
#include <wrl/client.h>

// 共有するSRV heapの区画を貸す。画像やImGuiの所有・寿命は扱わない。
class FDX12DescriptorPool
{
public:
	// 割り当ての世代も持つ。古い割り当てから別の区画を返却させない。
	struct FAllocation
	{
		const FDX12DescriptorPool* Owner = nullptr; // 貸し出したpool。異なるpoolへ返却させない。
		int32 Index = -1; // heap内の番号。
		uint64 Serial = 0; // 再利用しても一致しない貸出番号。
		D3D12_CPU_DESCRIPTOR_HANDLE Cpu = {}; // 資源を登録する場所。
		D3D12_GPU_DESCRIPTOR_HANDLE Gpu = {}; // 描画が参照する場所。
	};
	// 所有heapを複写しない。
	FDX12DescriptorPool() = default;
	FDX12DescriptorPool( const FDX12DescriptorPool& ) = delete;
	FDX12DescriptorPool& operator=( const FDX12DescriptorPool& ) = delete;
	// ShaderVisibleのSRV heapを作る。二重初期化・不正な容量はfalse。
	bool Initialize( ID3D12Device* Device, int32 Capacity );
	// GPU完了と全借用の返却後だけ呼ぶ。貸出番号は再利用しない。
	void Reset();
	// 空き区画を貸す。失敗時はOutを保つ。
	bool Allocate( FAllocation& Out );
	// 使用中のGPU参照がなくなってから戻す。異なる世代・二重返却はfalse。
	bool Release( const FAllocation& Allocation );
	// 同じ実行先内の描画へheapを借用する。
	ID3D12DescriptorHeap* GetHeap() const;
private:
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> Heap; // 所有するSRV heap。
	TArray<int32> Free; // 使用していない区画。
	TArray<uint64> Serials; // 区画ごとの貸出番号。0なら空き。
	uint64 NextSerial = 1; // Reset後も再利用しない番号。
	UINT Stride = 0; // 記述子一つの幅。
};
