#include "ConsoleTypes.h"

FConsoleBuffer::FConsoleBuffer( int32 InMaxLines )
	: MaxLines( InMaxLines > 1 ? InMaxLines : 1 )
{
}

void FConsoleBuffer::Write( EConsoleLine Kind, const FString& Text )
{
	// 改行ごとの行にする（"\r\n" の '\r' は捨てる。空の文字列は空行 1 つ）
	int32 Start = 0;
	for ( ;; )
	{
		const int32 End = Text.Find( "\n", ESearchCase::CaseSensitive, Start );
		FString Line = End == INDEX_NONE ? Text.Mid( Start ) : Text.Mid( Start, End - Start );
		if ( Line.EndsWith( "\r" ) ) Line = Line.LeftChop( 1 );

		FConsoleLine Entry;
		Entry.Kind = Kind;
		Entry.Text = Line;
		if ( NumLines < MaxLines )
		{
			Lines.Add( Entry );
			++NumLines;
		}
		else
		{
			Lines[Head] = Entry;
			Head = ( Head + 1 ) % MaxLines;
		}
		++Revision;

		if ( End == INDEX_NONE ) break;
		Start = End + 1;
	}
}

const FConsoleLine& FConsoleBuffer::Get( int32 Index ) const
{
	return Lines[( Head + Index ) % Lines.Num()];
}

void FConsoleBuffer::Clear()
{
	Lines.Empty();
	Head = 0;
	NumLines = 0;
	++Revision;
}

FString FConsoleBuffer::ToText() const
{
	FString Result;
	for ( int32 i = 0; i < NumLines; ++i )
	{
		if ( i > 0 ) Result += "\n";
		Result += Get( i ).Text;
	}
	return Result;
}
