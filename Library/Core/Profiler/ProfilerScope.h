#pragma once
#include "Library/Core/Interfaces/Profiler/IProfiler.h"

//=============================================================================
// PROFILE_SCOPE
//   このマクロを書いた位置から、囲んでいるブロックの終わりまでの時間を計測する。
//
//   void CEnemy::Tick( float32 DeltaTime )
//   {
//       PROFILE_SCOPE( "Enemy.Tick" );                       // 名前だけ
//       {
//           PROFILE_SCOPE_CAT( "AI", "FindTarget" );         // 分類 (Category) と名前
//           ...
//       }
//       PROFILE_SCOPE_OWNER( "AI", "Squad", "Pathfinding" ); // 持ち主 (Owner) も付ける
//   }
//   void Foo() { PROFILE_FUNCTION(); ... }                    // 関数名で計測
//
//   - 文字列は、ずっと有効なポインタ（文字列リテラルなど）を渡す。FString::c_str() のような一時的なものは不可
//   - 計測しない間（IProfiler::SetEnabled( false )）のコストは、ポインタが null かの確認だけ
//   - COG_PROFILER_ENABLED を 0 にすると、マクロごと消える（出荷ビルド用）
//
//   実行時に決まる値（シーン名・オブジェクトの名前など）を付けたいときは PROFILE_SCOPE_LAZY を使う。
//   式は「記録中のときだけ」評価されるので、止まっている間は名前の文字列を作るコストがかからない:
//
//       PROFILE_SCOPE_LAZY( Scope, MakeProfileScope( "Object.Tick" ) );   // FProfileScope を返す式
//=============================================================================
#ifndef COG_PROFILER_ENABLED
	#define COG_PROFILER_ENABLED 1
#endif

namespace NProfiler
{
	// 今フレームを記録している間だけ有効なプロファイラー（それ以外は nullptr）。
	// 計測コードがサブシステムの検索なしに「記録中か」を 1 回の読み込みで知るための入口で、IProfiler の実装が管理する
	inline IProfiler* GRecorder = nullptr;

	// このスレッドの名前（ジョブシステムのワーカーなどが、スレッドの開始時に 1 回呼ぶ）。タイムラインのスレッドの行に出る。
	//   NProfiler::SetThreadName( "Worker 3" );
	// 文字列はスレッドが動いている間、有効なものを渡す（文字列リテラル、またはそのスレッド用に確保した領域）
	inline thread_local const char* GThreadName = nullptr;
	FORCEINLINE void SetThreadName( const char* Name ) { GThreadName = Name; }
}

//=============================================================================
// PROFILE_COUNTER
//   時間ではない値（メモリ使用量・個数・キューの長さなど）を、今のフレームに記録する。
//   プロファイラーのウィンドウの Counters タブに、フレームごとの推移つきで出る（Chrome トレースにも出力される）。
//
//   PROFILE_COUNTER( "Asset.Count", NumAssets );                  // 個数
//   PROFILE_COUNTER_BYTES( "Asset.Memory.Texture", TextureBytes );  // バイト数（KB / MB で表示される）
//   PROFILE_COUNTER_MS( "Audio.Update", Milliseconds );
//   PROFILE_COUNTER_PERCENT( "Pool.Usage", Percent );
//
//   - 名前は、ずっと有効なポインタ（文字列リテラル）を渡す。'.' で区切ると、分類として並ぶ
//   - 値は「記録中のときだけ」評価される（止まっている間は式のコストがかからない）
//   - 計測しない間（IProfiler::SetEnabled( false )）のコストは、ポインタが null かの確認だけ
//=============================================================================
// FProfileScope
//   コンストラクタで開始、デストラクタで終了。文字列は IProfiler がポインタで ID に変換する
//=============================================================================
class FProfileScope
{
public:
	// 何も計測しない（PROFILE_SCOPE_LAZY が記録中でないときに使う）
	FProfileScope() = default;

	FORCEINLINE FProfileScope( const char* Category, const char* Owner, const char* Name, uint64 Instance = 0, uint32 Label = 0 )
	{
		if ( IProfiler* Recorder = NProfiler::GRecorder )
		{
			Target = Recorder;
			Index = Recorder->BeginSample( Recorder->InternLiteral( Category ), Recorder->InternLiteral( Owner ),
										   Recorder->InternLiteral( Name ), Instance, Label );
		}
	}

