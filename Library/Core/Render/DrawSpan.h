// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Types/TypeDefinition.h"

// 描画バッファを所有せずに参照する。呼び出し側は使用が終わるまで元バッファを保持する。
struct FDrawSpan
{
	// 読み取り専用の32bit値（1 word）の列。
	const uint32* Words = nullptr;
	// Wordsに含まれる32bit word数。
	int32 NumWords = 0;
};
