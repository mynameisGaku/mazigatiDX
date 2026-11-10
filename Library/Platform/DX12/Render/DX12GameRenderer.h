// SPDX-License-Identifier: MIT
#pragma once
#include "DX12GeometryRenderer.h"
#include "Library/Platform/Render/Canvas/GeometryCanvas2D.h"

// ゲームの2D・基本3D・文字を同じGPU命令列へ順に記録する。ImGuiとOSの型は必要としない。
class FDX12GameRenderer
{
  public:
	// GPU所有者と文字画像の作成先は借用し、描画の資源だけを保持する。
	FDX12GameRenderer() = default;
	FDX12GameRenderer( const FDX12GameRenderer & ) = delete;
	FDX12GameRenderer &operator=( const FDX12GameRenderer & ) = delete;
	// 呼出側はGPU完了後、画像所有者と文字作成先より先に破棄する。
	~FDX12GameRenderer();
	// GPUと寸法を固定する。未対応の深度・不正寸法・二重初期化はfalse。
	bool Initialize( ID3D12Device *Device, int32 Width, int32 Height, IRenderTextureResources &Textures, IRenderTextRasterizer &Text, ID3D12Resource *Depth = nullptr, ID3D12DescriptorHeap *DepthHeap = nullptr );
	// GPU完了後に記録先を借りる。撮影時はScaleを論理座標へ適用する。不正設定はfalse。
	bool BeginFrame( ID3D12GraphicsCommandList *Commands, D3D12_CPU_DESCRIPTOR_HANDLE ColorView, ID3D12DescriptorHeap *Images, float32 Scale = 1 );
	// 一時画像の番号を返し、記録先を外す。文字・頂点・消去の記録失敗はfalse。GPU実体は保持する。
	bool EndFrame();
	// 記録中だけ2Dの窓口を貸す。
	IRenderCanvas2D *GetCanvas2D();
	// 記録中だけ基本3Dの窓口を貸す。
	IRenderGeometry3D *GetGeometry3D();
	// 深度とクリップを変えず、命令位置で画面全体のRGBAを置き換える。
	void RecordClear( FColor32 Color );

  private:
	FDX12GeometryRenderer Geometry; // GPUの描画状態・深度・完了まで保持する頂点。
	FGeometryCanvas2D Canvas; // API非依存の図形生成と文字画像の借用。
	FRenderClipRect Clip; // 2Dと3Dで共有する描画先の切り取り範囲。
	IRenderTextureResources *Textures = nullptr; // アセットやUIと独立した既存画像の所有者。
	IRenderTextRasterizer *Text = nullptr; // CPUの文字画像だけを作る借用先。
	bool bDrawSucceeded = true; // 色の消去等がGPUへ記録できなければ終了で失敗を返す。
	bool bActive = false; // この記録先のフレーム内だけ操作を許可する。
};
