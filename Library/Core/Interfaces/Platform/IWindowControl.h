// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Types/TypeDefinition.h"

//=============================================================================
// IWindowControl
//   ゲームのウィンドウの位置・大きさの倍率・フォーカスを扱う窓口（全画面などのモードは IPlatform、ディスプレイの情報は IDisplayInfo）。
//   実装: CDxLibWindowControlSubsystem（DxLib）、CNullWindowControlSubsystem（検査用）。ゲームは GetAppSubsystem<IWindowControl>() で取る。メインスレッドから使う。
//
//   - 座標はデスクトップ全体の座標（IDisplayInfo と同じ）
//   - 位置と倍率を変えられるのは、通常のウィンドウ（EWindowMode::Windowed）のときだけ。それ以外は false を返して何もしない
//=============================================================================
class IWindowControl
{
public:
	virtual ~IWindowControl() = default;

	// ウィンドウの左上の位置。取れなければ false で、Out は変えない
	virtual bool GetWindowPosition( int32& OutX, int32& OutY ) const = 0;

	// ウィンドウの左上を (X, Y) に動かす。動かせなければ false
	virtual bool SetWindowPosition( int32 X, int32 Y ) = 0;

	// ウィンドウを DisplayIndex 番のディスプレイの中央に動かす（IDisplayInfo と同じ番号）。動かせなければ false
	virtual bool MoveWindowToDisplay( int32 DisplayIndex ) = 0;

	// ゲーム画面（描画先）に対するウィンドウの大きさの倍率（1.0 = 等倍、2.0 = 縦横 2 倍）
	virtual float64 GetWindowScale() const = 0;

	// 倍率を変える（0 より大きい値）。変えられなければ false
	virtual bool SetWindowScale( float64 Scale ) = 0;

	// ゲームのウィンドウが入力の対象（前面）か
	virtual bool IsWindowFocused() const = 0;
};
