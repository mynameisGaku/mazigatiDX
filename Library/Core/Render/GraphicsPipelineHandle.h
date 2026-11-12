// SPDX-License-Identifier: MIT
#pragma once
#include "Library/Core/Types/TypeDefinition.h"

// 作成済みグラフィックスパイプラインの番号。バッファやGPUのアドレスとは別の型で保持する。
struct FGraphicsPipelineHandle
{
	uint64 Value = 0; // 所有先が登録する再利用しない番号。0は無効。

	// 無効な値で始める。
	constexpr FGraphicsPipelineHandle() = default;
	// 所有先が作った番号を型付きの値にする。
	explicit constexpr FGraphicsPipelineHandle( uint64 InValue )
		: Value( InValue )
	{
	}
	// 番号が設定されているか。解放済みかどうかは所有先が確認する。
	constexpr bool IsValid() const
	{
		return Value != 0;
	}
	// 同じ登録番号を比較する。
	constexpr bool operator==( FGraphicsPipelineHandle Other ) const
	{
		return Value == Other.Value;
	}
};
