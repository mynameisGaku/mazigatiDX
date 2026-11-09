// SPDX-License-Identifier: MIT
#pragma once
#include "Library/Core/Types/TypeDefinition.h"

// OS接続にだけ使う借用ウィンドウ。描画APIのデバイスは持たない。
class INativeWindow
{
public:
	// 窓口経由の破棄を許可する。
	virtual ~INativeWindow() = default;
	// Windows実装ではHWND。OS接続以外の呼び出し側は解釈しない。
	virtual void* GetNativeWindow() const = 0;
	// 今フレームのホイール。次のフレーム開始まで同じ値。
	virtual void GetWheel( float32& X, float32& Y ) const = 0;
};
