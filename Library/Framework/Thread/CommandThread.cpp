// SPDX-License-Identifier: MIT
#include "CommandThread.h"

#include <exception>

#include "Library/Core/Profiler/ProfilerScope.h"
#include "Library/Framework/Thread/Private/OrderedTaskQueue.h"

CCommandThread::CCommandThread( const char *Name ) : Queue( MakeShared<COrderedTaskQueue>() ), ControlThread( NThread::GetCurrentId() )
{
	// 初期化済みの共有列だけを渡し、公開オブジェクトを実行中に参照しない。
	auto Run = [Pending = Queue, Name]()
	{
		NProfiler::SetThreadName( Name ? Name : "Command Thread" );
		while ( Pending->ExecuteOne( true ) )
		{
		}
	};
	Worker = FThread( MoveTemp( Run ) );
}

CCommandThread::~CCommandThread()
{
	if ( !StopAndJoin() )
	{
		// 実行中の自分を破棄すると所有資源の寿命を守れないため、黙って切り離さず終了する。
		std::terminate();
	}
}

FTaskFence CCommandThread::Enqueue( TCallable<void()> Task, const char *Name )
{
	// 専用スレッドは既に起動しているため、追加の実行側は作らない。
	bool bUnusedStart = false;
	return Queue->Enqueue( MoveTemp( Task ), Name, bUnusedStart );
}

FTaskFence CCommandThread::GetFence() const
{
	return Queue->GetFence( false );
}

FTaskFence CCommandThread::RequestStop()
{
	return Queue->GetFence( true );
}

bool CCommandThread::StopAndJoin()
{
	if ( ControlThread != NThread::GetCurrentId() )
	{
		return false;
	}
	RequestStop();
	if ( Worker.IsJoinable() )
	{
		Worker.Join();
	}
	return true;
}
