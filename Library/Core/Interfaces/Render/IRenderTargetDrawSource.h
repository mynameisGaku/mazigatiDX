// SPDX-License-Identifier: MIT
#pragma once
#include "Library/Core/Render/RenderTargetDrawView.h"

// 描画先（レンダーターゲット）の所有・作成から、描画側を切り離す。描画の命令を解釈する側が、アセットの番号を、描画先の資源へ変える。
// ゲームスレッドから呼ぶ。
class IRenderTargetDrawSource
{
public:
	// 窓口経由の破棄を許可する。
	virtual ~IRenderTargetDrawSource() = default;
	// 描画先として作った画像だけ返す。ふつうの画像・無効な番号は false（出力を保つ）。
	virtual bool GetRenderTargetDrawView( int32 Handle, FRenderTargetDrawView& Out ) = 0;
};
