// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Types/TypeDefinition.h"

//=============================================================================
// EShaderStage / FShaderHandle / IShader
//   描画の窓口（FDrawContext）に渡す、シェーダー（画面に描く計算を、自分で書いた小さなプログラム）を指す値と、それを持つもののインターフェース。
//   画像（FImageHandle / IImage）やシャドウマップ（FShadowMapHandle / IShadowMap）と同じ形で、DxLib のシェーダーのハンドル（int）を、そのまま公開しないための型。
//   ただの値（コピーしてよく、読むだけならどのスレッドからでも扱える）。シェーダーを所有はしない（描かれるときまで、持ち主が手放さないこと）。
//
//   実装する側の約束（CShader など）:
//   - GetShaderHandle() は const で、描画システムのワーカーのスレッドからも呼ばれる。状態を変えず、ロックを取らない
//   - 作られていない・作れなかったときは、無効なハンドル（FShaderHandle()）を返す
//=============================================================================

// シェーダーの種類
enum class EShaderStage : int32
{
	// 頂点シェーダー: 頂点 1 つごとに、画面での位置などを計算する
	Vertex = 0,
	// ピクセルシェーダー: 画素 1 つごとに、色を計算する
	Pixel = 1,
};

struct FShaderHandle
{
	// 実装が使う値（DxLib なら、シェーダーのハンドル）。負なら、無効
	int32 Value = -1;

	// 頂点シェーダーか、ピクセルシェーダーか。UseVertexShader / UsePixelShader に、種類の違うものは渡せない
	EShaderStage Stage = EShaderStage::Vertex;

	constexpr FShaderHandle() = default;

	constexpr FShaderHandle( int32 InValue, EShaderStage InStage )
		: Value( InValue )
		, Stage( InStage )
	{
	}

	// シェーダーを指しているか
	constexpr bool IsValid() const
	{
		return Value >= 0;
	}
};

class IShader
{
public:
	virtual ~IShader() = default;

	// このシェーダーを指すハンドル。描画の窓口は、記録のときにこれを読む（描かれるときまで、シェーダーは手放さないこと）
	virtual FShaderHandle GetShaderHandle() const = 0;
};
