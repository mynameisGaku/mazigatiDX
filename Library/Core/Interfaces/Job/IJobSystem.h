#pragma once
#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <type_traits>
#include <utility>
#include <vector>

#include "Library/Core/Types/CompilerMacros.h"
#include "Library/Core/Types/TypeDefinition.h"

//=============================================================================
// FJobHandle
//   Schedule で投げた単発のジョブの控え。終わったかを調べたり、待ったり、次のジョブの依存先にしたりできる。
//=============================================================================
struct FJobState
{
	std::function<void()> Function;
	const char* Name = "Job";
	std::atomic<int32> NumPending{ 0 };   // 自分の実行待ち 1 + 終わっていない依存先の数。0 になったら実行できる
	std::atomic<bool> bDone{ false };
	std::mutex Mutex;                     // bDone の切り替えと Dependents を守る
	std::vector<std::shared_ptr<FJobState>> Dependents;   // このジョブが終わったら実行できるようになるジョブ
};

class FJobHandle
{
public:
	FJobHandle() = default;
	explicit FJobHandle( std::shared_ptr<FJobState> InState ) : State( std::move( InState ) ) {}

	// 空のハンドル（何も待たない）は終わっている扱い
	FORCEINLINE bool IsDone() const { return State == nullptr || State->bDone.load( std::memory_order_acquire ); }
	FORCEINLINE bool IsValid() const { return State != nullptr; }

	std::shared_ptr<FJobState> State;
};

//=============================================================================
// IJobSystem
//   ワーカースレッドのプール。GetAppSubsystem<IJobSystem>() で取得する（常にある。ワーカーが 0 なら呼び出したスレッドで順番に実行する）。
//
//   データ並列: 0..Count-1 を分けて、複数のスレッドで同時に実行する。全部終わってから戻る（呼び出したスレッドも実行に加わる）
//       Jobs->ParallelFor( Count, 1024, "Enemy.Update", [&]( int32 i ) { Enemies[i].Update(); } );
//       Jobs->ParallelForRange( Count, 1024, "Enemy.Update", [&]( int32 Begin, int32 End ) { ... } );   // 範囲ごとに受け取る（準備を使い回せる）
//     - 範囲の分け方は Count と MinBatchSize だけで決まり、ワーカーの数や実行の順番には依存しない。
//       各要素が自分のデータだけを書き込むなら、ワーカーの数が違っても結果は完全に同じになる
//     - Count が MinBatchSize 以下なら、呼び出したスレッドだけで実行する（小さな仕事のためにスレッドを起こさない）
//     - 関数の中からも呼べる（入れ子）。例外は投げない
//
//   単発: Schedule で投げて、Wait で待つ。Dependency に別のジョブを渡すと、それが終わってから実行される
//       FJobHandle A = Jobs->Schedule( []{ Load(); }, "Load" );
//       FJobHandle B = Jobs->Schedule( []{ Build(); }, "Build", A );
//       Jobs->Wait( B );
//
//   プロファイラー: 実行するたびに Category "Job"、Name は引数の名前（文字列リテラル）で計測される。
//   ワーカーは "Job Worker N" という名前で、タイムラインのスレッドの行に出る。
//
//   起動引数 --job-threads=N でワーカーの数を決められる（既定は論理コア数 - 1。0 で、すべて呼び出したスレッドで実行する）
//=============================================================================
class IJobSystem
{
public:
	// 範囲を担当する関数への軽い参照（ParallelFor が戻るまで有効なので、コピーも確保もしない）
	struct FRangeFunction
	{
		void* Object = nullptr;
		void ( *Invoke )( void*, int32, int32 ) = nullptr;
	};

	virtual ~IJobSystem() = default;

	// ワーカーの数（呼び出したスレッドは含まない）
	virtual int32 GetNumWorkers() const = 0;

	virtual void RunParallelRange( int32 Count, int32 MinBatchSize, FRangeFunction Body, const char* Name ) = 0;

	virtual FJobHandle Schedule( std::function<void()> Function, const char* Name, const FJobHandle& Dependency = FJobHandle() ) = 0;
	virtual void Wait( const FJobHandle& Handle ) = 0;

	// 今のスレッドの番号。0 = ワーカーではないスレッド（メインスレッドなど）、1.. = ワーカー
	static int32 GetCurrentThreadIndex();

	//-------------------------------------------------------------------------
	// 書きやすいラッパー
	//-------------------------------------------------------------------------
	template<typename TFunction>
	FORCEINLINE void ParallelForRange( int32 Count, int32 MinBatchSize, const char* Name, TFunction&& Function )
	{
		using TDecayed = std::remove_reference_t<TFunction>;
		FRangeFunction Body;
		Body.Object = const_cast<void*>( static_cast<const void*>( &Function ) );
		Body.Invoke = []( void* Object, int32 Begin, int32 End ) { ( *static_cast<TDecayed*>( Object ) )( Begin, End ); };
		RunParallelRange( Count, MinBatchSize, Body, Name );
	}

	template<typename TFunction>
	FORCEINLINE void ParallelFor( int32 Count, int32 MinBatchSize, const char* Name, TFunction&& Function )
	{
		ParallelForRange( Count, MinBatchSize, Name, [&Function]( int32 Begin, int32 End )
		{
			for ( int32 i = Begin; i < End; ++i ) Function( i );
		} );
	}
};
