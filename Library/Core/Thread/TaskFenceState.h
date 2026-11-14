// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Thread/Mutex.h"
#include "Library/Core/Types/TypeDefinition.h"

// 実行列と完了票が共有する番号と待機条件。すべてMutexの下で読み書きする。
struct FTaskFenceState
{
	// 完了番号と実行中スレッドを保護する。
	FMutex Mutex;
	// 新しい仕事、完了、受付終了を待つ条件。
	FConditionVariable Changed;
	// 先頭から続けて完了した番号。
	uint64 Completed = 0;
	// 現在仕事を実行しているスレッド。実行していない間は0。
	uint64 ExecutingThread = 0;
	// スケジューラーが受付を拒否し、残りを実行できなかったか。
	bool bAborted = false;
};
