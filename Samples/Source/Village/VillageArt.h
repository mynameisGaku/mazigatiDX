// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Framework/Asset/PixelImage.h"

//=============================================================================
// NVillageArt
//   サンプル Village の絵を、CPU 側で作る（画像のファイルを置かなくても動くようにするため）。
//=============================================================================
namespace NVillageArt
{
	// タイル 1 つの大きさ（ピクセル。正方形）
	constexpr int32 TileSize = 32;

	// キャラクターの画像の、1 コマの大きさ。コマは、下向き 2・上向き 2・横向き 2（右向き。左向きは反転して使う）の 6 コマを、横に並べる
	constexpr int32 CharacterWidth = 24;
	constexpr int32 CharacterHeight = 32;
	constexpr int32 CharacterCells = 6;

	// 金貨の画像の大きさ（ピクセル。正方形）
	constexpr int32 CoinSize = 16;

	// タイルの画像（EVillageTile の順に、横に並べる。1 つが TileSize 四方）
	FPixelImage MakeTileset();

	// キャラクターの画像（服と髪の色を変えて、主人公と村人を作る）
	FPixelImage MakeCharacter( const FColor32& Shirt, const FColor32& Hair );

	// 金貨の画像
	FPixelImage MakeCoin();

	// 光の画像（中心が不透明で、縁へ向かって、なめらかに透明になる白い円。Size x Size）
	FPixelImage MakeGlow( int32 Size );

	// 輪の画像（白い輪。Size x Size。Thickness は輪の太さ）。円グラフ（CircleGauge）で、輪の一部だけを描くのに使う
	FPixelImage MakeRing( int32 Size, int32 Thickness );
}