	// Owner だけ ID で渡す（シーン名のように実行時に決まる持ち主を、毎回の文字列検索なしで付ける）
	FORCEINLINE FProfileScope( const char* Category, uint32 OwnerId, const char* Name, uint64 Instance = 0, uint32 Label = 0 )
	{
		if ( IProfiler* Recorder = NProfiler::GRecorder )
		{
			Target = Recorder;
			Index = Recorder->BeginSample( Recorder->InternLiteral( Category ), OwnerId, Recorder->InternLiteral( Name ), Instance, Label );
		}
	}

	// すべて ID で渡す
	FORCEINLINE FProfileScope( uint32 Category, uint32 Owner, uint32 Name, uint64 Instance, uint32 Label )
	{
		if ( IProfiler* Recorder = NProfiler::GRecorder )
		{
			Target = Recorder;
			Index = Recorder->BeginSample( Category, Owner, Name, Instance, Label );
		}
	}

	FORCEINLINE ~FProfileScope()
	{
		if ( Target != nullptr ) Target->EndSample( Index );
	}

	FProfileScope( const FProfileScope& ) = delete;
	FProfileScope& operator=( const FProfileScope& ) = delete;

private:
	IProfiler* Target = nullptr;
	int64 Index = INDEX_NONE;
};

#if COG_PROFILER_ENABLED
	#define PROFILE_CONCAT_INNER( A, B ) A##B
	#define PROFILE_CONCAT( A, B ) PROFILE_CONCAT_INNER( A, B )
	#define PROFILE_SCOPE_VAR PROFILE_CONCAT( ProfileScope_, __LINE__ )

	#define PROFILE_SCOPE( Name )                         FProfileScope PROFILE_SCOPE_VAR( "General", "", Name )
	#define PROFILE_SCOPE_CAT( Category, Name )           FProfileScope PROFILE_SCOPE_VAR( Category, "", Name )
	#define PROFILE_SCOPE_OWNER( Category, Owner, Name )  FProfileScope PROFILE_SCOPE_VAR( Category, Owner, Name )
	#define PROFILE_FUNCTION()                            FProfileScope PROFILE_SCOPE_VAR( "Function", "", __FUNCTION__ )

	// Expression（FProfileScope を返す式）は、記録中のときだけ評価される
	#define PROFILE_SCOPE_LAZY( Var, Expression )         FProfileScope Var = NProfiler::GRecorder ? ( Expression ) : FProfileScope()

	#define PROFILE_COUNTER_UNIT( Name, Value, Unit ) \
		do { if ( IProfiler* ProfileRecorder = NProfiler::GRecorder ) ProfileRecorder->RecordCounter( ProfileRecorder->InternLiteral( Name ), static_cast<float64>( Value ), Unit ); } while ( 0 )
	#define PROFILE_COUNTER( Name, Value )                PROFILE_COUNTER_UNIT( Name, Value, EProfileCounterUnit::Count )
	#define PROFILE_COUNTER_BYTES( Name, Value )          PROFILE_COUNTER_UNIT( Name, Value, EProfileCounterUnit::Bytes )
	#define PROFILE_COUNTER_MS( Name, Value )             PROFILE_COUNTER_UNIT( Name, Value, EProfileCounterUnit::Milliseconds )
	#define PROFILE_COUNTER_PERCENT( Name, Value )        PROFILE_COUNTER_UNIT( Name, Value, EProfileCounterUnit::Percent )
#else
	#define PROFILE_SCOPE( Name )                         ( (void)0 )
	#define PROFILE_SCOPE_CAT( Category, Name )           ( (void)0 )
	#define PROFILE_SCOPE_OWNER( Category, Owner, Name )  ( (void)0 )
	#define PROFILE_FUNCTION()                            ( (void)0 )
	#define PROFILE_SCOPE_LAZY( Var, Expression )         ( (void)0 )
	#define PROFILE_COUNTER_UNIT( Name, Value, Unit )     ( (void)0 )
	#define PROFILE_COUNTER( Name, Value )                ( (void)0 )
	#define PROFILE_COUNTER_BYTES( Name, Value )          ( (void)0 )
	#define PROFILE_COUNTER_MS( Name, Value )             ( (void)0 )
	#define PROFILE_COUNTER_PERCENT( Name, Value )        ( (void)0 )
#endif
