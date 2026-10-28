// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Types/TypeDefinition.h"

//=============================================================================
// FImageHandle
//   描画の窓口（FDrawContext）に渡す、画像を指す値。DxLib の画像のハンドル（int）を、そのまま公開しないための型。
//   画像を持つもの（CTexture など）は、IImage::GetImageHandle() でこれを返す。
//   int32 からは、明示的にしか作れない（FImageHandle( 3 )）。作られていない・読み込めなかった画像は、無効（IsValid が false）。
//   ただの値（コピーしてよく、読むだけならどのスレッドからでも扱える）。指している画像を所有はしない（画像は、描かれるときまで、持ち主が手放さないこと）。
//=============================================================================
struct FImageHandle
{
	// 実装が使う値（DxLib なら、グラフィックハンドル）。負なら、無効
	int32 Value = -1;

	constexpr FImageHandle() = default;

	explicit constexpr FImageHandle( int32 InValue )
		: Value( InValue )
	{
	}

	// 画像を指しているか
	constexpr bool IsValid() const
	{
		return Value >= 0;
	}

	friend constexpr bool operator==( const FImageHandle& A, const FImageHandle& B )
	{
		return A.Value == B.Value;
	}

	friend constexpr bool operator!=( const FImageHandle& A, const FImageHandle& B )
	{
		return A.Value != B.Value;
	}
};

static_assert( sizeof( FImageHandle ) == 4, "FImageHandle must be a plain 4-byte value" );
