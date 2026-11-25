// SPDX-License-Identifier: MIT
#pragma once
#include "Library/Core/Interfaces/Render/IRenderTextureResources.h"
#include "Library/Core/Collection/Array.h"
#include "Library/Core/Collection/ArrayView.h"
#include "Library/Core/Collection/Map.h"
#include "Library/Core/Render/ComputeTextureAccess.h"
#include "Library/Core/Render/RenderLook.h"
#include "Library/Core/Render/RenderTargetPassDesc.h"
#include "DX12SyncSubmitter.h"
#include "DX12TextureBinding.h"
#include "DX12TextureCreator.h"
#include "DX12TextureReader.h"
#include "DX12TextureRecord.h"
#include "DX12TextureUpdater.h"
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
	// 標準見た目（ERenderLook::Standard）では、ミップマップつきで作る（CPUで段を作って、全部の段を転送する）。互換見た目では、CreateTextureRGBA8 と同じ。
	FTextureResourceHandle CreateTextureRGBA8Mipped( int32 Width, int32 Height, const FColor32 *Pixels ) override;
	// 線形データ画像の各成分を独立して縮小する。
	FTextureResourceHandle CreateTextureRGBA8DataMipped( int32 Width, int32 Height, const FColor32 *Pixels ) override;
	// 見た目の水準を決める（画像を作る前に。作った画像は、作ったときの水準のまま）。
	void SetLook( ERenderLook InLook ) { Look = InLook; }
	ERenderLook GetLook() const { return Look; }
	// 対応能力を返す。未初期化・転送失敗・能力照会失敗時はOutを保ってfalse。
	bool GetComputeTextureCapabilities( FComputeTextureCapabilities &Out ) const override;
	// computeから読み書きできるRGBA8画像を作る。未対応・不正な入力は無効値。
	FTextureResourceHandle CreateComputeTextureRGBA8( int32 Width, int32 Height, const FColor32 *Pixels ) override;
	// computeから読み書きできるRGBA16F画像を作る。未対応・不正・非有限入力は無効値。
	FTextureResourceHandle CreateComputeTextureRGBA16F( int32 Width, int32 Height, const FColor *Pixels ) override;
	// 描画先画像と読み取れる深度の対応能力を返す。未初期化・照会失敗時はOutを保ってfalse。
	bool GetRenderTargetCapabilities( FRenderTargetCapabilities &Out ) const override;
	// 初期色で消去したRGBA8描画先画像を作る。通常描画やcomputeのRead入力にも使える。
	FTextureResourceHandle CreateRenderTargetRGBA8( int32 Width, int32 Height, FColor32 ClearColor = FColor32( 0, 0, 0, 255 ) ) override;
	// 初期色で消去したRGBA16F HDR描画先画像を作る。カスタム描画やcomputeのRead入力にも使える。
	FTextureResourceHandle CreateRenderTargetRGBA16F( int32 Width, int32 Height, FColor ClearColor = FColor( 0.0f, 0.0f, 0.0f, 1.0f ) ) override;
	// 初期値で消去したR32Float単一成分カラー描画先を作る。カスタム描画・compute/graphicsのR32Float Read入力にも使える。
	FTextureResourceHandle CreateRenderTargetR32Float( int32 Width, int32 Height, float32 ClearValue = 1.0f ) override;
	// 初期深度で消去した読み取れるD32深度を作る。描画パスの深度とcomputeのR32Float Read入力に使える。
	FTextureResourceHandle CreateDepthTextureD32( int32 Width, int32 Height, float32 ClearDepth = 1.0f ) override;
	// 同一寸法・同色グループ形式の画像全領域を同じDIRECTキューでコピーする。
	bool CopyTexture( FTextureResourceHandle Source, FTextureResourceHandle Destination ) override;
	// 検査済みの色画像全体を、同じGPUのフレームコマンド列へ複写する。フレーム完了まではStoreが元と先を保持する。
	bool RecordTextureCopy( ID3D12GraphicsCommandList *FrameCommands, FTextureResourceHandle Source, FTextureResourceHandle Destination ) const;
	// RGBA8画像の全画素をCPUへ読み戻す。失敗時は出力を変更しない。
	bool ReadTextureRGBA8( FTextureResourceHandle Texture, TArray<FColor32> &OutPixels, int32 &OutWidth, int32 &OutHeight ) override;
	// RGBA16F画像の全画素をFColor配列へ読み戻す。失敗時は出力を変更しない。
	bool ReadTextureRGBA16F( FTextureResourceHandle Texture, TArray<FColor> &OutPixels, int32 &OutWidth, int32 &OutHeight ) override;
	// R32Floatカラー画像の全画素をfloat32配列へ読み戻す。深度画像や別形式では出力を変更せずfalse。
	bool ReadTextureR32Float( FTextureResourceHandle Texture, TArray<float32> &OutValues, int32 &OutWidth, int32 &OutHeight ) override;
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
	// 画像・描画先・深度を新しく作れる状態か（健全・寸法・同時に持つ数の上限）。
	bool CanCreate( int32 Width, int32 Height ) const;
	// コピー可能な組を貸す。寸法・色形式の相違、書き込み中、解放済みの画像はfalse。
	bool FindTextureCopyPair( FTextureResourceHandle Source, FTextureResourceHandle Destination, const FDX12TextureRecord *&OutSource, const FDX12TextureRecord *&OutDestination ) const;
	// 完了を待った新規画像を作る。失敗時はOutを変えない。bMipsなら、ミップマップの全部の段も転送する（作れない大きさ・UAV書き込み可の画像は、段なし）。
	bool Upload( int32 Width, int32 Height, const FColor32 *Pixels, bool bWritable, FDX12TextureRecord &Out, bool bMips = false, ETextureMipMode MipMode = ETextureMipMode::Color );
	// 完了を待った新規RGBA16F compute画像を作る。失敗時はOutを変えない。
	bool UploadRGBA16F( int32 Width, int32 Height, const FColor *Pixels, FDX12TextureRecord &Out );
	// 浮動小数点カラー描画先（RGBA16F / R32Float）を作成する共通処理。
	FTextureResourceHandle CreateFloatColorRenderTarget( int32 Width, int32 Height, DXGI_FORMAT Format, const float32 ClearValues[4], EDX12TextureKind Kind );
	// 用途を固定して新規画像を登録する。失敗時は無効値。
	FTextureResourceHandle Create( int32 Width, int32 Height, const FColor32 *Pixels, bool bWritable, bool bMips = false, ETextureMipMode MipMode = ETextureMipMode::Color );
	// 完成した実体へ通し番号を付けて登録する。番号が取れなければ実体を破棄して無効値。
	FTextureResourceHandle Register( FDX12TextureRecord &Created );
	// 指定番号が現在アクティブなオフスクリーン描画パスの色・深度に含まれているか。
	bool IsActiveTarget( uint64 TextureValue ) const;
	// 解放できる画像と記述子を戻す。
	void Destroy( FDX12TextureRecord &Texture );
	Microsoft::WRL::ComPtr<ID3D12Device> Device; // 所有GPUの参照。
	Microsoft::WRL::ComPtr<ID3D12CommandQueue> Queue; // 描画と同じキューの参照。
	CDX12SyncSubmitter Submitter; // 作成・消去・コピー・読み戻しを、提出して完了まで待つ。
	CDX12TextureCreator Creator; // 画像・描画先・深度の GPU 資源を作る。
	CDX12TextureReader Reader; // 画像の全画素を CPU へ読み戻す。
	CDX12TextureUpdater Updater; // 読み取り用画像の全画素の更新を、完了を待たずに積む。
	CDX12DescriptorPool *Pool = nullptr; // 所有者より長生きする記述子の貸出先。
	TMap<uint64, FDX12TextureRecord> Textures; // 型付き番号から所有画像への表。
	TArray<FDX12TextureRecord> Retired; // 記録済みの描画が終わるまで保持する、登録解除した画像。
	TArray<FDX12TextureRecord> RetiredVersions; // 記録済みの描画が終わるまで保持する、更新で外れた旧版（GPU完了後は、更新の再利用へ預ける）。
	FComputeTextureCapabilities Capabilities; // 接続したGPUのcompute画像対応能力。
	FRenderTargetCapabilities RenderTargetCapabilities; // 接続したGPUの描画先・深度対応能力。
	uint64 ActiveColorTargets[FRenderTargetPassDesc::MaxColorTargets] = {}; // 現在記録中のオフスクリーン色描画先群。
	uint32 ActiveColorTargetCount = 0; // 現在記録中の色描画先枚数。
	uint64 ActiveDepthTarget = 0; // 現在記録中のオフスクリーン描画パスの深度先。
	bool bCapabilitiesAvailable = false; // 照会失敗を未対応の正常な回答と区別する。
	bool bRenderTargetCapabilitiesAvailable = false; // 描画先能力の照会成否。
	ERenderLook Look = ERenderLook::Compat; // 見た目の水準。
	static constexpr int32 MaxResources = 1024; // 旧版を含む同時保持数。
};
