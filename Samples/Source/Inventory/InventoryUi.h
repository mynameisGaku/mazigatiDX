// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Math/Math.h"
#include "Source/Inventory/InventoryTypes.h"

class FDrawContext;

//=============================================================================
// NInventoryUi
//   パネルが共通で使う、小さな UI の部品（四角形・ボタン・枠の中の道具の絵）。描画命令だけで作る。
//=============================================================================
struct FUiRect
{
	int32 X = 0;
	int32 Y = 0;
	int32 Width = 0;
	int32 Height = 0;

	bool Contains( int32 PointX, int32 PointY ) const
	{
		return PointX >= X && PointX < X + Width && PointY >= Y && PointY < Y + Height;
	}
};

namespace NInventoryUi
{
	constexpr int32 SlotSize = 64;
	constexpr int32 SlotGap = 6;

	// 道具の枠 1 つ（空の枠も描く。Hot は、マウスが乗っているとき）
	void DrawSlot( FDrawContext& CommandList, const FUiRect& Rect, const FItemStack& Stack, bool bHot );

	// ボタン（文字つきの四角形）
	void DrawButton( FDrawContext& CommandList, const FUiRect& Rect, const char* Label, bool bHot, bool bEnabled );

	// 見出し
	void DrawTitle( FDrawContext& CommandList, int32 X, int32 Y, const char* Text );
}
