#include "JobSystem.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

#include "Library/Core/Interfaces/Profiler/IProfiler.h"
#include "Library/Core/Log/Log.h"
#include "Library/Core/Profiler/ProfilerScope.h"
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
		const char* Found = CommandLine ? std::strstr( CommandLine, "--job-threads=" ) : nullptr;
		return Found != nullptr ? std::atoi( Found + std::strlen( "--job-threads=" ) ) : -1;
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

	int32 NumWorkers = NMath::Max( 0, static_cast<int32>( std::thread::hardware_concurrency() ) - 1 );
	const int32 Requested = ReadJobThreadsArgument();
	if ( Requested >= 0 ) NumWorkers = Requested;
	NumWorkers = NMath::Min( NumWorkers, MaxWorkers );

	Workers.reserve( static_cast<size_t>( NumWorkers ) );
	for ( int32 i = 0; i < NumWorkers; ++i ) Workers.emplace_back( &CJobSystem::WorkerMain, this, i );

	LOG( LogJob, Log, "Ready (%d worker thread(s)%s)", NumWorkers, NumWorkers == 0 ? ": everything runs on the calling thread" : "" );
}

void CJobSystem::Deinitialize()
{
	{
		std::lock_guard<std::mutex> Lock( Mutex );
		bStopping = true;
	}
	WorkAvailable.notify_all();
	for ( std::thread& Worker : Workers ) Worker.join();

	LOG( LogJob, Log, "Shutdown (%d worker thread(s) stopped)", static_cast<int32>( Workers.size() ) );
	Workers.clear();
	ReadyJobs.clear();
	Groups.clear();
}

#pragma endregion

#pragma region Workers

void CJobSystem::WorkerMain( int32 Index )
{
	GThreadIndex = Index + 1;
	char Name[24];
	std::snprintf( Name, sizeof( Name ), "Job Worker %d", Index + 1 );
	NProfiler::SetThreadName( Name );

	std::unique_lock<std::mutex> Lock( Mutex );
	for ( ;; )
	{
		WorkAvailable.wait( Lock, [this] { return bStopping || HasWorkLocked(); } );
		if ( bStopping ) return;
		RunOneLocked( Lock, true );
	}
}

bool CJobSystem::HasWorkLocked() const
{
	if ( !ReadyJobs.empty() ) return true;
	for ( const FGroup* Group : Groups )
	{
		if ( Group->NextBatch.load( std::memory_order_relaxed ) < Group->NumBatches ) return true;
	}
	return false;
}

bool CJobSystem::RunOneLocked( std::unique_lock<std::mutex>& Lock, bool bAllowJobs )
{
	// データ並列のグループを優先する（呼び出し側が待っている）
	for ( FGroup* Group : Groups )
	{
		if ( Group->NextBatch.load( std::memory_order_relaxed ) >= Group->NumBatches ) continue;

		Group->Users.fetch_add( 1, std::memory_order_acquire );   // ロックの中で数える（登録を外す側が、使い終わるまで待てるように）
		Lock.unlock();
		RunBatches( *Group );
		Group->Users.fetch_sub( 1, std::memory_order_release );
		Lock.lock();
		return true;
	}

	if ( bAllowJobs && !ReadyJobs.empty() )
	{
		std::shared_ptr<FJobState> Job = MoveTemp( ReadyJobs.front() );
		ReadyJobs.pop_front();
		Lock.unlock();
		RunJob( Job );
		Lock.lock();
		return true;
	}
	return false;
}

