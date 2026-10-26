#include "ProfilerAnalyzer.h"

#include "Library/Core/Collection/Map.h"
#include "Library/Core/Time/PlatformTime.h"
#include "Library/Core/Types/Utility.h"

namespace
{
	// 集計の行を区別するキー。GroupBy に含まれない軸は 0 にしておく
	struct FStatKey
	{
		uint32 Category = 0;
		uint32 Owner = 0;
		uint32 Name = 0;
		uint64 Instance = 0;

		FORCEINLINE bool operator==( const FStatKey& Other ) const
		{
			return Category == Other.Category && Owner == Other.Owner && Name == Other.Name && Instance == Other.Instance;
		}

		friend uint32 GetTypeHash( const FStatKey& Key )
		{
			uint32 Hash = HashCombine( GetTypeHash( Key.Category ), GetTypeHash( Key.Owner ) );
			Hash = HashCombine( Hash, GetTypeHash( Key.Name ) );
			return HashCombine( Hash, GetTypeHash( Key.Instance ) );
		}
	};

	FORCEINLINE FStatKey MakeKey( const FProfileSample& Sample, uint32 GroupBy )
	{
		FStatKey Key;
		if ( GroupBy & NProfileGroupBy::Category ) Key.Category = Sample.Category;
		if ( GroupBy & NProfileGroupBy::Owner ) Key.Owner = Sample.Owner;
		if ( GroupBy & NProfileGroupBy::Name ) Key.Name = Sample.Name;
		if ( GroupBy & NProfileGroupBy::Instance ) Key.Instance = Sample.Instance;
		return Key;
	}

	FString MakeRowLabel( const IProfiler& Profiler, const FProfileSample& Sample, uint32 GroupBy )
	{
		TArray<FString> Parts;
		if ( GroupBy & NProfileGroupBy::Category ) Parts.Add( Profiler.GetString( Sample.Category ) );
		if ( GroupBy & NProfileGroupBy::Owner ) Parts.Add( Profiler.GetString( Sample.Owner ) );

		FString NamePart;
		if ( GroupBy & NProfileGroupBy::Name ) NamePart = Profiler.GetString( Sample.Name );
		if ( GroupBy & NProfileGroupBy::Instance )
		{
			if ( !NamePart.IsEmpty() ) NamePart += ' ';
			NamePart += NProfilerAnalyzer::MakeInstanceLabel( Profiler, Sample.Instance, Sample.Label );
		}
		if ( !NamePart.IsEmpty() ) Parts.Add( NamePart );

		// 空の項目（Owner が無いサンプルなど）は表示しない
		Parts.RemoveAll( []( const FString& Part ) { return Part.IsEmpty(); } );
		return Parts.IsEmpty() ? FString( "(all)" ) : FString::Join( Parts, " | " );
	}

	FORCEINLINE uint64 CombineHash64( uint64 Hash, uint64 Value )
	{
		// 64 bit の混ぜ合わせ（splitmix64 の最終段）
		Hash ^= Value + 0x9E3779B97F4A7C15ull + ( Hash << 6 ) + ( Hash >> 2 );
		Hash ^= Hash >> 30; Hash *= 0xBF58476D1CE4E5B9ull;
		Hash ^= Hash >> 27; Hash *= 0x94D049BB133111EBull;
		Hash ^= Hash >> 31;
		return Hash;
	}
}

#pragma region Range

bool NProfilerAnalyzer::ClampFrameRange( const IProfiler& Profiler, int32& InOutFirst, int32& InOutLast )
{
	const int32 NumFrames = Profiler.GetNumFrames();
	if ( NumFrames <= 0 ) return false;

	if ( InOutLast < 0 || InOutLast >= NumFrames ) InOutLast = NumFrames - 1;
	if ( InOutFirst < 0 ) InOutFirst = 0;
	if ( InOutFirst > InOutLast ) InOutFirst = InOutLast;
	return true;
}

FProfileFrameStats NProfilerAnalyzer::ComputeFrameStats( const IProfiler& Profiler, int32 FirstFrame, int32 LastFrame )
{
	FProfileFrameStats Stats;
	if ( !ClampFrameRange( Profiler, FirstFrame, LastFrame ) ) return Stats;

	Stats.MinMs = 1.0e30;
	for ( int32 i = FirstFrame; i <= LastFrame; ++i )
	{
		const FProfilerFrame& Frame = Profiler.GetFrame( i );
		const float64 Ms = NPlatformTime::TicksToMs( Frame.GetDurationTick() );
		Stats.TotalMs += Ms;
		if ( Ms < Stats.MinMs ) Stats.MinMs = Ms;
		if ( Ms > Stats.MaxMs ) Stats.MaxMs = Ms;
		Stats.NumSamples += Frame.Samples.Num();
		++Stats.NumFrames;
	}
	Stats.AverageMs = Stats.TotalMs / static_cast<float64>( Stats.NumFrames );
	return Stats;
}

