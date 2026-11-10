// SPDX-License-Identifier: MIT
#pragma once
#include "Library/Math/Color.h"
#include "Library/Core/Render/OffscreenCaptureDesc.h"
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
};
