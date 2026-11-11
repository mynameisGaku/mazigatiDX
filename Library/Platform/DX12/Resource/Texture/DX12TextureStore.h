// SPDX-License-Identifier: MIT
#pragma once
#include "Library/Core/Interfaces/Render/IRenderTextureResources.h"
#include "Library/Core/Collection/Map.h"
#include "Library/Core/Render/ComputeTextureAccess.h"
#include "Library/Platform/DX12/Device/DX12QueueCompletion.h"
#include "DX12TextureBinding.h"
#include "Library/Platform/DX12/Resource/Descriptor/DX12DescriptorPool.h"

// RGBA8画像のGPU資源・転送・遅延解放を所有する。ImGui文脈を必要としない。
class CDX12TextureStore : public IRenderTextureResources
{
  public:
	// 所有資源と転送領域は複写しない。
	CDX12TextureStore() = default;
	CDX12TextureStore( const CDX12TextureStore & ) = delete;
	CDX12TextureStore &operator=( const CDX12TextureStore & ) = delete;
	// GPU完了後に所有者が破棄する。Poolはこの実装より後に破棄する。
	~CDX12TextureStore() override;
	// 転送するデバイス・キューと、借用するSRV poolを固定する。
	bool Initialize( ID3D12Device *InDevice, ID3D12CommandQueue *InQueue, CDX12DescriptorPool &InPool );
	// 同じキューのGPU完了後に古い画像だけを戻す。未提出の描画がある間は呼ばない。
	void CollectAfterGPUIdle();
	// 同じキューのGPU完了またはデバイス喪失後だけ呼ぶ。番号は再利用しない。
	void ResetAfterGPUIdle();
	// 画像を同期で転送し、描画用の参照を作る。
	FTextureResourceHandle CreateTextureRGBA8( int32 Width, int32 Height, const FColor32 *Pixels ) override;
	// 対応能力を返す。未初期化・転送失敗・能力照会失敗時はOutを保ってfalse。
	bool GetComputeTextureCapabilities( FComputeTextureCapabilities &Out ) const override;
	// computeから読み書きできるRGBA8画像を作る。未対応・不正な入力は無効値。
	FTextureResourceHandle CreateComputeTextureRGBA8( int32 Width, int32 Height, const FColor32 *Pixels ) override;
	// 同じGPU・提出先への接続が有効か確認する。
	bool UsesDeviceAndQueue( ID3D12Device *InDevice, ID3D12CommandQueue *InQueue ) const;
	// 提出が保持する画像実体を返す。用途違い・無効な番号はOutを保ってfalse。
	bool AcquireComputeTexture( FTextureResourceHandle Texture, EComputeTextureAccess Access, FDX12TextureBinding &Out ) const;
	// 新しい資源へ転送し、記録済みの描画には古い資源を保持する。
	bool UpdateTextureRGBA8( FTextureResourceHandle Texture, int32 Width, int32 Height, const FColor32 *Pixels ) override;
	// 番号だけを無効にし、GPU資源と記述子の解放を延期する。
	void ReleaseTexture( FTextureResourceHandle Texture ) override;
	// 有効な画像のviewだけを返す。
	bool GetTextureResourceView( FTextureResourceHandle Texture, FTextureDrawView &Out ) const override;

  private:
	// COM参照を値へ包み、コンテナからoperator&を呼ばせない。
	struct FTextureRecord
	{
		Microsoft::WRL::ComPtr<ID3D12Resource> Resource; // GPUが読む画像。
		CDX12DescriptorPool::FAllocation Descriptor; // GPU完了まで保持するSRV。
		int32 Width = 0, Height = 0; // 画像の寸法。
		bool bWritable = false; // UAVとして作成し、computeの書き込みを許す。
	};
	// 完了を待った新規画像を作る。失敗時はOutを変えない。
	bool Upload( int32 Width, int32 Height, const FColor32 *Pixels, bool bWritable, FTextureRecord &Out );
	// 用途を固定して新規画像を登録する。失敗時は無効値。
	FTextureResourceHandle Create( int32 Width, int32 Height, const FColor32 *Pixels, bool bWritable );
	// 解放できる画像と記述子を戻す。
	void Destroy( FTextureRecord &Texture );
	Microsoft::WRL::ComPtr<ID3D12Device> Device; // 所有GPUの参照。
	Microsoft::WRL::ComPtr<ID3D12CommandQueue> Queue; // 描画と同じキューの参照。
	Microsoft::WRL::ComPtr<ID3D12CommandAllocator> Allocator; // 同期転送だけに使う領域。
	Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> Commands; // 同期転送だけに使う記録先。
	CDX12QueueCompletion Completion; // 古い通知を排除して同じキューの転送完了を待つ。
	Microsoft::WRL::ComPtr<ID3D12Resource> PendingUpload, PendingTexture; // 待機失敗時もGPU参照を保持する。
	CDX12DescriptorPool *Pool = nullptr; // 所有者より長生きする記述子の貸出先。
	TMap<uint64, FTextureRecord> Textures; // 型付き番号から所有画像への表。
	TArray<FTextureRecord> Retired; // 記録済みの描画が終わるまで保持する旧版。
	FComputeTextureCapabilities Capabilities; // 接続したGPUのRGBA8対応能力。
	bool bCapabilitiesAvailable = false; // 照会失敗を未対応の正常な回答と区別する。
	bool bHealthy = false; // 待機に失敗した場合は転送を再利用しない。
	static constexpr int32 MaxResources = 1024; // 旧版を含む同時保持数。
};
