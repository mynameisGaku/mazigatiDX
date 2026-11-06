// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Collection/Array.h"
#include "Library/Core/Collection/Map.h"
#include "Library/Math/Color.h"

//=============================================================================
// FPaletteMapper
//   色を、色の表（パレット）のいちばん近い色の番号にする（RGB の差の 2 乗の和がいちばん小さいもの。同じなら若い番号）。
//   一度調べた色は覚えておく（画面の画素は、同じ色が多いので）。
//=============================================================================
class FPaletteMapper
{
public:
	explicit FPaletteMapper( const TArray<FColor32>& InPalette );

	// いちばん近い色の番号（表が空なら 0）
	uint8 Map( const FColor32& Color );

private:
	TArray<FColor32> Palette;
	TMap<uint32, uint8> Cache;
};
