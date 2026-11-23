// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Thread/Atomic.h"
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
	// ロックなしで読める、Completed の写し。眠る前に短く待つ（NSpinWait）ときの目安で、決めるのは、いつもロックの下の値。
	TAtomic<uint64> CompletedHint{ 0 };
	// ロックなしで読める、最後に受け付けた番号の写し。眠る前に短く待つときの目安で、決めるのは、いつもロックの下の値。
	TAtomic<uint64> IssuedHint{ 0 };
};
