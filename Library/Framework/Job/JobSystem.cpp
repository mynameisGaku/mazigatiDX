#include "JobSystem.h"

#include <cstdlib>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

#include "Library/Core/Interfaces/Profiler/IProfiler.h"
#include "Library/Core/Log/Log.h"
#include "Library/Core/Profiler/ProfilerScope.h"
#include "Library/Core/Thread/Atomic.h"
#include "Library/Core/Thread/Mutex.h"
#include "Library/Core/Thread/Thread.h"
#include "Library/Core/Types/Callable.h"
#include "Library/Core/Types/CString.h"
#include "Library/Core/Types/Numeric.h"
#include "Library/Core/Types/Utility.h"
#include "Library/Framework/Subsystem/SubsystemCollection.h"

DEFINE_LOG_CATEGORY_STATIC( LogJob, Log )

REGISTER_SUBSYSTEM( CJobSystem )

namespace
{
	// 0 = ワーカーではないスレッド、1.. = ワーカー
	thread_local int32 GThreadIndex = 0;

	constexpr int32 MaxWorkers = 31;
	constexpr int32 MaxBatchesPerCall = 64;   // 1 回の ParallelFor を、最大でこの数のバッチに分ける（ワーカーの数には依存しない）

	// 起動引数 --job-threads=N（無ければ -1）
	int32 ReadJobThreadsArgument()
	{
		const char* CommandLine = GetCommandLineA();
		const char* Found = CommandLine ? NCString::Find( CommandLine, "--job-threads=" ) : nullptr;
		return Found != nullptr ? NCString::ToInt32( Found + NCString::Length( "--job-threads=" ) ) : -1;
	}
}

int32 IJobSystem::GetCurrentThreadIndex()
{
	return GThreadIndex;
}

#pragma region Lifecycle

void CJobSystem::Initialize( FSubsystemCollection& Collection )
{
	// プロファイラーより後に初期化させる（終了はその逆なので、ワーカーが止まってからプロファイラーが片付く）
	Collection.InitializeDependency<IProfiler>();

	int32 NumWorkers = NMath::Max( 0, static_cast<int32>( NThread::GetHardwareConcurrency() ) - 1 );
	const int32 Requested = ReadJobThreadsArgument();
	if ( Requested >= 0 ) NumWorkers = Requested;
	NumWorkers = NMath::Min( NumWorkers, MaxWorkers );

	Workers.Reserve( NumWorkers );
	for ( int32 i = 0; i < NumWorkers; ++i ) Workers.Emplace( [this, i] { WorkerMain( i ); } );

	LOG( LogJob, Log, "Ready (%d worker thread(s)%s)", NumWorkers, NumWorkers == 0 ? ": everything runs on the calling thread" : "" );
}

void CJobSystem::Deinitialize()
{
	{
		FScopedLock Lock( Mutex );
		bStopping = true;
	}
	WorkAvailable.NotifyAll();
	for ( FThread& Worker : Workers ) Worker.Join();

	LOG( LogJob, Log, "Shutdown (%d worker thread(s) stopped)", Workers.Num() );
	Workers.Reset();
	ReadyJobs.Empty();
	Groups.Reset();
}

#pragma endregion

#pragma region Workers

void CJobSystem::WorkerMain( int32 Index )
{
	GThreadIndex = Index + 1;
	char Name[24];
	NCString::Format( Name, sizeof( Name ), "Job Worker %d", Index + 1 );
	NProfiler::SetThreadName( Name );

	FUniqueLock Lock( Mutex );
	for ( ;; )
	{
		WorkAvailable.Wait( Lock, [this] { return bStopping || HasWorkLocked(); } );
		if ( bStopping ) return;
		RunOneLocked( Lock, true );
	}
}

bool CJobSystem::HasWorkLocked() const
{
	if ( !ReadyJobs.IsEmpty() ) return true;
	for ( const FGroup* Group : Groups )
	{
		if ( Group->NextBatch.Load( EMemoryOrder::Relaxed ) < Group->NumBatches ) return true;
	}
	return false;
}

bool CJobSystem::RunOneLocked( FUniqueLock& Lock, bool bAllowJobs )
{
	// データ並列のグループを優先する（呼び出し側が待っている）
	for ( FGroup* Group : Groups )
	{
		if ( Group->NextBatch.Load( EMemoryOrder::Relaxed ) >= Group->NumBatches ) continue;

		Group->Users.FetchAdd( 1, EMemoryOrder::Acquire );   // ロックの中で数える（登録を外す側が、使い終わるまで待てるように）
		Lock.Unlock();
		RunBatches( *Group );
		Group->Users.FetchSub( 1, EMemoryOrder::Release );
		Lock.Lock();
		return true;
	}

	if ( bAllowJobs && !ReadyJobs.IsEmpty() )
	{
		TSharedPtr<FJobState> Job;
		ReadyJobs.Dequeue( Job );
		Lock.Unlock();
		RunJob( Job );
		Lock.Lock();
		return true;
	}
	return false;
}

void CJobSystem::RunBatches( FGroup& Group )
{
	for ( ;; )
	{
		const int32 Batch = Group.NextBatch.FetchAdd( 1, EMemoryOrder::Relaxed );
		if ( Batch >= Group.NumBatches ) return;

		const int32 Begin = Batch * Group.BatchSize;
		const int32 End = NMath::Min( Begin + Group.BatchSize, Group.Count );
		{
			PROFILE_SCOPE_CAT( "Job", Group.Name );
			Group.Body.Invoke( Group.Body.Object, Begin, End );
		}
		Group.NumDone.FetchAdd( 1, EMemoryOrder::Release );
	}
}

