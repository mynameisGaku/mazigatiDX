// SPDX-License-Identifier: MIT
#include "Library/Core/Thread/GameThread.h"

#include "Library/Core/Collection/Array.h"
#include "Library/Core/Log/Log.h"
#include "Library/Core/Thread/Atomic.h"
#include "Library/Core/Thread/Mutex.h"
#include "Library/Core/Thread/Thread.h"

DEFINE_LOG_CATEGORY_STATIC( LogGameThread, Log )

namespace
{
	struct FPendingTask
	{
		TCallable<void()> Task;
		const char* Name = "Task";
	};

	struct FGameThreadState
	{
		TAtomic<uint64> Id{ 0 };
		TAtomic<bool> bBound{ false };
		TAtomic<int32> NumWrongThreadCalls{ 0 };
		FMutex Mutex;
		TArray<FPendingTask> Pending;
	};

	FGameThreadState& State()
	{
		static FGameThreadState Instance;
		return Instance;
	}
}

void NGameThread::Bind()
{
	State().Id.Store( NThread::GetCurrentId(), EMemoryOrder::Release );
	State().bBound.Store( true, EMemoryOrder::Release );
}

bool NGameThread::IsBound()
{
	return State().bBound.Load( EMemoryOrder::Acquire );
}

bool NGameThread::IsCurrent()
{
	return !IsBound() || State().Id.Load( EMemoryOrder::Acquire ) == NThread::GetCurrentId();
}

void NGameThread::ReportWrongThread( const char* Function )
{
	State().NumWrongThreadCalls.FetchAdd( 1, EMemoryOrder::Relaxed );
	LOG( LogGameThread, Error, "%s must be called on the game thread (it was called on another thread and did nothing). Use AsyncTask( EThreadType::Game, ... ) to run it on the game thread.", Function != nullptr ? Function : "?" );
}

int32 NGameThread::GetNumWrongThreadCalls()
{
	return State().NumWrongThreadCalls.Load( EMemoryOrder::Relaxed );
}

void NGameThread::Enqueue( TCallable<void()> Task, const char* Name )
{
	if ( !Task )
	{
		return;
	}
	FScopedLock Lock( State().Mutex );
	State().Pending.Add( FPendingTask { MoveTemp( Task ), Name != nullptr ? Name : "Task" } );
}

int32 NGameThread::RunPendingTasks()
{
	if ( !IsCurrent() )
	{
		return 0;
	}
	// 実行中に積まれた仕事は、次の呼び出しへ回す（取り出してから、鍵を外して実行する）
	TArray<FPendingTask> Tasks;
	{
		FScopedLock Lock( State().Mutex );
		Tasks = MoveTemp( State().Pending );
		State().Pending.Reset();
	}
	for ( FPendingTask& Pending : Tasks )
	{
		Pending.Task();
	}
	return Tasks.Num();
}

int32 NGameThread::GetNumPendingTasks()
{
	FScopedLock Lock( State().Mutex );
	return State().Pending.Num();
}
