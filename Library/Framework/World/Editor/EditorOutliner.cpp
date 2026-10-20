#include "EditorOutliner.h"

#include "Library/Core/Collection/Map.h"

namespace
{
	// 最大の深さ（循環などで、親をたどり続けないための上限）
	constexpr int32 MaxDepth = 1024;

	bool ContainsNoCase( const FString& Text, const FString& Part )
	{
		return Text.Contains( Part, ESearchCase::IgnoreCase );
	}

	TArray<FString> SplitTerms( const FString& Filter )
	{
		TArray<FString> Terms;
		FString Current;
		for ( int32 i = 0; i < Filter.Len(); ++i )
		{
			const char C = Filter[i];
			if ( C == ' ' || C == '\t' )
			{
				if ( !Current.IsEmpty() ) Terms.Add( Current );
				Current = FString();
			}
			else Current += C;
		}
		if ( !Current.IsEmpty() ) Terms.Add( Current );
		return Terms;
	}
}

bool NWorldEditor::MatchesFilterTerm( const FWorldObject& Object, const FString& Term )
{
	if ( Term.StartsWith( "tag:" ) )
	{
		const FString Tag = Term.Mid( 4 );
		return !Tag.IsEmpty() && Object.HasTag( Tag );
	}
	if ( Term.StartsWith( "comp:" ) )
	{
		const FString Component = Term.Mid( 5 );
		for ( const FString& Name : Object.GetComponentNames() )
		{
			if ( Name.Equals( Component, ESearchCase::IgnoreCase ) ) return true;
		}
		return false;
	}
	if ( Term.StartsWith( "#" ) )
	{
		const FString Digits = Term.Mid( 1 );
		return !Digits.IsEmpty() && Digits == FString::Printf( "%d", Object.Id );
	}
	return ContainsNoCase( Object.Name, Term );
}

bool NWorldEditor::MatchesFilter( const FWorldObject& Object, const FString& Filter )
{
	for ( const FString& Term : SplitTerms( Filter ) )
	{
		if ( !MatchesFilterTerm( Object, Term ) ) return false;
	}
	return true;
}

TArray<FEditorOutlinerRow> NWorldEditor::BuildOutlinerRows( const FWorldDefinition& World, const TSet<int32>& Collapsed, const FString& Filter )
{
	const int32 Num = World.Objects.Num();
	TMap<int32, int32> IndexById;
	for ( int32 i = 0; i < Num; ++i )
	{
		if ( !IndexById.Contains( World.Objects[i].Id ) ) IndexById.Add( World.Objects[i].Id, i );
	}

	// 子の一覧（配列の順）と、最上位（親がいない・存在しない親）
	TArray<TArray<int32>> Children;
	Children.SetNum( Num );
	TArray<int32> Roots;
	for ( int32 i = 0; i < Num; ++i )
	{
		const int32 Parent = World.Objects[i].Parent;
		const int32* ParentIndex = Parent == WorldNoId ? nullptr : IndexById.Find( Parent );
		if ( ParentIndex == nullptr ) Roots.Add( i );
		else Children[*ParentIndex].Add( i );
	}

	// 絞り込み: 一致したものと、その祖先
	const bool bFiltering = !SplitTerms( Filter ).IsEmpty();
	TArray<bool> Match;
	TArray<bool> Show;
	Match.Init( true, Num );
	Show.Init( true, Num );
	if ( bFiltering )
	{
		Show.Init( false, Num );
		for ( int32 i = 0; i < Num; ++i )
		{
			Match[i] = MatchesFilter( World.Objects[i], Filter );
			if ( !Match[i] ) continue;
			int32 Current = i;
			for ( int32 Step = 0; Step < MaxDepth && Current != INDEX_NONE && !Show[Current]; ++Step )
			{
				Show[Current] = true;
				const int32 Parent = World.Objects[Current].Parent;
				const int32* ParentIndex = Parent == WorldNoId ? nullptr : IndexById.Find( Parent );
				Current = ParentIndex != nullptr ? *ParentIndex : INDEX_NONE;
			}
		}
	}

	TArray<FEditorOutlinerRow> Rows;
	TArray<bool> Visited;
	Visited.Init( false, Num );

	struct FEntry { int32 Index; int32 Depth; };
	const auto Walk = [&]( int32 RootIndex )
	{
		TArray<FEntry> Stack;
		Stack.Add( { RootIndex, 0 } );
		while ( !Stack.IsEmpty() )
		{
			const FEntry Entry = Stack.Pop();
			if ( Visited[Entry.Index] || !Show[Entry.Index] ) continue;
			Visited[Entry.Index] = true;

			const FWorldObject& Object = World.Objects[Entry.Index];
			FEditorOutlinerRow Row;
			Row.ObjectId = Object.Id;
			Row.Depth = Entry.Depth;
			Row.bHasChildren = !Children[Entry.Index].IsEmpty();
			Row.bMatch = Match[Entry.Index];
			Row.bExpanded = Row.bHasChildren && ( bFiltering || !Collapsed.Contains( Object.Id ) );
			Rows.Add( Row );

			if ( !Row.bExpanded ) continue;
			for ( int32 i = Children[Entry.Index].Num() - 1; i >= 0; --i ) Stack.Add( { Children[Entry.Index][i], Entry.Depth + 1 } );
		}
	};

	for ( const int32 Root : Roots ) Walk( Root );

	// 循環していて、木からたどれないもの（畳まれた木の中は、まだ「たどっていない」だけなので除く）
	TArray<bool> Reachable;
	Reachable.Init( false, Num );
	{
		TArray<int32> Stack = Roots;
		while ( !Stack.IsEmpty() )
		{
			const int32 Index = Stack.Pop();
			if ( Reachable[Index] ) continue;
			Reachable[Index] = true;
			for ( const int32 Child : Children[Index] ) Stack.Add( Child );
		}
	}
	for ( int32 i = 0; i < Num; ++i )
	{
		if ( Reachable[i] || Visited[i] || !Show[i] ) continue;
		Visited[i] = true;
		FEditorOutlinerRow Row;
		Row.ObjectId = World.Objects[i].Id;
		Row.Depth = 0;
		Row.bMatch = Match[i];
		Rows.Add( Row );
	}
	return Rows;
}
