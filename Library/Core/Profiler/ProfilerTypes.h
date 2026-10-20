#pragma once
#include "Library/Core/Collection/Array.h"
#include "Library/Core/Types/TypeDefinition.h"

//=============================================================================
// プロファイラーが記録するデータ
//
//   サンプル 1 つ = 1 回の計測区間（PROFILE_SCOPE 1 回分）。次の 4 つの軸で分類できる。
//     Category : 何の処理か          例) "Engine" "Subsystem" "Object.Tick" "Object.Draw" "Physics"
//     Owner    : どの持ち主の下か    例) シーン名 / サブシステムのコレクション名 ("Game" "App")
//     Name     : 処理や型の名前      例) "CPhysicsBall" "CPhysicsSubsystem" "Platform.EndFrame"
//     Instance : 同じ Name の個体    例) ゲームオブジェクトの ID（無ければ 0）
//   文字列は IProfiler::InternString で ID に変換して持つ（サンプルを小さく保つため）。
//=============================================================================
struct FProfileSample
{
	int64  StartTick    = 0;
	int64  DurationTick = 0;   // 子も含む時間
	int64  ChildTick    = 0;   // 直接の子の DurationTick の合計
	uint64 Instance     = 0;
	uint32 Category     = 0;
	uint32 Owner        = 0;
	uint32 Name         = 0;
	uint32 Label        = 0;   // Instance の表示名（無ければ 0。表示は "#Instance" になる）
	int32  Parent       = INDEX_NONE;
	uint16 Depth        = 0;

	// 子を除いた、その処理自身の時間
	FORCEINLINE int64 GetSelfTick() const { return DurationTick - ChildTick; }
};

//=============================================================================
// カウンター: 時間ではない、フレームごとの値（メモリ使用量・オブジェクトの数・キューの長さなど）。
//   PROFILE_COUNTER で、フレームに 1 回記録する（同じ名前を同じフレームで何度も記録すると、最後の値）。
//   Unit は表示のための単位（値そのものは float64 のまま）。
//=============================================================================
enum class EProfileCounterUnit : uint8
{
	Count,          // 個数・回数など
	Bytes,          // バイト数（表示は KB / MB に直す）
	Milliseconds,   // ミリ秒
	Percent,        // 0..100
};

struct FProfileCounter
{
	uint32 Name = 0;   // 文字列 ID（分類は名前の '.' 区切りで表す: "Asset.Memory.Texture"）
	EProfileCounterUnit Unit = EProfileCounterUnit::Count;
	float64 Value = 0.0;
};

//=============================================================================
// メインスレッド以外（ジョブシステムのワーカーなど）が記録したサンプル。1 スレッド分、1 フレームにつき 1 つ。
//   フレームはメインスレッドの BeginFrame で区切る。別スレッドのサンプルの木（開始から終了まで）は、
//   「木の根が始まった時刻」を含むフレームに入る（長いジョブは、始まったフレームから後ろのフレームへはみ出して描かれる）。
//   Parent / Depth は Samples の中での位置と深さ
//=============================================================================
struct FProfilerThreadTrack
{
	uint32 ThreadId = 0;   // OS のスレッド ID
	uint32 Name = 0;       // スレッド名（文字列 ID。0 なら名前なし）
	uint16 MaxDepth = 0;
	TArray<FProfileSample> Samples;
};

//=============================================================================
// 1 フレーム分のサンプル（記録された順 = 呼び出しの前順で並ぶ）
//=============================================================================
struct FProfilerFrame
{
	uint64 FrameNumber = 0;
	int64  StartTick   = 0;
	int64  EndTick     = 0;
	int32  NumDropped  = 0;   // サンプル数の上限で捨てた数
	uint16 MaxDepth    = 0;   // 一番深いサンプルの深さ（タイムラインの段の高さを決めるのに使う）
	TArray<FProfileSample> Samples;   // メインスレッドのサンプル

	// メインスレッド以外が記録したサンプル（記録したスレッドだけ入る）
	TArray<FProfilerThreadTrack> Threads;

	// このフレームで記録されたカウンター（記録した順）
	TArray<FProfileCounter> Counters;

	FORCEINLINE int64 GetDurationTick() const { return EndTick - StartTick; }
};