void CJobSystem::RunBatches( FGroup& Group )
{
	for ( ;; )
	{
		const int32 Batch = Group.NextBatch.fetch_add( 1, std::memory_order_relaxed );
		if ( Batch >= Group.NumBatches ) return;

		const int32 Begin = Batch * Group.BatchSize;
		const int32 End = NMath::Min( Begin + Group.BatchSize, Group.Count );
		{
			PROFILE_SCOPE_CAT( "Job", Group.Name );
			Group.Body.Invoke( Group.Body.Object, Begin, End );
		}
		Group.NumDone.fetch_add( 1, std::memory_order_release );
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
	if ( Workers.empty() || NumBatches <= 1 )
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
		std::lock_guard<std::mutex> Lock( Mutex );
		Groups.push_back( &Group );
	}
	WorkAvailable.notify_all();

	// 呼び出したスレッドも、ワーカーと一緒にバッチを取る
	RunBatches( Group );

	// 残りのバッチが終わるまで、他のグループの仕事を手伝いながら待つ（単発のジョブは長いことがあるので手伝わない）
	while ( Group.NumDone.load( std::memory_order_acquire ) < NumBatches )
	{
		std::unique_lock<std::mutex> Lock( Mutex );
		if ( !RunOneLocked( Lock, false ) )
		{
			Lock.unlock();
			std::this_thread::yield();
		}
	}

	{
		std::lock_guard<std::mutex> Lock( Mutex );
		Groups.erase( std::find( Groups.begin(), Groups.end(), &Group ) );
	}
	// 登録を外したあとは新しく使う者はいない。使っている最中のワーカーが抜けるまで待つ（Group は呼び出しスレッドの上にあるため）
	while ( Group.Users.load( std::memory_order_acquire ) > 0 ) std::this_thread::yield();
}

#pragma endregion

#pragma region Jobs

FJobHandle CJobSystem::Schedule( std::function<void()> Function, const char* Name, const FJobHandle& Dependency )
{
	std::shared_ptr<FJobState> Job = std::make_shared<FJobState>();
	Job->Function = MoveTemp( Function );
	Job->Name = Name;
	Job->NumPending.store( 1, std::memory_order_relaxed );   // 登録が終わるまで実行されないよう、自分の分を 1 つ持っておく

	if ( Dependency.State != nullptr )
	{
		std::lock_guard<std::mutex> Lock( Dependency.State->Mutex );
		if ( !Dependency.State->bDone.load( std::memory_order_relaxed ) )
		{
			Dependency.State->Dependents.push_back( Job );
			Job->NumPending.fetch_add( 1, std::memory_order_relaxed );
		}
	}

	ReleaseJob( Job );
	return FJobHandle( Job );
}

void CJobSystem::ReleaseJob( const std::shared_ptr<FJobState>& Job )
{
	if ( Job->NumPending.fetch_sub( 1, std::memory_order_acq_rel ) == 1 ) Enqueue( Job );
}

void CJobSystem::Enqueue( const std::shared_ptr<FJobState>& Job )
{
	// ワーカーがいないときは、その場で実行する（依存先もその場で終わっているので、順番は守られる）
	if ( Workers.empty() )
	{
		RunJob( Job );
		return;
	}

	{
		std::lock_guard<std::mutex> Lock( Mutex );
		ReadyJobs.push_back( Job );
	}
	WorkAvailable.notify_one();
}

void CJobSystem::RunJob( const std::shared_ptr<FJobState>& Job )
{
	{
		PROFILE_SCOPE_CAT( "Job", Job->Name );
		Job->Function();
	}
	Job->Function = nullptr;   // 取り込んだものを早く手放す

	std::vector<std::shared_ptr<FJobState>> Dependents;
	{
		std::lock_guard<std::mutex> Lock( Job->Mutex );
		Job->bDone.store( true, std::memory_order_release );
		Dependents.swap( Job->Dependents );
	}
	for ( const std::shared_ptr<FJobState>& Dependent : Dependents ) ReleaseJob( Dependent );
}

void CJobSystem::Wait( const FJobHandle& Handle )
{
	// 待っている間も、仕事（グループ・単発のジョブ）を手伝う
	while ( !Handle.IsDone() )
	{
		std::unique_lock<std::mutex> Lock( Mutex );
		if ( !RunOneLocked( Lock, true ) )
		{
			Lock.unlock();
			std::this_thread::yield();
		}
	}
}

#pragma endregion