#pragma endregion

#pragma region Stats

void NProfilerAnalyzer::BuildStats( const IProfiler& Profiler, const FProfileQuery& Query, TArray<FProfileStatRow>& OutRows )
{
	OutRows.Reset();

	int32 FirstFrame = Query.FirstFrame;
	int32 LastFrame = Query.LastFrame;
	if ( !ClampFrameRange( Profiler, FirstFrame, LastFrame ) ) return;

	// キー -> 行のインデックス。フィルターで除外された行は INDEX_NONE を入れて、次からは判定を省く
	TMap<FStatKey, int32> RowIndices;

	for ( int32 FrameIndex = FirstFrame; FrameIndex <= LastFrame; ++FrameIndex )
	{
		const FProfilerFrame& Frame = Profiler.GetFrame( FrameIndex );
		const TArray<FProfileSample>& Samples = Frame.Samples;

		for ( int32 i = 0; i < Samples.Num(); ++i )
		{
			const FProfileSample& Sample = Samples[i];
			if ( Query.MaxDepth >= 0 && Sample.Depth > Query.MaxDepth ) continue;

			const FStatKey Key = MakeKey( Sample, Query.GroupBy );

			int32 RowIndex;
			if ( const int32* Found = RowIndices.Find( Key ) )
			{
				RowIndex = *Found;
			}
			else
			{
				FString Label = MakeRowLabel( Profiler, Sample, Query.GroupBy );
				if ( !Query.Filter.IsEmpty() && !Label.Contains( Query.Filter, ESearchCase::IgnoreCase ) )
				{
					RowIndex = INDEX_NONE;
				}
				else
				{
					FProfileStatRow& Row = OutRows.EmplaceGetRef();
					Row.Label = MoveTemp( Label );
					Row.Category = Key.Category;
					Row.Owner = Key.Owner;
					Row.Name = Key.Name;
					Row.Instance = Key.Instance;
					RowIndex = OutRows.Num() - 1;
				}
				RowIndices.Add( Key, RowIndex );
			}
			if ( RowIndex == INDEX_NONE ) continue;

			FProfileStatRow& Row = OutRows[RowIndex];
			const float64 InclusiveMs = NPlatformTime::TicksToMs( Sample.DurationTick );
			++Row.Calls;
			Row.SelfMs += NPlatformTime::TicksToMs( Sample.GetSelfTick() );
			if ( InclusiveMs > Row.MaxMs ) Row.MaxMs = InclusiveMs;

			// 祖先に同じ行があれば、その内側の時間は外側に含まれているので足さない
			bool bNestedInSameRow = false;
			for ( int32 Parent = Sample.Parent; Parent != INDEX_NONE; Parent = Samples[Parent].Parent )
			{
				if ( MakeKey( Samples[Parent], Query.GroupBy ) == Key )
				{
					bNestedInSameRow = true;
					break;
				}
			}
			if ( !bNestedInSameRow ) Row.InclusiveMs += InclusiveMs;
		}
	}
}

#pragma endregion

#pragma region Labels

FString NProfilerAnalyzer::MakeInstanceLabel( const IProfiler& Profiler, uint64 Instance, uint32 Label )
{
	if ( Label != 0 )
	{
		const FString& Text = Profiler.GetString( Label );
		if ( !Text.IsEmpty() ) return Instance != 0 ? FString::Printf( "%s (#%llu)", *Text, static_cast<unsigned long long>( Instance ) ) : Text;
	}
	return Instance != 0 ? FString::Printf( "#%llu", static_cast<unsigned long long>( Instance ) ) : FString();
}

FString NProfilerAnalyzer::MakeNodeLabel( const IProfiler& Profiler, const FProfileTreeNode& Node )
{
	FString Result = Profiler.GetString( Node.Name );
	if ( Node.Instance != 0 || Node.Label != 0 )
	{
		const FString Instance = MakeInstanceLabel( Profiler, Node.Instance, Node.Label );
		if ( !Instance.IsEmpty() ) Result = Result.IsEmpty() ? Instance : Result + ' ' + Instance;
	}
	return Result.IsEmpty() ? FString( "(unnamed)" ) : Result;
}

