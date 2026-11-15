// SPDX-License-Identifier: MIT
#pragma once
#include "Library/Core/Collection/String.h"
#include "Library/Core/Types/TypeDefinition.h"

// 文字を画像にするときのフォントの指定。API・OS・ハンドルに依存しない値なので、スレッドをまたいで複写できる。
struct FRenderTextFont
{
	FString Face;             // フォント名（UTF-8）。空なら実装の既定のフォント。
	int32 Size = 16;          // 文字の高さ（画素）。
	int32 Thickness = 6;      // 太さ（0..9）。
	int32 EdgeSize = 0;       // 0より大きいと縁取りを付ける（画素）。
	int32 LetterSpacing = 0;  // 字間（画素。全部の文字の幅に足す）。
	int32 LineHeight = -1;    // 行の送り幅（画素）。-1ならフォントの既定。
	bool bItalic = false;     // 斜体。
	bool bAntialias = true;   // 縁をなめらかにするか。

	bool operator==( const FRenderTextFont& Other ) const
	{
		return Face == Other.Face && Size == Other.Size && Thickness == Other.Thickness && EdgeSize == Other.EdgeSize && LetterSpacing == Other.LetterSpacing
			&& LineHeight == Other.LineHeight && bItalic == Other.bItalic && bAntialias == Other.bAntialias;
	}
	bool operator!=( const FRenderTextFont& Other ) const { return !( *this == Other ); }

	// 同じ指定を見分ける文字列（キャッシュの鍵に使う）。
	FString ToKey() const
	{
		return FString::Printf( "%s|%d|%d|%d|%d|%d|%d|%d", *Face, Size, Thickness, EdgeSize, LetterSpacing, LineHeight, bItalic ? 1 : 0, bAntialias ? 1 : 0 );
	}
};
