// SPDX-License-Identifier: MIT
#pragma once
#include "Library/Core/Collection/Array.h"
#include "Library/Core/Collection/String.h"
#include "Library/Core/Types/TypeDefinition.h"

// 文字の代わりに画像を入れる指定（フォントごと。AddFontCharImage）。1 文字（コードポイント）を、画像（アセットの番号）に置き換え、次の文字までの幅を Advance にする。
struct FRenderCharImage
{
	uint32 CodePoint = 0;
	int32 Image = -1;
	int32 DrawX = 0, DrawY = 0; // 文字の左上から、画像の左上までのずれ（画素）。
	int32 Advance = 0; // 次の文字までの幅（画素）。

	bool operator==( const FRenderCharImage& Other ) const { return CodePoint == Other.CodePoint && Image == Other.Image && DrawX == Other.DrawX && DrawY == Other.DrawY && Advance == Other.Advance; }
};

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
	TArray<FRenderCharImage> CharImages; // 文字の代わりに入れる画像。
	int32 Antialias = 1;      // 縁のなめらかさ。0 = なし、1 = 標準（OS が数える65階調の被覆率）、4 / 8 / 16 = その倍の大きさで描いて、縮める。

	bool operator==( const FRenderTextFont& Other ) const
	{
		return Face == Other.Face && Size == Other.Size && Thickness == Other.Thickness && EdgeSize == Other.EdgeSize && LetterSpacing == Other.LetterSpacing
			&& LineHeight == Other.LineHeight && bItalic == Other.bItalic && Antialias == Other.Antialias && HasSameCharImages( Other );
	}
	bool operator!=( const FRenderTextFont& Other ) const { return !( *this == Other ); }

	// 文字の代わりに入れる画像が、同じか。
	bool HasSameCharImages( const FRenderTextFont& Other ) const
	{
		if ( CharImages.Num() != Other.CharImages.Num() )
		{
			return false;
		}
		for ( int32 Index = 0; Index < CharImages.Num(); ++Index )
		{
			if ( !( CharImages[Index] == Other.CharImages[Index] ) )
			{
				return false;
			}
		}
		return true;
	}

	// 同じ指定を見分ける文字列（キャッシュの鍵に使う）。
	FString ToKey() const
	{
		FString Key = FString::Printf( "%s|%d|%d|%d|%d|%d|%d|%d", *Face, Size, Thickness, EdgeSize, LetterSpacing, LineHeight, bItalic ? 1 : 0, Antialias );
		for ( const FRenderCharImage& Image : CharImages )
		{
			Key += FString::Printf( "|c%d:%d:%d:%d:%d", static_cast<int32>( Image.CodePoint ), Image.Image, Image.DrawX, Image.DrawY, Image.Advance );
		}
		return Key;
	}
};
