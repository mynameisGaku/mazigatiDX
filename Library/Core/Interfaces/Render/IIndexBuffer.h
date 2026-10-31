// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Types/TypeDefinition.h"

//=============================================================================
// FIndexBufferHandle / IIndexBuffer
//   描画の窓口（FDrawContext）に渡す、インデックスバッファ（頂点バッファの、どの頂点を使うかを並べたもの）を指す値と、それを持つもののインターフェース。
//   FVertexBufferHandle / IVertexBuffer と同じ形（添字の数を持つ。コピーしてよく、バッファを所有しない。描かれるときまで、持ち主が手放さないこと）。
//=============================================================================
struct FIndexBufferHandle
{
	// 実装が使う値（DxLib なら、インデックスバッファのハンドル）。負なら、無効
	int32 Value = -1;

	// 添字の数（1 以上。バッファの大きさ）
	int32 NumIndices = 0;

	constexpr FIndexBufferHandle() = default;

	constexpr FIndexBufferHandle( int32 InValue, int32 InNumIndices )
		: Value( InValue )
		, NumIndices( InNumIndices )
	{
	}

	// バッファを指しているか
	constexpr bool IsValid() const
	{
		return Value >= 0 && NumIndices >= 1;
	}
};

class IIndexBuffer
{
public:
	virtual ~IIndexBuffer() = default;

	// このバッファを指すハンドル。描画の窓口は、記録のときにこれを読む（描かれるときまで、バッファは手放さないこと）
	virtual FIndexBufferHandle GetIndexBufferHandle() const = 0;
};
