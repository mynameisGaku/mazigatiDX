// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Collection/Array.h"
#include "Library/Platform/System/DisplayTypes.h"

//=============================================================================
// IDisplayInfo
//   つながっているディスプレイ（モニター）の情報を、読むだけの窓口。ウィンドウの操作は IWindowControl。
//   実装: CDxLibDisplayInfoSubsystem（DxLib）、CNullDisplayInfoSubsystem（検査用）。ゲームは GetAppSubsystem<IDisplayInfo>() で取る。メインスレッドから使う。
//
//   - 座標はデスクトップ全体の座標（主ディスプレイの左上が原点）。複数のディスプレイでは、左や上のものは負の値になる
//   - 一覧はそのとき調べた値（ディスプレイの抜き差しは、次に読んだときに反映される）
//=============================================================================
class IDisplayInfo
{
public:
	virtual ~IDisplayInfo() = default;

	// つながっているディスプレイの数（分からなければ 0）
	virtual int32 GetDisplayCount() const = 0;

	// OS で「メインのディスプレイ」に設定されているものの番号（FDisplayInfo::bPrimary が true のもの）。分からなければ -1
	virtual int32 GetPrimaryDisplayIndex() const = 0;

	// Index 番のディスプレイの情報。番号が範囲の外なら false で、Out は変えない
	virtual bool GetDisplay( int32 Index, FDisplayInfo& Out ) const = 0;

	// Index 番のディスプレイに切り替えられる解像度とリフレッシュレート。対応していない・番号が範囲の外なら false で、Out は変えない
	virtual bool GetDisplayModes( int32 Index, TArray<FDisplayMode>& Out ) const = 0;

	// ゲームのウィンドウがいるディスプレイの番号（分からなければ -1）
	virtual int32 GetWindowDisplayIndex() const = 0;

	// ゲームのウィンドウの表示の拡大率（1.0 = 100%、1.5 = 150%。分からなければ 1.0）
	virtual float32 GetWindowDpiScale() const = 0;
};
