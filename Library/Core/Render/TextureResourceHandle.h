// SPDX-License-Identifier: MIT
#pragma once
#include "Library/Core/Types/TypeDefinition.h"

// GPUの画像資源を指す値。アセット番号や描画APIのアドレスとは別の名前空間。
struct FTextureResourceHandle
{
	uint64 Value = 0; // 所有する実行先だけが解釈する番号。0は無効。
	// 無効な値で始める。
	constexpr FTextureResourceHandle() = default;
	// 実行先が作った番号を型付きの値にする。
	explicit constexpr FTextureResourceHandle( uint64 InValue ) : Value( InValue ) {}
	// 資源番号が設定されているか。解放済みかどうかは実行先が調べる。
	constexpr bool IsValid() const { return Value != 0; }
	// 同じ実行先の番号同士を比較する。
	constexpr bool operator==( FTextureResourceHandle Other ) const { return Value == Other.Value; }
	constexpr bool operator!=( FTextureResourceHandle Other ) const { return Value != Other.Value; }
};
