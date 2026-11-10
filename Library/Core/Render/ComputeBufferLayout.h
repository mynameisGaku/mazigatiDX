// SPDX-License-Identifier: MIT
#pragma once
#include "Library/Core/Types/TypeDefinition.h"

// computeがバッファを読む配置。資源の所有と書き込み許可とは別に指定する。
enum class EComputeBufferLayout : uint8
{
	Raw, // 四バイト単位のバイト列。
	Structured, // 一定幅の構造体の配列。
	Constant // 読み取り専用の定数。行列などの共通入力に使う。
};
