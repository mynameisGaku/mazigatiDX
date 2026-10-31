// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Types/TypeDefinition.h"

//=============================================================================
// FVertexBufferHandle / IVertexBuffer
//   描画の窓口（FDrawContext）に渡す、頂点バッファ（GPU に置いた頂点の並び）を指す値と、それを持つもののインターフェース。
//   画像（FImageHandle / IImage）と同じ形で、DxLib の頂点バッファのハンドル（int）を、そのまま公開しないための型。
//   ハンドルは、頂点の数も持つ（バッファの大きさは、作ったあと変わらない）。描画の窓口は、記録のときに、使う範囲が収まるか確かめる。
//   ただの値（コピーしてよく、読むだけならどのスレッドからでも扱える）。バッファを所有はしない（バッファは、描かれるときまで、持ち主が手放さないこと）。
//
//   実装する側の約束（CVertexBuffer など）:
//   - GetVertexBufferHandle() は const で、描画システムのワーカーのスレッドからも呼ばれる。状態を変えず、ロックを取らない
//   - 作られていない・作れなかったときは、無効なハンドル（FVertexBufferHandle()）を返す
//=============================================================================
struct FVertexBufferHandle
{
	// 実装が使う値（DxLib なら、頂点バッファのハンドル）。負なら、無効
	int32 Value = -1;

	// 頂点の数（1 以上。バッファの大きさ）
	int32 NumVertices = 0;

	constexpr FVertexBufferHandle() = default;

	constexpr FVertexBufferHandle( int32 InValue, int32 InNumVertices )
		: Value( InValue )
		, NumVertices( InNumVertices )
	{
	}

	// バッファを指しているか
	constexpr bool IsValid() const
	{
		return Value >= 0 && NumVertices >= 1;
	}
};

class IVertexBuffer
{
public:
	virtual ~IVertexBuffer() = default;

	// このバッファを指すハンドル。描画の窓口は、記録のときにこれを読む（描かれるときまで、バッファは手放さないこと）
	virtual FVertexBufferHandle GetVertexBufferHandle() const = 0;
};
