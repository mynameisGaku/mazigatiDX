// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Types/TypeDefinition.h"

//=============================================================================
// フォントの指定・測った値の型
//   DxLib の型・定数は出さない（実装は Platform/DxLib）。
//=============================================================================

// 文字のなめらかさ（アンチエイリアス）。階調（縁の濃さの段階）が多いほど、縁がなめらかで、描くのは重い
enum class EFontAntialias : uint8
{
	None,        // なし（ギザギザ。階調は 1）
	Standard,    // 標準（既定）
	High4x4,     // 4 x 4 のサンプリング（階調は少なめで、縁がはっきりする）
	High8x8,     // 8 x 8 のサンプリング
	High16x16,   // 16 x 16 のサンプリング（階調がいちばん細かい）
};

// フォントの寸法（作ったあとの値。ピクセル）
struct FFontMetrics
{
	int32 Size = 0;        // 指定の大きさ（文字の高さ）
	int32 GlyphHeight = 0; // 1 行の文字の高さ（大きさに、上下の余白と縁取りの分が足される）
	int32 LineHeight = 0;  // 行の送り幅（次の行の同じ位置までの距離。指定していなければ GlyphHeight と同じ）
	int32 Ascent = 0;      // 描く位置（文字の上）から、ベースラインまで
	int32 MaxWidth = 0;    // 文字の幅の最大
	int32 EdgeSize = 0;    // 縁取りの太さ（縁取りがあると、1 文字の幅と行の高さに、縁取りの 2 倍が足される）
	int32 LetterSpacing = 0;   // 字間（全部の文字の幅に足される）
};

// 文字列を描いたときの大きさ
struct FTextSize
{
	int32 Width = 0;       // いちばん長い行の幅
	int32 Height = 0;      // 全体の高さ（(行数 - 1) x 行の送り幅（LineHeight）+ 1 行の文字の高さ（GlyphHeight）。空の文字列は 0）
	int32 NumLines = 0;    // 行数（改行で区切る。末尾の改行のあとも 1 行。空の文字列は 0）
};

// 文字 1 つ（UTF-8 のコードポイント 1 つ。改行も 1 つ）の、描く位置と幅
struct FTextGlyph
{
	int32 Offset = 0;      // 文字列の中のバイト位置
	int32 Bytes = 0;       // その文字のバイト数
	int32 X = 0;           // 文字列の左上からの位置（改行で、次の行の左端に戻る）
	int32 Y = 0;
	int32 Width = 0;       // 次の文字までの幅（字間・縁取りを含む。改行は 0）
};
