// SPDX-License-Identifier: MIT
#include "TaskFence.h"

#include "Library/Core/Thread/TaskFenceState.h"
#include "Library/Core/Thread/Thread.h"

FTaskFence::FTaskFence( TSharedPtr<FTaskFenceState> InState, uint64 InSequence ) : State( MoveTemp( InState ) ), Sequence( InSequence ) {}

bool FTaskFence::IsValid() const
{
	return State != nullptr;
}

bool FTaskFence::IsComplete() const
{
	if ( !State )
	{
		return false;
	}
	FScopedLock Lock( State->Mutex );
	return State->Completed >= Sequence;
}

bool FTaskFence::Wait( uint32 TimeoutMilliseconds ) const
{
	if ( !State || TimeoutMilliseconds > 60000 )
	{
		return false;
	}
	FUniqueLock Lock( State->Mutex );
	if ( State->Completed >= Sequence )
	{
		return true;
	}
	if ( State->ExecutingThread == NThread::GetCurrentId() || State->bAborted )
	{
		return false;
	}
	// 完了または実行不能を、同じ共有ロックの下で調べる。
	const auto HasFinished = [this]() { return State->Completed >= Sequence || State->bAborted; };
	State->Changed.WaitFor( Lock, TimeoutMilliseconds, HasFinished );
	return State->Completed >= Sequence;
}

uint64 FTaskFence::GetSequence() const
{
	return Sequence;
}
