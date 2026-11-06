// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Types/TypeDefinition.h"

//=============================================================================
// ICursorControl
//   マウスカーソルの見た目と位置を操作する窓口（読み取りは IMouseInput）。
//   実装: CDxLibInputSubsystem（DxLib）、CNullInputSubsystem（検査用）。すべてメインスレッドから使う。
//   座標はゲーム描画領域の左上を原点にする（IMouseInput と同じ）。
//=============================================================================
class ICursorControl
{
public:
	virtual ~ICursorControl() = default;

	// カーソルを表示するか。実装が対応していなければ false を返す
	virtual bool SetCursorVisible( bool bVisible ) = 0;
	// 最後に設定した表示の状態（設定していなければ、既定の true）
	virtual bool IsCursorVisible() const = 0;

	// カーソルの位置を動かす。次の入力更新から IMouseInput に反映される。実装が対応していなければ false
	virtual bool SetCursorPosition( int32 X, int32 Y ) = 0;
};
