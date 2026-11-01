// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Collection/Array.h"
#include "Library/Core/Collection/ArrayView.h"
#include "Library/Math/Color.h"

//=============================================================================
// NAlphaMatte
//   同じ被写体を、黒の背景と白の背景に描いた 2 枚から、背景の透けた画像（色と透明度）を求める（差分マット）。
//   背景が黒のとき C_黒 = a * F、白のとき C_白 = a * F + (1 - a) * 255 なので、a = 1 - (C_白 - C_黒) / 255、F = C_黒 / a。
//   半透明（ガラス・煙・光の縁）も、正しい透明度になる。a は 3 色の平均で求める。何も描かれていない所は (0, 0, 0, 0)。
//   描画の中身に依らない、CPU だけの計算。
//=============================================================================
namespace NAlphaMatte
{
	// 2 枚の大きさ（画素の数）が違えば false（Out は変えない）
	bool FromBlackAndWhite( TArrayView<const FColor32> OnBlack, TArrayView<const FColor32> OnWhite, TArray<FColor32>& Out );

	// 透けた画像を、不透明な背景の色の上に重ねる（透明度を残せない形式で保存するため）
	void Flatten( TArray<FColor32>& InOut, const FColor32& Background );
}
