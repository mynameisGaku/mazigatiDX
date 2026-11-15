// SPDX-License-Identifier: MIT
#pragma once
#include "Library/Core/Types/TypeDefinition.h"

//=============================================================================
// INativeWindowMessageObserver
//   OS が窓へ送ったメッセージを、描画 API とは別の層（文字入力など）が受け取るための窓口。
//   Windows 実装では、Message = WM_*、WParam / LParam = WPARAM / LPARAM、Window = HWND。ゲームのスレッドから呼ばれる（窓のメッセージを処理するスレッド）。
//=============================================================================
class INativeWindowMessageObserver
{
public:
	virtual ~INativeWindowMessageObserver() = default;

	// メッセージを処理して、OS の既定の処理を止めるときだけ true を返し、OS へ返す値を OutResult に入れる。
	// false なら、ほかの受け取り手と OS の既定の処理が、続けて受け取る
	virtual bool OnNativeWindowMessage( void* Window, uint32 Message, uint64 WParam, int64 LParam, int64& OutResult ) = 0;
};

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
	// 窓に届いたメッセージの受け取り手を足す／外す（同じものを二重には足さない。外し忘れないこと。足した順に呼ばれる）
	virtual void AddMessageObserver( INativeWindowMessageObserver* Observer ) = 0;
	virtual void RemoveMessageObserver( INativeWindowMessageObserver* Observer ) = 0;
};