#pragma endregion

#pragma region CallTree

namespace
{
	// ツリー構築中の作業用ノード（兄弟は FirstChild / NextSibling で連結する）
	struct FWorkNode
	{
		FProfileTreeNode Node;
		int32 FirstChild = INDEX_NONE;
		int32 LastChild = INDEX_NONE;
		int32 NextSibling = INDEX_NONE;
	};

	void EmitNode( const TArray<FWorkNode>& Work, int32 Index, int32 Depth, TArray<FProfileTreeNode>& Out )
	{
		const int32 OutIndex = Out.Num();
		FProfileTreeNode& Emitted = Out.EmplaceGetRef( Work[Index].Node );
		Emitted.Depth = Depth;

		for ( int32 Child = Work[Index].FirstChild; Child != INDEX_NONE; Child = Work[Child].NextSibling ) EmitNode( Work, Child, Depth + 1, Out );

		Out[OutIndex].NumDescendants = Out.Num() - OutIndex - 1;
	}
}

void NProfilerAnalyzer::BuildCallTree( const IProfiler& Profiler, int32 FirstFrame, int32 LastFrame, bool bMergeSiblings, bool bMergeInstances,
									   TArray<FProfileTreeNode>& OutNodes )
{
	OutNodes.Reset();
	if ( !ClampFrameRange( Profiler, FirstFrame, LastFrame ) ) return;

	// 複数フレームは、フレームをまたいで同じ処理を同じノードにするために、まとめる前提にする
	const bool bMerge = bMergeSiblings || FirstFrame != LastFrame;

	// 仮想のルート（index 0）の下にフレーム全体を載せる
	TArray<FWorkNode> Work;
	Work.Emplace();

	TMap<uint64, int32> MergedNodes;    // まとめるとき: キー -> ノード
	TMap<uint64, int32> KeyCounts;      // まとめないとき: 同じ親・同じキーの何番目か（ID を区別するため）
	TArray<int32> NodeOfSample;

	for ( int32 FrameIndex = FirstFrame; FrameIndex <= LastFrame; ++FrameIndex )
	{
		const TArray<FProfileSample>& Samples = Profiler.GetFrame( FrameIndex ).Samples;
		NodeOfSample.SetNum( Samples.Num() );
		KeyCounts.Reset();

		for ( int32 i = 0; i < Samples.Num(); ++i )
		{
			const FProfileSample& Sample = Samples[i];
			const int32 ParentNode = Sample.Parent == INDEX_NONE ? 0 : NodeOfSample[Sample.Parent];

			// 親の下での、この処理を表すキー
			uint64 Key = CombineHash64( 0x51ED27ull, Sample.Category );
			Key = CombineHash64( Key, Sample.Owner );
			Key = CombineHash64( Key, Sample.Name );
			if ( !( bMerge && bMergeInstances ) ) Key = CombineHash64( Key, Sample.Instance );

			const uint64 PathKey = CombineHash64( Work[ParentNode].Node.Id, Key );

			int32 NodeIndex = INDEX_NONE;
			uint64 NodeId = PathKey;
			if ( bMerge )
			{
				if ( const int32* Found = MergedNodes.Find( PathKey ) ) NodeIndex = *Found;
			}
			else
			{
				// まとめない: 同じ親の下に同じキーが複数あれば、何番目かで ID を区別する
				int32& Ordinal = KeyCounts.FindOrAdd( PathKey );
				NodeId = CombineHash64( PathKey, static_cast<uint64>( Ordinal ) );
				++Ordinal;
			}

			if ( NodeIndex == INDEX_NONE )
			{
				NodeIndex = Work.Num();
				FWorkNode& Created = Work.EmplaceGetRef();
				Created.Node.Id = NodeId;
				Created.Node.Category = Sample.Category;
				Created.Node.Owner = Sample.Owner;
				Created.Node.Name = Sample.Name;
				Created.Node.Label = ( bMerge && bMergeInstances ) ? 0u : Sample.Label;
				Created.Node.Instance = ( bMerge && bMergeInstances ) ? 0 : Sample.Instance;

				// 親の子リストの末尾に追加
				FWorkNode& Parent = Work[ParentNode];
				if ( Parent.LastChild == INDEX_NONE ) Parent.FirstChild = NodeIndex;
				else Work[Parent.LastChild].NextSibling = NodeIndex;
				Parent.LastChild = NodeIndex;

				if ( bMerge ) MergedNodes.Add( PathKey, NodeIndex );
			}
			NodeOfSample[i] = NodeIndex;

			FProfileTreeNode& Node = Work[NodeIndex].Node;
			++Node.Calls;
			Node.TotalTick += Sample.DurationTick;
			Node.SelfTick += Sample.GetSelfTick();
			if ( Sample.DurationTick > Node.MaxTick ) Node.MaxTick = Sample.DurationTick;
		}
	}

	// 仮想ルートの子を最上位として、呼ばれた順に並べた配列へ出力する
	OutNodes.Reserve( Work.Num() );
	for ( int32 Root = Work[0].FirstChild; Root != INDEX_NONE; Root = Work[Root].NextSibling ) EmitNode( Work, Root, 0, OutNodes );
}

