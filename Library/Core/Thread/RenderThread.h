// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Types/CompilerMacros.h"
#include "Library/Core/Types/TypeDefinition.h"

// 専用描画スレッドの識別。命令の受付やGPU完了は管理しない。
namespace NRenderThread
{
	// 調停する所有者だけが呼ぶ。未登録なら現在のスレッドを登録し、別スレッドが登録済みならfalse。
	bool Bind();
	// 登録したスレッドだけが解除できる。未登録や別のスレッドではfalse。
	bool Unbind();
	// 描画スレッドが登録されているか返す。
	bool IsBound();
	// 現在が登録した描画スレッドか返す。起動前と終了後はfalse。
	bool IsCurrent();
	// 誤ったスレッドからの呼出しを記録する。Functionは呼び出した関数名。
	void ReportWrongThread( const char *Function );
	// 誤った呼出しの累積数を検査用に返す。
	int32 GetNumWrongThreadCalls();
} // namespace NRenderThread

// 現在が専用の描画スレッドか返す。ゲームスレッドや共有ワーカーではfalse。
FORCEINLINE bool IsInRenderThread() { return NRenderThread::IsCurrent(); }

// 専用描画スレッド以外なら記録して戻る。引数があればその値を返す。
#define RENDER_THREAD_ONLY( ... )                             \
	do                                                        \
	{                                                         \
		if ( !IsInRenderThread() )                            \
		{                                                     \
			NRenderThread::ReportWrongThread( __FUNCTION__ ); \
			return __VA_ARGS__;                               \
		}                                                     \
	} while ( 0 )
