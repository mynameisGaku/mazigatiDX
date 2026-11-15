// SPDX-License-Identifier: MIT
#pragma once
#include "Library/Core/Collection/Array.h"
#include "Library/Math/Color.h"

//=============================================================================
// NTgaDecoder
//   TGA（Targa）の画像を、RGBA8 の画素にする。OS・GPU・描画 API を知らない。
//   読める形: 無圧縮・RLE 圧縮の、トゥルーカラー（24 / 32 ビット）・16 ビット（5-5-5-1）・グレースケール（8 ビット、16 ビット = 明るさ + アルファ）・
//   カラーマップ（8 ビットの添字、パレットは 24 / 32 ビット）。原点は、左上・左下・右上・右下のどれでもよい。
//   WIC（Windows の画像の読み込み）は TGA を読めないので、組み込みの画像（エフェクトの丸など）と TGA のファイルは、これで読む。
//=============================================================================
namespace NTgaDecoder
{
	// TGA らしい先頭（ヘッダー）か。形式の判定にだけ使う。読めるとは限らない
	bool LooksLikeTga( const void* Data, usize Size );
	// 展開する。壊れた・対応しない形・各辺 16384 超・合計 6400 万画素超は false（出力は変えない）
	bool Decode( const void* Data, usize Size, int32& OutWidth, int32& OutHeight, TArray<FColor32>& OutPixels );
}