namespace
{
	// [Begin, End) にある兄弟（各ノードの部分木を飛ばして数える）を並べ替えて、部分木ごと Out へ出力する
	void EmitSorted( const IProfiler& Profiler, const TArray<FProfileTreeNode>& In, int32 Begin, int32 End, NProfilerAnalyzer::ETreeSortKey Key,
					 bool bAscending, TArray<FProfileTreeNode>& Out )
	{
		TArray<int32> Siblings;
		for ( int32 i = Begin; i < End; i += 1 + In[i].NumDescendants ) Siblings.Add( i );

		// true なら A が B より前
		const auto Before = [&Profiler, &In, Key, bAscending]( int32 A, int32 B )
		{
			const FProfileTreeNode& NodeA = In[A];
			const FProfileTreeNode& NodeB = In[B];
			switch ( Key )
			{
			case NProfilerAnalyzer::ETreeSortKey::Name:
			case NProfilerAnalyzer::ETreeSortKey::Category:
			case NProfilerAnalyzer::ETreeSortKey::Owner:
			{
				const uint32 IdA = Key == NProfilerAnalyzer::ETreeSortKey::Name ? NodeA.Name : ( Key == NProfilerAnalyzer::ETreeSortKey::Category ? NodeA.Category : NodeA.Owner );
				const uint32 IdB = Key == NProfilerAnalyzer::ETreeSortKey::Name ? NodeB.Name : ( Key == NProfilerAnalyzer::ETreeSortKey::Category ? NodeB.Category : NodeB.Owner );
				int32 Compare = Profiler.GetString( IdA ).Compare( Profiler.GetString( IdB ), ESearchCase::IgnoreCase );
				// 名前が同じなら個体（Instance）の順
				if ( Compare == 0 && Key == NProfilerAnalyzer::ETreeSortKey::Name ) Compare = NodeA.Instance < NodeB.Instance ? -1 : ( NodeA.Instance > NodeB.Instance ? 1 : 0 );
				return bAscending ? Compare < 0 : Compare > 0;
			}
			case NProfilerAnalyzer::ETreeSortKey::Calls:
				return bAscending ? NodeA.Calls < NodeB.Calls : NodeA.Calls > NodeB.Calls;
			case NProfilerAnalyzer::ETreeSortKey::Self:
				return bAscending ? NodeA.SelfTick < NodeB.SelfTick : NodeA.SelfTick > NodeB.SelfTick;
			case NProfilerAnalyzer::ETreeSortKey::Total:
			default:
				return bAscending ? NodeA.TotalTick < NodeB.TotalTick : NodeA.TotalTick > NodeB.TotalTick;
			}
		};
		Siblings.StableSort( Before );

		for ( int32 Sibling : Siblings )
		{
			Out.Add( In[Sibling] );
			EmitSorted( Profiler, In, Sibling + 1, Sibling + 1 + In[Sibling].NumDescendants, Key, bAscending, Out );
		}
	}
}

void NProfilerAnalyzer::SortCallTree( const IProfiler& Profiler, const TArray<FProfileTreeNode>& InNodes, ETreeSortKey Key, bool bAscending,
									  TArray<FProfileTreeNode>& OutNodes )
{
	OutNodes.Reset();
	OutNodes.Reserve( InNodes.Num() );
	// 兄弟の並びだけが変わり、深さや部分木の大きさ（NumDescendants）は変わらないので、そのままコピーできる
	EmitSorted( Profiler, InNodes, 0, InNodes.Num(), Key, bAscending, OutNodes );
}

#pragma endregion

