// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Interfaces/Asset/AssetBackend.h"

// フォントの見た目の指定（大きさ以外）
struct FFontStyle
{
	int32 Thickness = 6;       // 太さ（0..9。実際は、0..5 と 6..9 の 2 段階にしか変わらないフォントが多い）
	int32 EdgeSize = 0;        // 0 より大きいと縁取り（ピクセル。縁取りの 2 倍が、1 文字の幅と行の高さに足される）
	bool bItalic = false;
	EFontAntialias Antialias = EFontAntialias::Standard;   // 文字のなめらかさ
	int32 LetterSpacing = 0;   // 字間（ピクセル。全部の文字の幅に足す。負の値で詰める）
	int32 LineHeight = -1;     // 行の送り幅（ピクセル。次の行の同じ位置までの距離）。-1 はフォントの既定
};
