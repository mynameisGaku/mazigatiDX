// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Types/TypeDefinition.h"

//=============================================================================
// FShadowMapHandle / IShadowMap
//   描画の窓口（FDrawContext）に渡す、シャドウマップ（影の絵。光から見た深度を覚えておく画像）を指す値と、それを持つもののインターフェース。
//   画像（FImageHandle / IImage）や頂点バッファ（FVertexBufferHandle / IVertexBuffer）と同じ形で、DxLib のシャドウマップのハンドル（int）を、そのまま公開しないための型。
//   ただの値（コピーしてよく、読むだけならどのスレッドからでも扱える）。シャドウマップを所有はしない（描かれるときまで、持ち主が手放さないこと）。
//
//   実装する側の約束（CShadowMap など）:
//   - GetShadowMapHandle() は const で、描画システムのワーカーのスレッドからも呼ばれる。状態を変えず、ロックを取らない
//   - 作られていない・作れなかったときは、無効なハンドル（FShadowMapHandle()）を返す
//=============================================================================
struct FShadowMapHandle
{
	// 実装が使う値（DxLib なら、シャドウマップのハンドル）。負なら、無効
	int32 Value = -1;

	// 影の絵の縦横の大きさ（ピクセル。1 以上。大きいほど影の輪郭が細かく、メモリを使う）
	int32 Width = 0;
	int32 Height = 0;

	constexpr FShadowMapHandle() = default;

	constexpr FShadowMapHandle( int32 InValue, int32 InWidth, int32 InHeight )
		: Value( InValue )
		, Width( InWidth )
		, Height( InHeight )
	{
	}

	// シャドウマップを指しているか
	constexpr bool IsValid() const
	{
		return Value >= 0 && Width >= 1 && Height >= 1;
	}
};

class IShadowMap
{
public:
	virtual ~IShadowMap() = default;

	// このシャドウマップを指すハンドル。描画の窓口は、記録のときにこれを読む（描かれるときまで、シャドウマップは手放さないこと）
	virtual FShadowMapHandle GetShadowMapHandle() const = 0;
};
