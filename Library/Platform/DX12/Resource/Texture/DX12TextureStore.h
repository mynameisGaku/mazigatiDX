// SPDX-License-Identifier: MIT
#pragma once
#include "Library/Core/Interfaces/Render/IRenderTextureResources.h"
#include "Library/Core/Collection/Array.h"
#include "Library/Core/Collection/ArrayView.h"
#include "Library/Core/Collection/Map.h"
#include "Library/Core/Render/ComputeTextureAccess.h"
#include "Library/Core/Render/RenderTargetPassDesc.h"
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
	// 描画先画像と読み取れる深度の対応能力を返す。未初期化・照会失敗時はOutを保ってfalse。
	bool GetRenderTargetCapabilities( FRenderTargetCapabilities &Out ) const override;
	// 初期色で消去したRGBA8描画先画像を作る。通常描画やcomputeのRead入力にも使える。
	FTextureResourceHandle CreateRenderTargetRGBA8( int32 Width, int32 Height, FColor32 ClearColor = FColor32( 0, 0, 0, 255 ) ) override;
	// 初期深度で消去した読み取れるD32深度を作る。描画パスの深度とcomputeのR32Float Read入力に使える。
	FTextureResourceHandle CreateDepthTextureD32( int32 Width, int32 Height, float32 ClearDepth = 1.0f ) override;
	// 同じGPU・提出先への接続が有効か確認する。
	bool UsesDeviceAndQueue( ID3D12Device *InDevice, ID3D12CommandQueue *InQueue ) const;
	// 提出が保持する画像実体を返す。用途・形式違い・パス記録中の競合・無効な番号はOutを保ってfalse。
	bool AcquireComputeTexture( FTextureResourceHandle Texture, EComputeTextureAccess Access, FDX12TextureBinding &Out, EComputeTextureFormat Format = EComputeTextureFormat::RGBA8 ) const;
	// グラフィックス描画から読む画像実体を返す。深度・形式違い・パス書き込み中のターゲット競合はOutを保ってfalse。
	bool AcquireGraphicsTexture( FTextureResourceHandle Texture, EComputeTextureFormat Format, FDX12TextureBinding &Out ) const;
	// 複数または単一のオフスクリーン描画パス先を検証して固定する。重複・競合・寸法不一致・用途違いはfalse。
	bool BeginRenderTargetPass( TArrayView<const FTextureResourceHandle> ColorTargets, FTextureResourceHandle DepthTarget, TArray<FDX12TextureBinding> &OutColors, FDX12TextureBinding &OutDepth );
	// 単一のオフスクリーン描画パス先を検証して固定する。競合・用途違いはfalse。
	bool BeginRenderTargetPass( FTextureResourceHandle ColorTarget, FTextureResourceHandle DepthTarget, FDX12TextureBinding &OutColor, FDX12TextureBinding &OutDepth );
	// オフスクリーン描画パスの書き込み中状態を解除する。
	void EndRenderTargetPass();
	// オフスクリーン描画パスの記録中か。
	bool IsRenderTargetPassActive() const;
	// 指定番号が現在アクティブなオフスクリーン描画パスの色・深度ターゲットに含まれているか。
	bool IsActiveTarget( FTextureResourceHandle Texture ) const;
	// 新しい資源へ転送し、記録済みの描画には古い資源を保持する。
	bool UpdateTextureRGBA8( FTextureResourceHandle Texture, int32 Width, int32 Height, const FColor32 *Pixels ) override;
	// 番号だけを無効にし、GPU資源と記述子の解放を延期する。
	void ReleaseTexture( FTextureResourceHandle Texture ) override;
	// 有効な画像のviewだけを返す。
	bool GetTextureResourceView( FTextureResourceHandle Texture, FTextureDrawView &Out ) const override;

  private:
	// 同じハンドル番号空間で所有する画像資源の用途。
	enum class ETextureKind : uint8
	{
		StandardRGBA8, // CPUから転送した読み取り用のRGBA8画像。
		ComputeRGBA8, // UAV書き込みも許可するRGBA8画像。
		RenderTargetRGBA8, // オフスクリーン描画先として使えるRGBA8画像。
		DepthD32, // 描画パスとcompute R32Float読み取りで使える深度画像。
	};
	// COM参照を値へ包み、コンテナからoperator&を呼ばせない。
	struct FTextureRecord
	{
		Microsoft::WRL::ComPtr<ID3D12Resource> Resource; // GPUが読む・描く画像。
		Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> TargetHeap; // 描画先RTVまたは深度DSVのCPUヒープ。
		CDX12DescriptorPool::FAllocation Descriptor; // GPU完了まで保持するSRV（深度では未使用）。
		int32 Width = 0, Height = 0; // 画像の寸法。
		FColor32 InitialClearColor = FColor32( 0, 0, 0, 255 ); // 作成時に設定した初期消去色。
		float32 InitialClearDepth = 1.0f; // 作成時に設定した初期深度。
		ETextureKind Kind = ETextureKind::StandardRGBA8; // 用途の区別。
	};
	// 完了を待った新規画像を作る。失敗時はOutを変えない。
	bool Upload( int32 Width, int32 Height, const FColor32 *Pixels, bool bWritable, FTextureRecord &Out );
	// 用途を固定して新規画像を登録する。失敗時は無効値。
	FTextureResourceHandle Create( int32 Width, int32 Height, const FColor32 *Pixels, bool bWritable );
	// 指定番号が現在アクティブなオフスクリーン描画パスの色・深度に含まれているか。
	bool IsActiveTarget( uint64 TextureValue ) const;
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
	FComputeTextureCapabilities Capabilities; // 接続したGPUのcompute画像対応能力。
	FRenderTargetCapabilities RenderTargetCapabilities; // 接続したGPUの描画先・深度対応能力。
	uint64 ActiveColorTargets[FRenderTargetPassDesc::MaxColorTargets] = {}; // 現在記録中のオフスクリーン色描画先群。
	uint32 ActiveColorTargetCount = 0; // 現在記録中の色描画先枚数。
	uint64 ActiveDepthTarget = 0; // 現在記録中のオフスクリーン描画パスの深度先。
	bool bCapabilitiesAvailable = false; // 照会失敗を未対応の正常な回答と区別する。
	bool bRenderTargetCapabilitiesAvailable = false; // 描画先能力の照会成否。
	bool bHealthy = false; // 待機に失敗した場合は転送を再利用しない。
	static constexpr int32 MaxResources = 1024; // 旧版を含む同時保持数。
};