#pragma endregion

#pragma region ParallelFor

void CJobSystem::RunParallelRange( int32 Count, int32 MinBatchSize, FRangeFunction Body, const char* Name )
{
	if ( Count <= 0 ) return;

	// バッチの分け方は Count と MinBatchSize だけで決まる（ワーカーの数に依存しない）
	const int32 MinBatch = NMath::Max( MinBatchSize, 1 );
	const int32 BatchSize = NMath::Max( MinBatch, ( Count + MaxBatchesPerCall - 1 ) / MaxBatchesPerCall );
	const int32 NumBatches = ( Count + BatchSize - 1 ) / BatchSize;

	// 小さな仕事、またはワーカーがいないときは、呼び出したスレッドだけで（同じ分け方で）順番に実行する
	if ( Workers.IsEmpty() || NumBatches <= 1 )
	{
		for ( int32 Batch = 0; Batch < NumBatches; ++Batch )
		{
			const int32 Begin = Batch * BatchSize;
			PROFILE_SCOPE_CAT( "Job", Name );
			Body.Invoke( Body.Object, Begin, NMath::Min( Begin + BatchSize, Count ) );
		}
		return;
	}

	FGroup Group;
	Group.Body = Body;
	Group.Name = Name;
	Group.Count = Count;
	Group.BatchSize = BatchSize;
	Group.NumBatches = NumBatches;
	{
		FScopedLock Lock( Mutex );
		Groups.Add( &Group );
	}
	WorkAvailable.NotifyAll();

	// 呼び出したスレッドも、ワーカーと一緒にバッチを取る
	RunBatches( Group );

	// 残りのバッチが終わるまで、他のグループの仕事を手伝いながら待つ（単発のジョブは長いことがあるので手伝わない）
	while ( Group.NumDone.Load( EMemoryOrder::Acquire ) < NumBatches )
	{
		FUniqueLock Lock( Mutex );
		if ( !RunOneLocked( Lock, false ) )
		{
			Lock.Unlock();
			NThread::YieldToOthers();
		}
	}

	{
		FScopedLock Lock( Mutex );
		Groups.RemoveSingle( &Group );
	}
	// 登録を外したあとは新しく使う者はいない。使っている最中のワーカーが抜けるまで待つ（Group は呼び出しスレッドの上にあるため）
	while ( Group.Users.Load( EMemoryOrder::Acquire ) > 0 ) NThread::YieldToOthers();
}

#pragma endregion

#pragma region Jobs

FJobHandle CJobSystem::Schedule( TCallable<void()> Function, const char* Name, const FJobHandle& Dependency )
{
	TSharedPtr<FJobState> Job = MakeShared<FJobState>();
	Job->Function = MoveTemp( Function );
	Job->Name = Name;
	Job->NumPending.Store( 1, EMemoryOrder::Relaxed );   // 登録が終わるまで実行されないよう、自分の分を 1 つ持っておく

	if ( Dependency.State != nullptr )
	{
		FScopedLock Lock( Dependency.State->Mutex );
		if ( !Dependency.State->bDone.Load( EMemoryOrder::Relaxed ) )
		{
			Dependency.State->Dependents.Add( Job );
			Job->NumPending.FetchAdd( 1, EMemoryOrder::Relaxed );
		}
	}

	ReleaseJob( Job );
	return FJobHandle( Job );
}

void CJobSystem::ReleaseJob( const TSharedPtr<FJobState>& Job )
{
	if ( Job->NumPending.FetchSub( 1, EMemoryOrder::AcquireRelease ) == 1 ) Enqueue( Job );
}

void CJobSystem::Enqueue( const TSharedPtr<FJobState>& Job )
{
	// ワーカーがいないときは、その場で実行する（依存先もその場で終わっているので、順番は守られる）
	if ( Workers.IsEmpty() )
	{
		RunJob( Job );
		return;
	}

	{
		FScopedLock Lock( Mutex );
		ReadyJobs.Enqueue( Job );
	}
	WorkAvailable.NotifyOne();
}

void CJobSystem::RunJob( const TSharedPtr<FJobState>& Job )
{
	{
		PROFILE_SCOPE_CAT( "Job", Job->Name );
		Job->Function();
	}
	Job->Function = nullptr;   // 取り込んだものを早く手放す

	TArray<TSharedPtr<FJobState>> Dependents;
	{
		FScopedLock Lock( Job->Mutex );
		Job->bDone.Store( true, EMemoryOrder::Release );
		Dependents = MoveTemp( Job->Dependents );
	}
	for ( const TSharedPtr<FJobState>& Dependent : Dependents ) ReleaseJob( Dependent );
}

void CJobSystem::Wait( const FJobHandle& Handle )
{
	// 待っている間も、仕事（グループ・単発のジョブ）を手伝う
	while ( !Handle.IsDone() )
	{
		FUniqueLock Lock( Mutex );
		if ( !RunOneLocked( Lock, true ) )
		{
			Lock.Unlock();
			NThread::YieldToOthers();
		}
	}
}

#pragma endregion
