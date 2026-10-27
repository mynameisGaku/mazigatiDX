// SPDX-License-Identifier: NOASSERTION
#pragma once

#include <cstdlib>   // 中身は std::abort。標準ライブラリに触れるのは、このヘッダーだけに閉じ込める

#include "Library/Core/Types/CompilerMacros.h"

//=============================================================================
// NProcess
//   プロセス全体に関わる操作。<cstdlib> の std::abort の代わり。
//=============================================================================
namespace NProcess
{
	// すぐにプロセスを異常終了させる（後始末はしない。デバッグ版では異常終了のダイアログが出る）。続行できない不具合の最後の手段。
	[[noreturn]] FORCEINLINE void Abort() { std::abort(); }
}