//=============================================================================
// カウンター
//=============================================================================
void NProfilerAnalyzer::BuildCounterRows( const IProfiler& Profiler, int32 FirstFrame, int32 LastFrame, const FString& Filter, TArray<FProfileCounterRow>& OutRows )
{
	OutRows.Reset();
	if ( !ClampFrameRange( Profiler, FirstFrame, LastFrame ) ) return;

	TMap<uint32, int32> RowOfName;
	for ( int32 FrameIndex = FirstFrame; FrameIndex <= LastFrame; ++FrameIndex )
	{
		for ( const FProfileCounter& Counter : Profiler.GetFrame( FrameIndex ).Counters )
		{
			int32* Found = RowOfName.Find( Counter.Name );
			if ( Found == nullptr )
			{
				FProfileCounterRow Row;
				Row.Name = Counter.Name;
				Row.Label = Profiler.GetString( Counter.Name );
				Row.Unit = Counter.Unit;
				Row.Min = Counter.Value;
				Row.Max = Counter.Value;
				RowOfName.Add( Counter.Name, OutRows.Add( Row ) );
				Found = RowOfName.Find( Counter.Name );
			}
			FProfileCounterRow& Row = OutRows[*Found];
			Row.Unit = Counter.Unit;
			Row.Last = Counter.Value;
			Row.Min = Counter.Value < Row.Min ? Counter.Value : Row.Min;
			Row.Max = Counter.Value > Row.Max ? Counter.Value : Row.Max;
			Row.Average += Counter.Value;   // あとで個数で割る
			++Row.NumSamples;
		}
	}

	for ( FProfileCounterRow& Row : OutRows )
	{
		if ( Row.NumSamples > 0 ) Row.Average /= static_cast<float64>( Row.NumSamples );
	}
	if ( !Filter.IsEmpty() ) OutRows.RemoveAll( [&Filter]( const FProfileCounterRow& Row ) { return !Row.Label.Contains( Filter, ESearchCase::IgnoreCase ); } );
	OutRows.Sort( []( const FProfileCounterRow& A, const FProfileCounterRow& B ) { return A.Label.Compare( B.Label, ESearchCase::IgnoreCase ) < 0; } );
}

void NProfilerAnalyzer::GetCounterSeries( const IProfiler& Profiler, uint32 Name, int32 FirstFrame, int32 LastFrame, TArray<float32>& OutValues )
{
	OutValues.Reset();
	if ( !ClampFrameRange( Profiler, FirstFrame, LastFrame ) ) return;

	float32 Previous = 0.0f;
	for ( int32 FrameIndex = FirstFrame; FrameIndex <= LastFrame; ++FrameIndex )
	{
		for ( const FProfileCounter& Counter : Profiler.GetFrame( FrameIndex ).Counters )
		{
			if ( Counter.Name == Name )
			{
				Previous = static_cast<float32>( Counter.Value );
				break;
			}
		}
		OutValues.Add( Previous );
	}
}

FString NProfilerAnalyzer::FormatCounterValue( float64 Value, EProfileCounterUnit Unit )
{
	switch ( Unit )
	{
	case EProfileCounterUnit::Bytes:
	{
		const float64 Magnitude = Value < 0.0 ? -Value : Value;
		if ( Magnitude >= 1024.0 * 1024.0 * 1024.0 ) return FString::Printf( "%.2f GB", Value / ( 1024.0 * 1024.0 * 1024.0 ) );
		if ( Magnitude >= 1024.0 * 1024.0 ) return FString::Printf( "%.2f MB", Value / ( 1024.0 * 1024.0 ) );
		if ( Magnitude >= 1024.0 ) return FString::Printf( "%.1f KB", Value / 1024.0 );
		return FString::Printf( "%.0f B", Value );
	}
	case EProfileCounterUnit::Milliseconds: return FString::Printf( "%.3f ms", Value );
	case EProfileCounterUnit::Percent: return FString::Printf( "%.1f %%", Value );
	case EProfileCounterUnit::Count:
	default:
	{
		// 整数なら 3 桁ごとの区切り
		if ( Value == static_cast<float64>( static_cast<int64>( Value ) ) )
		{
			FString Digits = FString::FromInt( static_cast<int64>( Value < 0.0 ? -Value : Value ) );
			FString Grouped;
			for ( int32 i = 0; i < Digits.Len(); ++i )
			{
				if ( i > 0 && ( Digits.Len() - i ) % 3 == 0 ) Grouped += ',';
				Grouped += Digits[i];
			}
			return ( Value < 0.0 ? FString( "-" ) : FString() ) + Grouped;
		}
		return FString::Printf( "%.2f", Value );
	}
	}
}
