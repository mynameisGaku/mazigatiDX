// SPDX-License-Identifier: MIT
#pragma once
#include "Library/Core/Collection/Array.h"
#include "Library/Core/Collection/Queue.h"
#include "Library/Core/Interfaces/Job/IJobSystem.h"
#include "Library/Core/Thread/Atomic.h"
#include "Library/Core/Thread/JobState.h"
#include "Library/Core/Thread/Mutex.h"
#include "Library/Core/Thread/Thread.h"
#include "Library/Core/Types/Callable.h"
#include "Library/Framework/Subsystem/AppSubsystem.h"

//=============================================================================
// CJobSubsystem
//   IJobSystem の実装。アプリ全体で 1 つ、自動で生成される。
//
//   仕組み: ワーカーは 1 つの待ち行列を共有する（ロックは短い）。ParallelFor は「グループ」を登録して、呼び出したスレッドも
//   ワーカーと一緒にバッチを取り合う。全バッチが終わるまで、呼び出したスレッドは他のグループの仕事を手伝いながら待つ
//   （入れ子の ParallelFor でも詰まらない）。単発のジョブは待ち行列に入り、依存先が終わると実行できるようになる。
//=============================================================================
class CJobSubsystem : public CAppSubsystem, public IJobSystem
{
public:
	//-------------------------------------------------------------------------
	// CAppSubsystem
	//-------------------------------------------------------------------------
	void Initialize( FSubsystemCollection& Collection ) override;
	void Deinitialize() override;

	//-------------------------------------------------------------------------
	// IJobSystem
	//-------------------------------------------------------------------------
	FORCEINLINE int32 GetNumWorkers() const override { return Workers.Num(); }
	void RunParallelRange( int32 Count, int32 MinBatchSize, FRangeFunction Body, const char* Name ) override;
	FJobHandle Schedule( TCallable<void()> Function, const char* Name, const FJobHandle& Dependency = FJobHandle() ) override;
	void Wait( const FJobHandle& Handle ) override;
	// 所有する仕事がなくなるまで待つ。仕事本体からは呼べない。
	bool WaitIdle() override;

private:
	// ParallelFor 1 回分。呼び出したスレッドの上にあり、全員が使い終わるまで呼び出し側が待つ
	struct FGroup
	{
		FRangeFunction Body;
		const char* Name = "ParallelFor";
		int32 Count = 0;
		int32 BatchSize = 1;
		int32 NumBatches = 1;
		TAtomic<int32> NextBatch{ 0 };   // 次に取るバッチ
		TAtomic<int32> NumDone{ 0 };     // 終わったバッチ
		TAtomic<int32> Users{ 0 };       // 今、このグループを調べているワーカーの数
	};

	void WorkerMain( int32 Index );
	// 待ち行列から仕事を 1 つ取って実行する（Lock は実行中だけ外す）。仕事が無ければ false
	bool RunOneLocked( FUniqueLock& Lock, bool bAllowJobs );
	bool HasWorkLocked() const;
	void RunBatches( FGroup& Group );
	void RunJob( const TSharedPtr<FJobState>& Job );
	void ReleaseJob( const TSharedPtr<FJobState>& Job );
	void Enqueue( const TSharedPtr<FJobState>& Job );

private:
	TArray<FThread> Workers;
	FMutex Mutex;
	FConditionVariable WorkAvailable;
	TArray<FGroup*> Groups;                    // 実行中の ParallelFor
	TQueue<TSharedPtr<FJobState>> ReadyJobs;   // 実行できる単発のジョブ
	// 受付済みで、依存待ち・実行待ち・実行中のジョブ総数。Mutexで保護する。
	uint64 OutstandingJobs = 0;
	// 終了中は外部からの新規受付を拒否し、実行済みジョブからの子だけ受け付ける。
	bool bStopping = false;
};
