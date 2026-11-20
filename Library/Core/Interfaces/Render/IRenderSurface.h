// SPDX-License-Identifier: MIT
#pragma once
#include "Library/Math/Color.h"
#include "Library/Core/Render/OffscreenCaptureDesc.h"
#include "Library/Core/Render/RenderTargetPassDesc.h"
#include "Library/Core/Render/GpuTimestampHandle.h"
#include "Library/Core/Render/TextureResourceHandle.h"
#include "Library/Core/Collection/Array.h"
class IRenderCanvas2D;
class IRenderGeometry3D;

// フレームの描画先。APIのデバイスやウィンドウとは別の窓口。
class IRenderSurface
{
public:
	// 窓口経由の破棄を許可する。
	virtual ~IRenderSurface() = default;
	// フレーム外ではnullptr。返したキャンバスを保持しない。
	virtual IRenderCanvas2D* GetCanvas2D() = 0;
	// 3Dの実行先。非対応・フレーム外ではnullptr。
	virtual IRenderGeometry3D* GetGeometry3D() { return nullptr; }
	// 記録済みの描画より後で画面全体を消す。フレーム内だけ使用する。
	virtual void RecordClear( FColor32 Color ) = 0;
	// 終了時の資源解放前にGPUの完了を待つ。喪失やタイムアウトはfalse。
	virtual bool WaitForGPU() = 0;
	// 撮影用の描き直しへ切り替える。未対応・フレーム外・二重開始・不正な設定はfalse。
	virtual bool BeginOffscreenCapture( const FOffscreenCaptureDesc& ) { return false; }
	// 撮影用の画素を読み、通常の描画先へ戻す。失敗時はOutを保つ。
	virtual bool EndOffscreenCapture( TArray<FColor32>& ) { return false; }
	// 所有済みの描画先資源へ描くパスを開始する。未対応・二重開始・不正な設定はfalse。
	virtual bool BeginRenderTargetPass( const FRenderTargetPassDesc& ) { return false; }
	// 描画先パスの命令を同じGPUキューへ提出し、通常の描画先へ戻す。失敗時はfalse。
	virtual bool EndRenderTargetPass() { return false; }
	// いま描いている色（画面または描画先パスの最初の色）の矩形 [X0, X1) x [Y0, Y1) を、同じ形式・同じ大きさの画像 Dest へ写す。いままでの描画が、写される。未対応・範囲外・形式や大きさの違いはfalse。
	virtual bool CopyColorRegion( int32 X0, int32 Y0, int32 X1, int32 Y1, FTextureResourceHandle Dest ) { (void)X0; (void)Y0; (void)X1; (void)Y1; (void)Dest; return false; }
	// フレーム内のパス間で、同じ寸法・色形式の画像全体を複写する命令を記録する。未対応・不正な資源はfalse。
	// 呼び出しの成功は記録を示す。後続の処理は同じGPUキューで結果を使える。
	virtual bool RecordTextureCopy( FTextureResourceHandle, FTextureResourceHandle ) { return false; }
	// 現在の記録先（主画面または描画先パス）へGPUタイムスタンプの開始点を積む。未対応・フレーム外・不正番号はfalse。
	virtual bool BeginGpuTimestamp( FGpuTimestampHandle Query ) { (void)Query; return false; }
	// 開始済みのGPUタイムスタンプを閉じ、同じコマンド列へ解決を記録する。未開始・別ターゲットへ跨ぐ呼び出しはfalse。
	virtual bool EndGpuTimestamp( FGpuTimestampHandle Query ) { (void)Query; return false; }
};
