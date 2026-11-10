// SPDX-License-Identifier: MIT
#include "JobPipe.h"

#include "Library/Core/Interfaces/Job/IJobSystem.h"
#include "Library/Framework/Thread/Private/OrderedTaskQueue.h"

CJobPipe::CJobPipe( IJobSystem &InJobs ) : Jobs( InJobs ), Queue( MakeShared<COrderedTaskQueue>() ) {}

CJobPipe::~CJobPipe()
{
	Close();
}

FTaskFence CJobPipe::Enqueue( TCallable<void()> Task, const char *Name )
{
	// 実行は受付ロックを外してから始める。ワーカー0の再受付でも二重ロックしない。
	bool bStart = false;
	const FTaskFence Fence = Queue->Enqueue( MoveTemp( Task ), Name, bStart );
	if ( bStart )
	{
		if ( !ScheduleDrain( Jobs, Queue ) )
		{
			return {};
		}
	}
	return Fence;
}

FTaskFence CJobPipe::GetFence() const
{
	return Queue->GetFence( false );
}

FTaskFence CJobPipe::Close()
{
	return Queue->GetFence( true );
}

bool CJobPipe::ScheduleDrain( IJobSystem &Jobs, TSharedPtr<COrderedTaskQueue> Pending )
{
	// 公開オブジェクトを保持せず、実行列と生存が契約されたスケジューラーだけを渡す。
	auto Drain = [&Jobs, Pending]()
	{
		// ワーカー0では再帰的なジョブ登録を避け、その場で残りを実行する。
		if ( Jobs.GetNumWorkers() == 0 )
		{
			while ( Pending->ExecuteOne( false ) )
			{
			}
			return;
		}
		// 短い仕事64件ごとにワーカーを他のジョブへ返す。実行側は常に1つだけ。
		for ( int32 Index = 0; Index < 64; ++Index )
		{
			if ( !Pending->ExecuteOne( false ) )
			{
				return;
			}
		}
		ScheduleDrain( Jobs, Pending );
	};
	if ( !Jobs.Schedule( MoveTemp( Drain ), "JobPipe.Drain" ).IsValid() )
	{
		Pending->Abort();
		return false;
	}
	return true;
}
