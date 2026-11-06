// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Types/Callable.h"
#include "Library/Core/Types/CompilerMacros.h"
#include "Library/Core/Types/TypeDefinition.h"

//=============================================================================
// ゲームスレッド（メインスレッド）
//   描画・アセット・音・ウィンドウなど、DxLib を触る仕事は、ゲームスレッドだけで行う（DxLib はスレッドセーフではない）。
//   ゲームスレッドは、アプリの起動のはじめ（NGameMain・CEngine::Run）で Bind したスレッド。
//
//   if ( !IsInGameThread() ) { ... }                       // いまゲームスレッドか
//   GAME_THREAD_ONLY();                                    // ゲームスレッドの外から呼ばれたら、エラーを記録して戻る（void の関数）
//   GAME_THREAD_ONLY( false );                             // 戻り値のある関数は、戻す値を書く
//   NGameThread::Enqueue( []{ ... }, "Name" );             // どのスレッドからでも、ゲームスレッドで実行する仕事を積む
//                                                          // （ふつうは AsyncTask( EThreadType::Game, ... ) を使う。Library/Framework/Job/AsyncTask.h）
//=============================================================================
namespace NGameThread
{
	// いまのスレッドを、ゲームスレッドとして覚える
	void Bind();
	// Bind したか（する前は、どのスレッドもゲームスレッドとみなす。起動の前に作られる道具や、単体の検査で、誤って咎めないように）
	bool IsBound();
	// いまゲームスレッドか
	bool IsCurrent();

	// ゲームスレッドの外から、ゲームスレッドだけの関数が呼ばれたことを記録する（GAME_THREAD_ONLY が呼ぶ）。Function は関数の名前
	void ReportWrongThread( const char* Function );
	// これまでに ReportWrongThread された回数（検査用）
	int32 GetNumWrongThreadCalls();

	// ゲームスレッドで実行する仕事を積む（どのスレッドからでも呼べる）。Name は文字列リテラル（プロファイラーとログに出る）
	void Enqueue( TCallable<void()> Task, const char* Name );
	// 積まれた仕事を、積まれた順に実行する（ゲームスレッドで、エンジンが毎フレームのはじめに呼ぶ）。実行中に積まれたものは、次の呼び出しで実行する。
	// ゲームスレッドの外から呼ぶと、何もせず 0。戻り値は実行した数
	int32 RunPendingTasks();
	// まだ実行していない仕事の数
	int32 GetNumPendingTasks();
}

// いまゲームスレッドか（Unreal Engine と同じ名前）
FORCEINLINE bool IsInGameThread()
{
	return NGameThread::IsCurrent();
}

// ゲームスレッドの外から呼ばれたら、エラーを記録して、引数の値を返す（引数が無ければ、何も返さずに戻る）
#define GAME_THREAD_ONLY( ... ) \
	do \
	{ \
		if ( !IsInGameThread() ) \
		{ \
			NGameThread::ReportWrongThread( __FUNCTION__ ); \
			return __VA_ARGS__; \
		} \
	} while ( 0 )
