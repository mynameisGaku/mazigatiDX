// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Collection/ArrayView.h"
#include "Library/Core/Types/Callable.h"
#include "Library/Core/Types/TypeDefinition.h"
#include "Library/Math/Vector2.h"

//=============================================================================
// NPixelRaster
//   図形が覆う画素を求めて、1 画素ずつ Visit( X, Y ) を呼ぶ（描き先を知らない。何を塗るかは呼ぶ側が決める）。
//   座標は画素。画素 (X, Y) の中心は (X + 0.5, Y + 0.5) で、中心が図形の中にある画素を返す（線は Bresenham）。
//   同じ画素を 2 回以上は返さない。大きさが 0 以下・点が足りない・値が有限でないときは、何も呼ばない。
//=============================================================================
namespace NPixelRaster
{
	using FVisit = TCallable<void( int32 X, int32 Y )>;

	// A から B への線（両端を含む）。Width が 2 以上なら、線の各点を Width x Width の四角で太らせる
	void Line( const FVector2& A, const FVector2& B, int32 Width, const FVisit& Visit );

	// 中心 Center・半径 Radius の塗りの円
	void Disc( const FVector2& Center, float32 Radius, const FVisit& Visit );

	// 半径 Radius を真ん中にした、太さ Width の輪
	void Ring( const FVector2& Center, float32 Radius, int32 Width, const FVisit& Visit );

	// 輪のうち、角度 Start から End（ラジアン。x 軸から時計回り = 画面の下向きが正）の部分。End - Start が 2π 以上なら輪の全部
	void Arc( const FVector2& Center, float32 Radius, float32 Start, float32 End, int32 Width, const FVisit& Visit );

	// 横の半径 Radius.x・縦の半径 Radius.y の楕円の輪（太さ Width。足元の輪など、斜め上から見た円に）
	void Ellipse( const FVector2& Center, const FVector2& Radius, int32 Width, const FVisit& Visit );

	// 多角形の塗り（偶奇規則。点は 3 つ以上）
	void Polygon( TArrayView<const FVector2> Points, const FVisit& Visit );

	// 左上 TopLeft・大きさ Size の四角の塗り
	void Rect( const FVector2& TopLeft, const FVector2& Size, const FVisit& Visit );
}
