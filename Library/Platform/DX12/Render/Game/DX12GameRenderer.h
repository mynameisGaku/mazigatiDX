// SPDX-License-Identifier: MIT
#pragma once
#include "Library/Platform/DX12/Render/Geometry/DX12GeometryRenderer.h"
#include "Library/Platform/Render/Canvas/GeometryCanvas2D.h"

// ゲームの2D・基本3D・文字を同じGPU命令列へ順に記録する。ImGuiとOSの型は必要としない。
class CDX12GameRenderer
{
  public:
	// GPU所有者と文字画像の作成先は借用し、描画の資源だけを保持する。
	CDX12GameRenderer() = default;
	CDX12GameRenderer( const CDX12GameRenderer & ) = delete;
	CDX12GameRenderer &operator=( const CDX12GameRenderer & ) = delete;
	// 呼出側はGPU完了後、画像所有者と文字作成先より先に破棄する。
	~CDX12GameRenderer();
	// GPUと寸法を固定する。未対応の深度・不正寸法・二重初期化はfalse。
	// BuffersとQueueは両方省略するか同じGPU・キューで指定する。不一致はfalse。借用する所有先は本描画器より長く生存させる。
	bool Initialize( ID3D12Device *Device, int32 Width, int32 Height, IRenderTextureResources &Textures, IRenderTextRasterizer &Text, ID3D12Resource *Depth = nullptr, ID3D12DescriptorHeap *DepthHeap = nullptr, CDX12BufferStore *Buffers = nullptr, ID3D12CommandQueue *Queue = nullptr, CDX12TextureStore *TextureStore = nullptr, CDX12GraphicsPipelineStore *GraphicsPipelines = nullptr, CDX12UploadArena *UploadArena = nullptr );
	// フレーム外で描画先の寸法と深度を差し替える。
	bool BindTarget( int32 Width, int32 Height, ID3D12Resource *Depth, ID3D12DescriptorHeap *DepthHeap );
	// GPUアイドル後のフレーム外で内部深度の寸法を更新する。
	bool Resize( int32 Width, int32 Height );
	// GPU完了後に記録先を借りる。撮影・描画先パスではScaleや深度消去設定を適用する。不正設定はfalse。
	bool BeginFrame( ID3D12GraphicsCommandList *Commands, D3D12_CPU_DESCRIPTOR_HANDLE ColorView, ID3D12DescriptorHeap *Images, float32 Scale = 1, bool bClearDepth = true, float32 ClearDepth = 1.0f, uint64 ForbiddenTextureBinding = 0 );
	// GPU完了後に1〜8枚の色描画先と自己参照禁止SRV群を借りる。MRT時または非RGBA8時は2Dキャンバスを無効化する。
	bool BeginFrame( ID3D12GraphicsCommandList *Commands, TArrayView<const D3D12_CPU_DESCRIPTOR_HANDLE> ColorViews, ID3D12DescriptorHeap *Images, float32 Scale = 1, bool bClearDepth = true, float32 ClearDepth = 1.0f, TArrayView<const uint64> ForbiddenTextureBindings = {}, TArrayView<const ERenderTargetColorFormat> ColorFormats = {} );
	// 一時画像の番号を返し、記録先を外す。文字・頂点・消去の記録失敗や自己参照競合はfalse。GPU実体は保持する。
	bool EndFrame();
	// フレーム外で、記録した頂点とGPUバッファ参照を提出レコードへ移動する。
	void MoveFrameResources( TArray<FDX12BufferBinding> &OutBuffers, TArray<Microsoft::WRL::ComPtr<ID3D12Resource>> &OutUploads );
	// フレーム外で、記録した頂点・GPUバッファ・画像・パイプライン参照を提出レコードへ移動する。
	void MoveFrameResources( TArray<FDX12BufferBinding> &OutBuffers, TArray<Microsoft::WRL::ComPtr<ID3D12Resource>> &OutUploads, TArray<FDX12TextureBinding> &OutTextures, TArray<FDX12GraphicsPipelineBinding> &OutPipelines );
	// 現在フレームで開いているアップロードアリーナのチケットを取得し、提出側へ受け渡す。
	uint64 TakeUploadTicket();
	// 提出前に記録を中断した場合に、開いているアップロード予約を取り消す。
	void AbortUploadTicket();
	// 記録中だけ2Dの窓口を貸す。
	IRenderCanvas2D *GetCanvas2D();
	// 記録中だけ基本3Dの窓口を貸す。
	IRenderGeometry3D *GetGeometry3D();
	// 深度とクリップを変えず、命令位置で画面全体のRGBAを置き換える。
	void RecordClear( FColor32 Color );

  private:
	CDX12GeometryRenderer Geometry; // GPUの描画状態・深度・完了まで保持する頂点。
	CGeometryCanvas2D Canvas; // API非依存の図形生成と文字画像の借用。
	FRenderClipRect Clip; // 2Dと3Dで共有する描画先の切り取り範囲。
	IRenderTextureResources *Textures = nullptr; // アセットやUIと独立した既存画像の所有者。
	IRenderTextRasterizer *Text = nullptr; // CPUの文字画像だけを作る借用先。
	bool bAllowCanvas = false; // 単一色ターゲット時のみ2D固定PSOキャンバスを有効にする。
	bool bDrawSucceeded = true; // 色の消去等がGPUへ記録できなければ終了で失敗を返す。
	bool bActive = false; // この記録先のフレーム内だけ操作を許可する。
};
