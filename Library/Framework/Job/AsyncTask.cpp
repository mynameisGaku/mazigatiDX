// SPDX-License-Identifier: MIT
#include "Library/Framework/Job/AsyncTask.h"

#include "Library/Core/Interfaces/Job/IJobSystem.h"
#include "Library/Core/Thread/GameThread.h"
#include "Library/Framework/Engine/Engine.h"

void AsyncTask( EThreadType Thread, TCallable<void()> Task, const char* Name )
{
	if ( !Task )
	{
		return;
	}
	if ( Thread == EThreadType::Game )
	{
		NGameThread::Enqueue( MoveTemp( Task ), Name );
		return;
	}
	IJobSystem* Jobs = GetAppSubsystem<IJobSystem>();
	if ( Jobs == nullptr )
	{
		// ジョブシステムが無い（エンジンの外・終わったあと）ときは、その場で実行する
		Task();
		return;
	}
	Jobs->Schedule( MoveTemp( Task ), Name );
}
