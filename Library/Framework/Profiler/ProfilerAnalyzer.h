#pragma once
#include "Library/Core/Collection/Array.h"
#include "Library/Core/Collection/String.h"
#include "Library/Core/Interfaces/Profiler/IProfiler.h"

//=============================================================================
// ProfilerAnalyzer
//   記録したサンプルの集計。画面表示に依存しない（IProfiler のデータだけを読む）ので、
//   テストやログ出力、別の表示（ImGui 以外）からも使える。
//
//   // 直近 60 フレームを、シーン（Owner）ごとの合計時間にまとめる
//   FProfileQuery Query;
//   Query.FirstFrame = Profiler.GetNumFrames() - 60;
//   Query.GroupBy    = NProfileGroupBy::Owner;
//   TArray<FProfileStatRow> Rows;
//   NProfilerAnalyzer::BuildStats( Profiler, Query, Rows );
//=============================================================================

//-----------------------------------------------------------------------------
// 集計の軸（組み合わせられる）。
//   Category | Name       -> 分類ごと・名前ごと
//   Owner                 -> シーンやサブシステムの持ち主ごと
//   Name | Instance       -> ゲームオブジェクト 1 つずつ
//-----------------------------------------------------------------------------
namespace NProfileGroupBy
{
	constexpr uint32 None     = 0;
	constexpr uint32 Category = 1u << 0;
	constexpr uint32 Owner    = 1u << 1;
	constexpr uint32 Name     = 1u << 2;
	constexpr uint32 Instance = 1u << 3;
}

struct FProfileQuery
{
	// 対象のフレーム範囲（IProfiler::GetFrame のインデックス。両端を含む）。LastFrame < 0 なら最後まで
	int32 FirstFrame = 0;
	int32 LastFrame = -1;

	uint32 GroupBy = NProfileGroupBy::Name;

	// 空でなければ、この文字列を含む行だけ残す（大文字小文字は区別しない）
	FString Filter;

	// これより深いサンプルは無視する（0 で最上位だけ、-1 で制限なし）
	int32 MaxDepth = -1;
};

struct FProfileStatRow
{
	FString Label;

	uint32 Category = 0;
	uint32 Owner = 0;
	uint32 Name = 0;
	uint64 Instance = 0;

	int32 Calls = 0;
	float64 InclusiveMs = 0.0;   // 子を含む合計（入れ子で同じ行が重なる場合は外側だけ数える）
	float64 SelfMs = 0.0;        // 子を除いた合計（行をまたいで足しても二重にならない）
	float64 MaxMs = 0.0;         // 1 回あたりの最大（子を含む）
};

// カウンター 1 つ分の集計（範囲の中で、そのカウンターが記録されたフレームについて）
struct FProfileCounterRow
{
	uint32 Name = 0;
	FString Label;
	EProfileCounterUnit Unit = EProfileCounterUnit::Count;
	int32 NumSamples = 0;   // 記録されたフレームの数
	float64 Last = 0.0;     // 範囲の中の最後の値
	float64 Min = 0.0;
	float64 Max = 0.0;
	float64 Average = 0.0;
};

struct FProfileFrameStats
{
	int32 NumFrames = 0;
	int64 NumSamples = 0;
	float64 TotalMs = 0.0;
	float64 AverageMs = 0.0;
	float64 MinMs = 0.0;
	float64 MaxMs = 0.0;
};

//-----------------------------------------------------------------------------
// 1 フレームの呼び出しツリー
//-----------------------------------------------------------------------------
struct FProfileTreeNode
{
	// 親からの道筋で決まる ID。フレームが変わっても同じ処理は同じ ID になる（表示の開閉状態を覚えておくのに使う）
	uint64 Id = 0;

	uint32 Category = 0;
	uint32 Owner = 0;
	uint32 Name = 0;
	uint32 Label = 0;
	uint64 Instance = 0;

	int32 Depth = 0;
	int32 Calls = 0;
	int64 TotalTick = 0;
	int64 SelfTick = 0;
	int64 MaxTick = 0;

	// 配列上で、このノードの直後から続く子孫の数（閉じたノードを飛ばすのに使う）
	int32 NumDescendants = 0;
};

namespace NProfilerAnalyzer
{
	// 範囲を [0, GetNumFrames() - 1] に収めて両端を返す。フレームが 1 つもなければ false
	bool ClampFrameRange( const IProfiler& Profiler, int32& InOutFirst, int32& InOutLast );

	FProfileFrameStats ComputeFrameStats( const IProfiler& Profiler, int32 FirstFrame, int32 LastFrame );

	// カウンターの集計（名前の昇順）。Filter が空でなければ、含む名前だけ（大文字小文字は区別しない）
	void BuildCounterRows( const IProfiler& Profiler, int32 FirstFrame, int32 LastFrame, const FString& Filter, TArray<FProfileCounterRow>& OutRows );
	// 名前が Name のカウンターの値を、FirstFrame..LastFrame の各フレームについて返す（記録が無いフレームは直前の値、最初から無ければ 0）
	void GetCounterSeries( const IProfiler& Profiler, uint32 Name, int32 FirstFrame, int32 LastFrame, TArray<float32>& OutValues );
	// カウンターの値を読みやすい文字列にする: Bytes は "12.3 MB" / "512 KB"、Count は桁区切り、Milliseconds は "3.25 ms"、Percent は "75.0 %"
	FString FormatCounterValue( float64 Value, EProfileCounterUnit Unit );

	// 集計。結果は未整列（並べ替えは呼び出し側で行う）
	void BuildStats( const IProfiler& Profiler, const FProfileQuery& Query, TArray<FProfileStatRow>& OutRows );

	// FirstFrame..LastFrame（IProfiler::GetFrame のインデックス。LastFrame < 0 で最後まで）を呼び出しツリーにする。
	// 表示順に並べた配列で、Depth と NumDescendants で階層を表す。Calls / TotalTick / SelfTick は範囲の合計
	// （1 フレームあたりにするには、呼び出し側でフレーム数で割る）。
	//   bMergeSiblings : 同じ親の下の同じ処理（Category / Owner / Name が同じ）を 1 つにまとめる。時間の大きい順に並ぶ。
	//                    複数フレームを渡したときは、フレームをまたいで揃えるために常にまとめる
	//   bMergeInstances: まとめるときに Instance の違いを無視する（false なら個体ごとに別ノード）
	//   並びは最初に呼ばれた順（時間が近い行が入れ替わり続けないので安定）。並べ替えたいときは SortCallTree を使う
	void BuildCallTree( const IProfiler& Profiler, int32 FirstFrame, int32 LastFrame, bool bMergeSiblings, bool bMergeInstances,
						TArray<FProfileTreeNode>& OutNodes );

	// 呼び出しツリーの並べ替えの基準
	enum class ETreeSortKey : uint8
	{
		Name,
		Category,
		Owner,
		Calls,
		Total,
		Self,
	};

	// 親子関係は保ったまま、同じ親の下の兄弟どうしを並べ替える（InNodes は BuildCallTree の結果）。
	// 値が同じ行は元の順のままなので、並びが入れ替わり続けない
	void SortCallTree( const IProfiler& Profiler, const TArray<FProfileTreeNode>& InNodes, ETreeSortKey Key, bool bAscending,
					   TArray<FProfileTreeNode>& OutNodes );

	// 表示用のラベル。Instance は Label があればその名前、無ければ "#ID"
	FString MakeInstanceLabel( const IProfiler& Profiler, uint64 Instance, uint32 Label );
	FString MakeNodeLabel( const IProfiler& Profiler, const FProfileTreeNode& Node );
}
