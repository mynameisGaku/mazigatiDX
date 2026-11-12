// SPDX-License-Identifier: MIT
#pragma once
#include "Library/Core/Types/TypeDefinition.h"

// GPUタイムスタンプ計測ペアを指す値。所有先が発行し、0は無効を表す。
struct FGpuTimestampHandle
{
	uint64 Value = 0; // 所有する実行先だけが解釈する識別番号。0は無効。
	// 無効な値で始める。
	constexpr FGpuTimestampHandle() = default;
	// 発行済みの識別番号を型付きの値にする。
	explicit constexpr FGpuTimestampHandle( uint64 InValue ) : Value( InValue ) {}
	// 番号が設定されているか。解放済み・未提出かは所有先が判定する。
	constexpr bool IsValid() const { return Value != 0; }
	// 同じ所有先の番号同士を比較する。
	constexpr bool operator==( FGpuTimestampHandle Other ) const { return Value == Other.Value; }
	constexpr bool operator!=( FGpuTimestampHandle Other ) const { return Value != Other.Value; }
};
