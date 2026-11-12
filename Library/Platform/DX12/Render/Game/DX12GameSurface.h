// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Interfaces/Render/IRenderSurface.h"

class CDX12Device;
class CDX12GameRenderer;
class CDX12GameCapture;
class CDX12RenderTargetPass;

// 描画の所有スレッド内で、通常画面と撮影先・描画先パスを切り替える実行先。窓・UI・命令のスケジューラーは知らない。
class CDX12GameSurface : public IRenderSurface
{
  public:
	// すべての借用先はこの接続と実行中の命令より長く生存させる。
	CDX12GameSurface( CDX12Device &InDevice, CDX12GameRenderer &InRenderer, CDX12GameCapture &InCapture, CDX12RenderTargetPass *InTargetPass = nullptr );
	// 記録中の通常画面・撮影先・描画先パスの2Dを借りる。フレーム外はnullptr。
	IRenderCanvas2D *GetCanvas2D() override;
	// 記録中の通常画面・撮影先・描画先パスの3Dを借りる。フレーム外はnullptr。
	IRenderGeometry3D *GetGeometry3D() override;
	// 現在の描画先へ色の消去を記録する。
	void RecordClear( FColor32 Color ) override;
	// 同じキューのGPU完了を、既存の期限で待つ。
	bool WaitForGPU() override;
	// 呼出し側が前の命令を確定した後に、撮影先へ切り替える。
	bool BeginOffscreenCapture( const FOffscreenCaptureDesc &Desc ) override;
	// 撮影を提出して画素を読み、通常画面へ戻す。失敗時はOutを保つ。
	bool EndOffscreenCapture( TArray<FColor32> &Out ) override;
	// 所有済みの描画先資源へ描くパスを開始する。
	bool BeginRenderTargetPass( const FRenderTargetPassDesc &Desc ) override;
	// 描画先パスの命令を同じGPUキューへ提出し、通常画面へ戻す。
	bool EndRenderTargetPass() override;
	// 別のGPU提出より前に通常画面の記録を提出する。フレーム外は処理不要でtrue。
	// 撮影・描画先パス内の外部提出、またはGPU記録の失敗ではfalse。
	bool SubmitPendingMainCommands();
	// 実行先の接続を複製しない。
	CDX12GameSurface( const CDX12GameSurface & ) = delete;
	// 実行中に借用先を差し替えない。
	CDX12GameSurface &operator=( const CDX12GameSurface & ) = delete;

  private:
	// GPUと同じキューの完了待機を借りる。
	CDX12Device &Device;
	// 通常画面の描画先。
	CDX12GameRenderer &Renderer;
	// 撮影先と切替状態の所有者。
	CDX12GameCapture &Capture;
	// オフスクリーン描画先パスの所有者（任意）。
	CDX12RenderTargetPass *TargetPass = nullptr;
};
