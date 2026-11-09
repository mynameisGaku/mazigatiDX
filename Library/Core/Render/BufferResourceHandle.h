// SPDX-License-Identifier: MIT
#pragma once
#include "Library/Core/Types/TypeDefinition.h"

// GPUのバッファ資源を指す値。画像・アセット番号やGPUのアドレスとは区別する。
struct FBufferResourceHandle
{
	uint64 Value = 0; // 実行先が登録した番号。0は無効。
	// 無効な値で始める。
	constexpr FBufferResourceHandle() = default;
	// 記録済みの番号を型付きの値へ戻す。資源の有効性は所有先が調べる。
	explicit constexpr FBufferResourceHandle( uint64 InValue ) : Value( InValue ) {}
	// 同一プロセス内で再利用しない登録番号を発行する。番号の枯渇時は無効値。
	static FBufferResourceHandle Allocate();
	// 登録番号があるか。解放済みかどうかは所有先へ問い合わせる。
	constexpr bool IsValid() const { return Value != 0; }
	// 同じ登録番号同士を比較する。
	constexpr bool operator==( FBufferResourceHandle Other ) const { return Value == Other.Value; }
	// 異なる登録番号かを比較する。
	constexpr bool operator!=( FBufferResourceHandle Other ) const { return Value != Other.Value; }
};
