// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Types/TypeDefinition.h"

//=============================================================================
// タッチ入力の値の型
//   ITouchInput が毎フレーム返す。座標はゲーム描画領域の左上を原点にする（マウスと同じ）。
//=============================================================================

// 画面に触れている指（ペン）1 つ
struct FTouchPoint
{
	// 触れている間、同じ指には同じ ID が付く（離すと別の指に使い回されることがある）
	uint32 Id = 0;
	int32 X = 0;
	int32 Y = 0;
	// 圧力（0〜1。分からない機器は 0）
	float32 Pressure = 0.0f;
	// このフレームで触れ始めた指だけ true
	bool bPressedThisFrame = false;
};
