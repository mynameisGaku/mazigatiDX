// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Types/CompilerMacros.h"
#include "Library/Core/Types/TypeDefinition.h"

//=============================================================================
// NPixelDither
//   4 x 4 の Bayer のディザ。ドット絵では、半透明を色の混ぜ合わせではなく、画素を間引いて表す。
//   濃さ Alpha の図形は、しきい値が Alpha より小さい画素だけを塗る（Alpha = 0.5 なら、市松模様にちょうど半分）。
//=============================================================================
namespace NPixelDither
{
	// (X, Y) のしきい値（0 より大きく 1 より小さい。16 段）。負の座標でも、4 画素ごとに同じ並び
	FORCEINLINE float32 GetThreshold( int32 X, int32 Y )
	{
		static constexpr int32 Matrix[4][4] = {
			{ 0, 8, 2, 10 },
			{ 12, 4, 14, 6 },
			{ 3, 11, 1, 9 },
			{ 15, 7, 13, 5 },
		};
		const int32 Row = ( ( Y % 4 ) + 4 ) % 4;
		const int32 Column = ( ( X % 4 ) + 4 ) % 4;
		return ( static_cast<float32>( Matrix[Row][Column] ) + 0.5f ) / 16.0f;
	}

	// 濃さ Alpha のとき、(X, Y) を塗るか（Alpha が 1 以上ならいつも、0 以下なら決して）
	FORCEINLINE bool CanDraw( int32 X, int32 Y, float32 Alpha )
	{
		return GetThreshold( X, Y ) < Alpha;
	}
}
