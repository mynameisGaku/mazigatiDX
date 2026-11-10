// SPDX-License-Identifier: MIT
#pragma once
#include "Library/Core/Interfaces/Render/IRenderSurface.h"
#include "DX12CaptureTarget.h"
#include "DX12GameRenderer.h"

class FDX12Device;

// ゲームの共通描画を、独立したGPU撮影先へ描き直す。UIの文脈を必要としない。
class FDX12GameCapture : public IRenderSurface
{
  public:
	// 通常画面のGPUと文字作成先を借り、撮影先と描き直しの記録だけを所有する。
	FDX12GameCapture( FDX12Device &InOwner, IRenderTextRasterizer &InText );
	FDX12GameCapture( const FDX12GameCapture & ) = delete;
	FDX12GameCapture &operator=( const FDX12GameCapture & ) = delete;
	// 撮影のGPU完了か喪失まで待ち、借用先より先に破棄する。
	~FDX12GameCapture() override;
	// 通常画面の記録中に撮影先へ切り替える。不正設定・二重開始・失敗後はfalse。
	bool Begin( const FOffscreenCaptureDesc &Desc );
	// 撮影を提出し、詰めたRGBA8を返す。失敗時はOutを保ち、再利用を止める。
	bool End( TArray<FColor32> &Out );
	// 撮影への記録中だけtrue。
	bool IsActive() const;
	// 撮影中だけ2Dを貸す。
	IRenderCanvas2D *GetCanvas2D() override;
	// 撮影中だけ専用深度の基本3Dを貸す。
	IRenderGeometry3D *GetGeometry3D() override;
	// 記録位置で撮影先全体の色だけを消す。
	void RecordClear( FColor32 Color ) override;
	// 通常画面と同じキューの完了を待つ。
	bool WaitForGPU() override;

  private:
	FDX12Device &Owner; // 資源とキューを保持する借用先。
	IRenderTextRasterizer &Text; // GPUと独立したCPU文字画像の作成先。
	FDX12CaptureTarget Target; // UIを知らない色・深度・命令列・読み戻し。
	FDX12GameRenderer *Renderer = nullptr; // 撮影先の寸法と深度へ固定した描画記録。
	ID3D12Resource *BoundDepth = nullptr; // 描画先の作り直しを検出する借用値。
	bool bActive = false; // その撮影でだけ描画を許可する。
	bool bHealthy = false; // 失敗後に未完了の資源を再利用しないための印。
};
