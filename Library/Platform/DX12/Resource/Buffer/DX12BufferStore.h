// SPDX-License-Identifier: MIT
#pragma once
#include "Library/Core/Interfaces/Render/IRenderBufferResources.h"
#include "Library/Core/Collection/Map.h"
#include "Library/Platform/System/WindowsInclude.h"
#include <d3d12.h>
#include <wrl/client.h>
#include "DX12BufferBinding.h"

// GPU上のバイト列・転送領域・完了待ちと解放を所有する。画像・記述子・ImGuiは扱わない。
class CDX12BufferStore : public IRenderBufferResources
{
public:
	// 所有資源と記録領域を複写しない。
	CDX12BufferStore() = default;
	CDX12BufferStore( const CDX12BufferStore& ) = delete;
	CDX12BufferStore& operator=( const CDX12BufferStore& ) = delete;
	// 所有者はGPU完了またはデバイス喪失を保証してから破棄する。
	~CDX12BufferStore() override;
	// 同じデバイスのDIRECTキューを借用する。二重初期化・異なるデバイス・作成失敗はfalse。
	bool Initialize( ID3D12Device* InDevice, ID3D12CommandQueue* InQueue );
	// 同じキューの完了後に、登録解除済みと転送待ちの資源を回収する。失敗した記録領域は再使用しない。
	void CollectAfterGPUIdle();
	// 同じキューの完了またはデバイス喪失後に全資源を戻す。以後は再初期化が必要。
	void ResetAfterGPUIdle();
	// 初期バイト列を既定のGPUメモリへ転送する。失敗時は番号を公開しない。
	FBufferResourceHandle CreateBuffer( TArrayView<const uint8> Bytes, ERenderBufferShaderAccess Access = ERenderBufferShaderAccess::None ) override;
	// 番号だけを無効にし、実体の解放をGPU完了後へ延期する。
	void ReleaseBuffer( FBufferResourceHandle Buffer ) override;
	// 有効な資源の大きさを返す。失敗時はOutSizeを保つ。
	bool GetBufferSize( FBufferResourceHandle Buffer, uint64& OutSize ) const override;
	// 別のreadback領域へコピーし、完了後だけOutを置き換える。
	bool ReadBuffer( FBufferResourceHandle Buffer, uint64 Offset, int32 NumBytes, TArray<uint8>& Out ) override;
	// 同じ提出先から使用できるかを確認する。異なるキューでの状態管理を混在させない。
	bool UsesDeviceAndQueue( ID3D12Device* InDevice, ID3D12CommandQueue* InQueue ) const;
	// 用途が合う有効な資源を、提出完了まで保持できる参照として渡す。失敗時はOutを保つ。
	bool AcquireComputeBuffer( FBufferResourceHandle Buffer, ERenderBufferShaderAccess Access, FDX12BufferBinding& Out ) const;
	// 描画へ貸す有効な資源を保持参照で渡す。シェーダー用途には制限せず、失敗時はOutを保つ。
	bool AcquireGeometryBuffer( FBufferResourceHandle Buffer, FDX12BufferBinding& Out ) const;
private:
	// COM参照を通常の構造体へ包み、コンテナの処理とoperator&を衝突させない。
	struct FBufferRecord
	{
		Microsoft::WRL::ComPtr<ID3D12Resource> Resource; // GPUが読むバッファ。
		uint64 Size = 0; // 実際のデータのバイト数。
		ERenderBufferShaderAccess Access = ERenderBufferShaderAccess::None; // 作成時に固定したシェーダーの用途。
	};
	// 指定した置き場と初期状態でバッファを作る。失敗時はOutを保つ。
	bool CreateResource( uint64 Size, D3D12_HEAP_TYPE HeapType, D3D12_RESOURCE_STATES State, Microsoft::WRL::ComPtr<ID3D12Resource>& Out, D3D12_RESOURCE_FLAGS Flags = D3D12_RESOURCE_FLAG_NONE );
	// 前の提出が終わっている記録領域を開く。失敗後は再初期化まで提出しない。
	bool BeginCopy();
	// 記録を閉じて提出し、完了を待つ。失敗時もSourceとTargetを保持する。
	bool SubmitCopy( ID3D12Resource* Source, ID3D12Resource* Target );
	Microsoft::WRL::ComPtr<ID3D12Device> Device; // 資源を作るデバイス。
	Microsoft::WRL::ComPtr<ID3D12CommandQueue> Queue; // 描画と同じキュー。
	Microsoft::WRL::ComPtr<ID3D12CommandAllocator> Allocator; // 同期コピーの記録領域。
	Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> Commands; // 同期コピーの命令。
	Microsoft::WRL::ComPtr<ID3D12Fence> Fence; // この所有先の提出完了番号。
	Microsoft::WRL::ComPtr<ID3D12Resource> PendingSource, PendingTarget; // 待機に失敗しても回収しない転送資源。
	TMap<uint64, FBufferRecord> Buffers; // 有効な登録番号と所有バッファ。
	TArray<FBufferRecord> Retired; // 次のキュー完了確認まで残す登録解除済みバッファ。
	HANDLE FenceEvent = nullptr; // 完了待ち用の自動解除イベント。
	uint64 FenceValue = 0; // 次の提出に対応する通知番号。
	uint64 ResidentBytes = 0; // 登録解除済みを含むデータ量。GPU側の確保単位による余白は含まない。
	bool bHealthy = false; // 記録・提出・待機に失敗した後は再使用を禁止する。
	static constexpr int32 MaxBuffers = 256; // 登録解除済みを含む保持数の上限。
	static constexpr uint64 MaxBufferBytes = 64ull * 1024 * 1024; // 一つのバッファの上限。
	static constexpr uint64 MaxResidentBytes = 256ull * 1024 * 1024; // 同時に保持するデータ量の上限。
};
