// SPDX-License-Identifier: MIT
#pragma once

#include <cstdlib>   // 中身は std::abort。標準ライブラリに触れるのは、このヘッダーだけに閉じ込める

#include "Library/Core/Types/CompilerMacros.h"

//=============================================================================
// NProcess
//   プロセス全体に関わる操作。<cstdlib> の std::abort の代わり。
//=============================================================================
namespace NProcess
{
	// 異常終了（Abort）の直前に、1 回だけ呼ばれる関数（クラッシュ報告を書くために、Platform が登録する。Reason は、きっかけの短い説明）
	using FAbortHook = void ( * )( const char* Reason );

	FORCEINLINE FAbortHook& GetAbortHook()
	{
		static FAbortHook Hook = nullptr;
		return Hook;
	}

	FORCEINLINE void SetAbortHook( FAbortHook Hook )
	{
		GetAbortHook() = Hook;
	}

	// すぐにプロセスを異常終了させる（後始末はしない。デバッグ版では異常終了のダイアログが出る）。登録されたフックを、先に 1 回だけ呼ぶ。続行できない不具合の最後の手段。
	[[noreturn]] FORCEINLINE void Abort()
	{
		if ( const FAbortHook Hook = GetAbortHook() )
		{
			GetAbortHook() = nullptr;   // フックの中で、また Abort が呼ばれても、入れ子にならないように
			Hook( "Abort" );
		}
		std::abort();
	}
}
