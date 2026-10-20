#include "ConsoleHistory.h"

FConsoleHistory::FConsoleHistory( int32 InMaxEntries )
	: MaxEntries( InMaxEntries > 1 ? InMaxEntries : 1 )
{
}

void FConsoleHistory::Add( const FString& Line )
{
	ResetNavigation();
	const FString Trimmed = Line.TrimStartAndEnd();
	if ( Trimmed.IsEmpty() ) return;

	// 同じ行が過去にあれば、そちらを消して、最後（一番新しい）に足す
	for ( int32 i = Lines.Num() - 1; i >= 0; --i )
	{
		if ( Lines[i] == Trimmed ) Lines.RemoveAt( i );
	}
	Lines.Add( Trimmed );
	while ( Lines.Num() > MaxEntries ) Lines.RemoveAt( 0 );
}

void FConsoleHistory::Clear()
{
	Lines.Empty();
	ResetNavigation();
}

void FConsoleHistory::SetAll( const TArray<FString>& InLines )
{
	Lines.Empty();
	for ( const FString& Line : InLines ) Add( Line );
	ResetNavigation();
}

void FConsoleHistory::ResetNavigation()
{
	Cursor = INDEX_NONE;
	Saved.Empty();
}

FString FConsoleHistory::Previous( const FString& CurrentInput )
{
	if ( Lines.IsEmpty() ) return CurrentInput;
	if ( Cursor == INDEX_NONE )
	{
		Saved = CurrentInput;
		Cursor = Lines.Num();
	}
	if ( Cursor > 0 ) --Cursor;
	return Lines[Cursor];
}

FString FConsoleHistory::Next( const FString& CurrentInput )
{
	if ( Cursor == INDEX_NONE ) return CurrentInput;
	++Cursor;
	if ( Cursor >= Lines.Num() )
	{
		const FString Restored = Saved;
		ResetNavigation();
		return Restored;
	}
	return Lines[Cursor];
}

TArray<FString> FConsoleHistory::FindMatches( const FString& Prefix, int32 MaxCount ) const
{
	TArray<FString> Matches;
	if ( Prefix.IsEmpty() ) return Matches;
	for ( int32 i = Lines.Num() - 1; i >= 0 && Matches.Num() < MaxCount; --i )
	{
		if ( Lines[i].StartsWith( Prefix, ESearchCase::IgnoreCase ) && !Lines[i].Equals( Prefix, ESearchCase::IgnoreCase ) ) Matches.Add( Lines[i] );
	}
	return Matches;
}
