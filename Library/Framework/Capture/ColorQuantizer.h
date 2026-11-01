// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Collection/Array.h"
#include "Library/Core/Collection/ArrayView.h"
#include "Library/Math/Color.h"

//=============================================================================
// NColorQuantizer
//   画素の色を、MaxColors 色以下の色の表（パレット）にまとめる（GIF は 1 枚 256 色まで）。
//   使われている色が MaxColors 以下なら、その色をそのまま並べる（色は変わらない）。
//   多ければ、色の箱を、いちばん広がっている色の軸の真ん中で分けていく（メディアンカット。画素の数で重みを付ける）。
//   透明度は見ない（a は 255 として扱う）。
//=============================================================================
namespace NColorQuantizer
{
	TArray<FColor32> BuildPalette( TArrayView<const FColor32> Pixels, int32 MaxColors );
}
