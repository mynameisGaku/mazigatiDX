// SPDX-License-Identifier: MIT
#include "OrderedTaskQueue.h"

#include "Library/Core/Profiler/ProfilerScope.h"
#include "Library/Core/Thread/Thread.h"

COrderedTaskQueue::COrderedTaskQueue() : State( MakeShared<FTaskFenceState>() ) {}

FTaskFence COrderedTaskQueue::Enqueue( TCallable<void()> Task, const char *Name, bool &OutStart )
{
	OutStart = false;
	if ( !Task )
	{
		return {};
	}
	FUniqueLock Lock( State->Mutex );
	if ( !bAccepting || Pending.Num() >= 4096 || Issued == static_cast<uint64>( -1 ) )
	{
		return {};
	}
	// 受付番号を決める箇所が、複数の要求元を1つの順序へまとめる境界。
	const uint64 Sequence = ++Issued;
	Pending.Enqueue( FEntry{ MoveTemp( Task ), Name ? Name : "OrderedTask", Sequence } );
	OutStart = !bRunning;
	bRunning = true;
	Lock.Unlock();
	State->Changed.NotifyAll();
	return FTaskFence( State, Sequence );
}

FTaskFence COrderedTaskQueue::GetFence( bool bClose )
{
	FUniqueLock Lock( State->Mutex );
	if ( bClose )
	{
		bAccepting = false;
	}
	// ロックの外で通知するまでに、新しい受付が来ても取得した区切りは変わらない。
	const FTaskFence Fence( State, Issued );
	Lock.Unlock();
	State->Changed.NotifyAll();
	return Fence;
}

bool COrderedTaskQueue::ExecuteOne( bool bWait )
{
	// 仕事本体と取り込んだ値の破棄を、待ち行列のロックの外で行う。
	FEntry Entry;
	{
		FUniqueLock Lock( State->Mutex );
		if ( bWait )
		{
			// 専用スレッドは仕事が届くか受付が閉じるまで眠る。
			const auto HasWork = [this]() { return !Pending.IsEmpty() || !bAccepting; };
			State->Changed.Wait( Lock, HasWork );
		}
		if ( !Pending.Dequeue( Entry ) )
		{
			bRunning = false;
			return false;
		}
		State->ExecutingThread = NThread::GetCurrentId();
	}
	{
		PROFILE_SCOPE_CAT( NProfileCategory::Job, Entry.Name );
		Entry.Task();
	}
	Entry.Task = nullptr;
	{
		FScopedLock Lock( State->Mutex );
		State->Completed = Entry.Sequence;
		State->ExecutingThread = 0;
	}
	State->Changed.NotifyAll();
	return true;
}

void COrderedTaskQueue::Abort()
{
	// 取り込んだ値の破棄が別の受付を行っても、ロックを取り直せるよう外へ移す。
	TQueue<FEntry> Discarded;
	{
		FScopedLock Lock( State->Mutex );
		bAccepting = false;
		bRunning = false;
		State->bAborted = true;
		Discarded = MoveTemp( Pending );
	}
	State->Changed.NotifyAll();
}
