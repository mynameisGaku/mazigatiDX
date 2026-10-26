#include "System.h"

#include <algorithm>
#include <cstring>

#include "Library/Core/Profiler/ProfilerScope.h"
#include "Library/Core/Types/Numeric.h"
#include "Library/Core/Types/Utility.h"

namespace
{
	// 1 回の ParallelFor の仕事 1 つ（あるシステムの、対象の一部）
	struct FWorkItem
	{
		NEcs::ISystemRunner* Runner;
		const char* Name;
		int32 Begin;
		int32 End;
	};
}

bool NEcs::FSystemScheduler::Conflicts( const FSystem& A, const FSystem& B )
{
	for ( const FAccess& X : A.Access )
	{
		for ( const FAccess& Y : B.Access )
		{
			if ( X.Key == Y.Key && ( X.bWrite || Y.bWrite ) ) return true;
		}
	}
	return false;
}

void NEcs::FSystemScheduler::AddSystem( FSystem&& System )
{
	// 先に登録された、衝突するシステム（同じフェーズ）より必ず後の段に置く
	System.Stage = 0;
	for ( const FSystem& Existing : Systems )
	{
		if ( Existing.Phase == System.Phase && Conflicts( Existing, System ) ) System.Stage = NMath::Max( System.Stage, Existing.Stage + 1 );
	}
	if ( System.Phase == EPhase::Draw ) DrawSystems.Add( Systems.Num() );
	Systems.Add( MoveTemp( System ) );
}

int32 NEcs::FSystemScheduler::GetStage( const char* Name ) const
{
	for ( const FSystem& System : Systems )
	{
		if ( std::strcmp( System.Name, Name ) == 0 ) return System.Stage;
	}
	return INDEX_NONE;
}

int32 NEcs::FSystemScheduler::GetNumStages( EPhase Phase ) const
{
	int32 NumStages = 0;
	for ( const FSystem& System : Systems )
	{
		if ( System.Phase == Phase ) NumStages = NMath::Max( NumStages, System.Stage + 1 );
	}
	return NumStages;
}

void NEcs::FSystemScheduler::Run( EPhase Phase, float32 DeltaTime, uint64 MaxObjectId, FEcsWorld& World, FTransformWorld& Transforms, IJobSystem* Jobs )
{
	const int32 NumStages = GetNumStages( Phase );
	thread_local TArray<FWorkItem> Items;

	// 描画システムはワールド値を読むだけにしたいので、先に全部を最新にしておく（古いものが無ければ、すぐ戻る）
	if ( Phase == EPhase::Draw )
	{
		Transforms.Update( Jobs );
		ActiveDrawRunners.Reset();
	}

	for ( int32 Stage = 0; Stage < NumStages; ++Stage )
	{
		// この段のシステム（互いに触るデータが無い）の仕事を、塊に分けて 1 つの一覧にする
		Items.Reset();
		for ( FSystem& System : Systems )
		{
			if ( System.Phase != Phase || System.Stage != Stage ) continue;

			const int32 Count = System.Runner->Prepare( World, Transforms, DeltaTime, MaxObjectId );
			if ( Phase == EPhase::Draw && Count > 0 )
			{
				ActiveDrawRunners.Add( System.Runner.get() );
			}
			if ( IProfiler* Recorder = NProfiler::GRecorder )
			{
				Recorder->RecordCounter( Recorder->InternString( *FString::Printf( "Ecs.Entities.%s", System.Name ) ), Count, EProfileCounterUnit::Count );
			}
			for ( int32 Begin = 0; Begin < Count; Begin += EntitiesPerBatch )
			{
				Items.Add( { System.Runner.get(), System.Name, Begin, NMath::Min( Begin + EntitiesPerBatch, Count ) } );
			}
		}
		if ( Items.IsEmpty() ) continue;

		const FWorkItem* WorkData = Items.GetData();
		const auto Body = [WorkData]( int32 First, int32 Last )
		{
			for ( int32 i = First; i < Last; ++i )
			{
				const FWorkItem& Item = WorkData[i];
				PROFILE_SCOPE_CAT( "System", Item.Name );
				Item.Runner->RunRange( Item.Begin, Item.End );
			}
		};
		if ( Jobs != nullptr && Jobs->GetNumWorkers() > 0 ) Jobs->ParallelForRange( Items.Num(), 1, "Systems", Body );
		else Body( 0, Items.Num() );

		// 並列に溜めた「ワールド値を古くする」「破棄の予約」を、メインスレッドでまとめて反映する（次の段の前に）
		if ( Phase != EPhase::Draw )
		{
			Transforms.FlushDeferredDirty();
			World.FlushDeferred();
		}
	}
}
