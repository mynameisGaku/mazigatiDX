// SPDX-License-Identifier: MIT
#pragma once
#include "Library/Core/Types/TypeDefinition.h"

// カスタムグラフィックスパイプラインが読むバッファの配置種別（R1では読み取り専用）。
enum class EGraphicsBufferLayout : uint8
{
	Constant, // 定数バッファ（DX12のregister(bN)）。
	Raw, // 生の4バイト単位バッファ（ByteAddressBuffer、register(tN)）。
	Structured, // 固定幅構造体配列（StructuredBuffer<T>、register(tN)）。
};
