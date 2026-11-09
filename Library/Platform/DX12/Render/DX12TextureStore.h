// SPDX-License-Identifier: MIT
#pragma once
#include "Library/Core/Interfaces/Render/IRenderTextureResources.h"
#include "Library/Core/Collection/Map.h"
#include "DX12DescriptorPool.h"

// RGBA8画像のGPU資源・転送・遅延解放を所有する。ImGui文脈を必要としない。
class FDX12TextureStore : public IRenderTextureResources
{
public:
	// 所有資源と転送領域は複写しない。
	FDX12TextureStore() = default;
	FDX12TextureStore( const FDX12TextureStore& ) = delete;
	FDX12TextureStore& operator=( const FDX12TextureStore& ) = delete;
	// GPU完了後に所有者が破棄する。Poolはこの実装より後に破棄する。
	~FDX12TextureStore() override;
	// 転送するデバイス・キューと、借用するSRV poolを固定する。
	bool Initialize( ID3D12Device* InDevice, ID3D12CommandQueue* InQueue, FDX12DescriptorPool& InPool );
	// 同じキューのGPU完了後に古い画像だけを戻す。未提出の描画がある間は呼ばない。
	void CollectAfterGPUIdle();
	// 同じキューのGPU完了またはデバイス喪失後だけ呼ぶ。番号は再利用しない。
	void ResetAfterGPUIdle();
	// 画像を同期で転送し、描画用の参照を作る。
	FTextureResourceHandle CreateTextureRGBA8( int32 Width, int32 Height, const FColor32* Pixels ) override;
	// 新しい資源へ転送し、記録済みの描画には古い資源を保持する。
	bool UpdateTextureRGBA8( FTextureResourceHandle Texture, int32 Width, int32 Height, const FColor32* Pixels ) override;
	// 番号だけを無効にし、GPU資源と記述子の解放を延期する。
	void ReleaseTexture( FTextureResourceHandle Texture ) override;
	// 有効な画像のviewだけを返す。
	bool GetTextureResourceView( FTextureResourceHandle Texture, FTextureDrawView& Out ) const override;
private:
	// COM参照を値へ包み、コンテナからoperator&を呼ばせない。
	struct FTextureRecord
	{
		Microsoft::WRL::ComPtr<ID3D12Resource> Resource; // GPUが読む画像。
		FDX12DescriptorPool::FAllocation Descriptor; // GPU完了まで保持するSRV。
		int32 Width = 0, Height = 0; // 画像の寸法。
	};
	// 完了を待った新規画像を作る。失敗時はOutを変えない。
	bool Upload( int32 Width, int32 Height, const FColor32* Pixels, FTextureRecord& Out );
	// 解放できる画像と記述子を戻す。
	void Destroy( FTextureRecord& Texture );
	Microsoft::WRL::ComPtr<ID3D12Device> Device; // 所有GPUの参照。
	Microsoft::WRL::ComPtr<ID3D12CommandQueue> Queue; // 描画と同じキューの参照。
	Microsoft::WRL::ComPtr<ID3D12CommandAllocator> Allocator; // 同期転送だけに使う領域。
	Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> Commands; // 同期転送だけに使う記録先。
	Microsoft::WRL::ComPtr<ID3D12Fence> Fence; // 転送完了の通知。
	Microsoft::WRL::ComPtr<ID3D12Resource> PendingUpload, PendingTexture; // 待機失敗時もGPU参照を保持する。
	FDX12DescriptorPool* Pool = nullptr; // 所有者より長生きする記述子の貸出先。
	TMap<uint64, FTextureRecord> Textures; // 型付き番号から所有画像への表。
	TArray<FTextureRecord> Retired; // 記録済みの描画が終わるまで保持する旧版。
	HANDLE FenceEvent = nullptr; // 転送の完了を待つイベント。
	UINT64 FenceValue = 0; // 次に通知する転送番号。
	uint64 NextHandle = 1; // Reset後も再利用しない画像番号。
	bool bHealthy = false; // 待機に失敗した場合は転送を再利用しない。
	static constexpr int32 MaxResources = 1024; // 旧版を含む同時保持数。
};
