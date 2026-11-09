// SPDX-License-Identifier: MIT
#pragma once
#include "Library/Math/Color.h"
class IRenderCanvas2D;

// フレームの描画先。APIのデバイスやウィンドウとは別の窓口。
class IRenderSurface
{
public:
	// 窓口経由の破棄を許可する。
	virtual ~IRenderSurface() = default;
	// フレーム外ではnullptr。返したキャンバスを保持しない。
	virtual IRenderCanvas2D* GetCanvas2D() = 0;
	// 記録済みの描画より後で画面全体を消す。フレーム内だけ使用する。
	virtual void RecordClear( FColor32 Color ) = 0;
	// 終了時の資源解放前にGPUの完了を待つ。喪失やタイムアウトはfalse。
	virtual bool WaitForGPU() = 0;
};
