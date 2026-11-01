// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Collection/Array.h"
#include "Library/Core/Collection/ArrayView.h"
#include "Library/Core/Types/TypeDefinition.h"

//=============================================================================
// NGifLzw
//   GIF の画素の圧縮（可変長の LZW。符号は 12 ビットまで、表がいっぱいになったら初めからやり直す）。
//   Indices は色の番号の並び（どれも 2^MinCodeSize 未満）。MinCodeSize は 2〜8。
//   返すのは符号の並びのバイト列（最下位ビットから詰める）。255 バイトずつの小分けは、呼ぶ側（GIF の組み立て）が行う。
//=============================================================================
namespace NGifLzw
{
	TArray<uint8> Compress( TArrayView<const uint8> Indices, int32 MinCodeSize );
}
