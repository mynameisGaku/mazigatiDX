#pragma once
#include <deque>
#include <vector>

#include "Library/Core/Interfaces/Job/IJobSystem.h"
#include "Library/Core/Thread/Atomic.h"
#include "Library/Core/Thread/Mutex.h"
#include "Library/Core/Thread/Thread.h"
#include "Library/Framework/Subsystem/AppSubsystem.h"

//=============================================================================
// CJobSystem
//   IJobSystem の実装。アプリ全体で 1 つ、自動で生成される。
//
//   仕組み: ワーカーは 1 つの待ち行列を共有する（ロックは短い）。ParallelFor は「グループ」を登録して、呼び出したスレッドも
//   ワーカーと一緒にバッチを取り合う。全バッチが終わるまで、呼び出したスレッドは他のグループの仕事を手伝いながら待つ
//   （入れ子の ParallelFor でも詰まらない）。単発のジョブは待ち行列に入り、依存先が終わると実行できるようになる。
//=============================================================================
class CJobSystem : public CAppSubsystem, public IJobSystem
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
	FORCEINLINE int32 GetNumWorkers() const override { return static_cast<int32>( Workers.size() ); }
	void RunParallelRange( int32 Count, int32 MinBatchSize, FRangeFunction Body, const char* Name ) override;
	FJobHandle Schedule( std::function<void()> Function, const char* Name, const FJobHandle& Dependency ) override;
	void Wait( const FJobHandle& Handle ) override;

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
	void RunJob( const std::shared_ptr<FJobState>& Job );
	void ReleaseJob( const std::shared_ptr<FJobState>& Job );
	void Enqueue( const std::shared_ptr<FJobState>& Job );

private:
	std::vector<FThread> Workers;
	FMutex Mutex;
	FConditionVariable WorkAvailable;
	std::vector<FGroup*> Groups;                    // 実行中の ParallelFor
	std::deque<std::shared_ptr<FJobState>> ReadyJobs;   // 実行できる単発のジョブ
	bool bStopping = false;
};
