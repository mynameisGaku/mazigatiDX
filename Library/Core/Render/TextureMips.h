// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Collection/Array.h"
#include "Library/Math/Color.h"

//=============================================================================
// NTextureMips
//   画像のミップマップ（半分ずつに縮めた画像の列）を、CPU で作る。GPU も API も知らない純粋な計算。
//   縮小して描くときに、遠い・小さい画像がちらつかず、なめらかに見えるようにするための元になる（DX12 の標準見た目。ERenderLook::Standard）。
//
//   作り方（箱フィルター）: 段 k の画素は、段 k - 1 の、重なる範囲の画素の面積の重みつきの平均。辺が奇数でも、端の画素を捨てず、面積で割る（明るさが保たれる）。
//   色は、sRGB の値を線形の光へ戻して平均し、sRGB へ戻す（細かい白黒の格子を縮めると、中間の明るさ 188 前後になる。値のまま平均すると 128 で、暗く見える）。
//   透明な画素の色は、平均に混ぜない（アルファで重みを付ける）ので、透明部分の色が、縁ににじまない。アルファは線形に平均する。
//   段の大きさは、元の辺を 2 で割った切り捨て（最小 1）。1 x 1 になるまで作る（Direct3D のミップの規則と同じ）。
//
//   TArray<NTextureMips::FLevel> Levels;
//   NTextureMips::GenerateChain( Width, Height, Pixels, Levels );   // Levels[0] が段 1（元の画像の半分）。元の画像（段 0）は含まない
//=============================================================================
namespace NTextureMips
{
	// 作れる元の画像の画素数の上限（作る間の一時メモリが、画素数に比例して大きくなるため）
	constexpr int64 MaxSourcePixels = 16 * 1024 * 1024;

	// 1 つの段
	struct FLevel
	{
		int32 Width = 0;
		int32 Height = 0;
		TArray<FColor32> Pixels;
	};

	// 段の数（元の画像の段 0 を含む）。長い辺の log2 の切り捨て + 1。辺が 0 以下なら 0
	int32 GetNumLevels( int32 Width, int32 Height );

	// 段 Level（0 が元の画像）での、1 辺の長さ。元の辺 Size を、Level 回、2 で割って切り捨てる（最小 1）
	int32 GetLevelSize( int32 Size, int32 Level );

	// 元の画像（左上から行の順の、非乗算済みの RGBA8）から、段 1 以降を作る。段 0 は含まない（段の数 - 1 個）。
	// 元の画像が 1 x 1 なら、段は 0 個で成功。大きさが 0 以下・画素が無い・MaxSourcePixels を超える・メモリ不足は、OutLevels を変えずに false
	bool GenerateChain( int32 Width, int32 Height, const FColor32* Pixels, TArray<FLevel>& OutLevels );
}
