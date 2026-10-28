#include "ConsoleParse.h"

namespace
{
	struct FScanToken
	{
		FString Value;
		int32 Start = 0;
		int32 End = 0;
		bool bTerminated = true;   // 引用符が閉じている（引用符のないトークンは true）
	};

	FORCEINLINE bool IsSpace( char Ch ) { return Ch == ' ' || Ch == '\t'; }

	// Start から、引用符の外の ';' の手前（または末尾）まで走査して、引数に分ける。OutEnd は、走査を止めた位置（';' の位置か、文字列の長さ）
	void Scan( const FString& Text, int32 Start, TArray<FScanToken>& OutTokens, int32& OutEnd )
	{
		const int32 Length = Text.Len();
		int32 i = Start;
		while ( i < Length )
		{
			while ( i < Length && IsSpace( Text[i] ) ) ++i;
			if ( i >= Length ) break;
			if ( Text[i] == ';' )
			{
				OutEnd = i;
				return;
			}

			FScanToken Token;
			Token.Start = i;
			while ( i < Length )
			{
				const unsigned char Ch = static_cast<unsigned char>( Text[i] );
				if ( IsSpace( static_cast<char>( Ch ) ) || Ch == ';' ) break;

				if ( Ch == '"' )
				{
					++i;
					Token.bTerminated = false;
					while ( i < Length )
					{
						const unsigned char Inner = static_cast<unsigned char>( Text[i] );
						if ( Inner == '\\' && i + 1 < Length )
						{
							const char Next = Text[i + 1];
							if ( Next == '"' ) { Token.Value += '"'; i += 2; }
							else if ( Next == '\\' ) { Token.Value += '\\'; i += 2; }
							else if ( Next == 'n' ) { Token.Value += '\n'; i += 2; }
							else if ( Next == 't' ) { Token.Value += '\t'; i += 2; }
							else { Token.Value += '\\'; ++i; }   // それ以外は、\ をそのまま残す（"C:\dir"）
						}
						else if ( Inner == '"' )
						{
							++i;
							Token.bTerminated = true;
							break;
						}
						else
						{
							Token.Value += static_cast<char>( Inner );
							++i;
						}
					}
					continue;
				}
				if ( Ch == '\'' )
				{
					++i;
					Token.bTerminated = false;
					while ( i < Length )
					{
						const unsigned char Inner = static_cast<unsigned char>( Text[i] );
						if ( Inner == '\'' )
						{
							++i;
							Token.bTerminated = true;
							break;
						}
						else
						{
							Token.Value += static_cast<char>( Inner );
							++i;
						}
					}
					continue;
				}
				Token.Value += static_cast<char>( Ch );
				++i;
			}
			Token.End = i;
			OutTokens.Add( Token );
		}
		OutEnd = Length;
	}
}

bool NConsole::Tokenize( const FString& Text, TArray<FString>& OutTokens, FString& OutError )
{
	OutTokens.Empty();
	OutError.Empty();
	TArray<FScanToken> Scanned;
	int32 End = 0;
	Scan( Text, 0, Scanned, End );
	if ( End < Text.Len() )
	{
		OutError = "unexpected ';' (use \";\" or split the commands first)";
		return false;
	}
	for ( const FScanToken& Token : Scanned )
	{
		if ( !Token.bTerminated )
		{
			OutError = "a quote is not closed";
			return false;
		}
		OutTokens.Add( Token.Value );
	}
	return true;
}

TArray<FString> NConsole::SplitCommands( const FString& Line )
{
	TArray<FString> Commands;
	const FString Trimmed = Line.TrimStartAndEnd();
	if ( Trimmed.IsEmpty() || Trimmed.StartsWith( "#" ) || Trimmed.StartsWith( "//" ) ) return Commands;

	int32 Start = 0;
	while ( Start <= Line.Len() )
	{
		TArray<FScanToken> Ignored;
		int32 End = 0;
		Scan( Line, Start, Ignored, End );
		const FString Segment = Line.Mid( Start, End - Start ).TrimStartAndEnd();
		if ( !Segment.IsEmpty() ) Commands.Add( Segment );
		if ( End >= Line.Len() ) break;
		Start = End + 1;
	}
	return Commands;
}

FString NConsole::Quote( const FString& Text )
{
	bool bNeedsQuote = Text.IsEmpty();
	for ( int32 i = 0; i < Text.Len() && !bNeedsQuote; ++i )
	{
		const unsigned char Ch = static_cast<unsigned char>( Text[i] );
		bNeedsQuote = IsSpace( static_cast<char>( Ch ) ) || Ch == '"' || Ch == '\'' || Ch == ';' || Ch == '\n';
	}
	if ( !bNeedsQuote ) return Text;

	FString Result = "\"";
	for ( int32 i = 0; i < Text.Len(); ++i )
	{
		const unsigned char Ch = static_cast<unsigned char>( Text[i] );
		if ( Ch == '"' ) Result += "\\\"";
		else if ( Ch == '\\' ) Result += "\\\\";
		else if ( Ch == '\n' ) Result += "\\n";
		else if ( Ch == '\t' ) Result += "\\t";
		else Result += static_cast<char>( Ch );
	}
	Result += "\"";
	return Result;
}

NConsole::FInputAnalysis NConsole::AnalyzeInput( const FString& Line )
{
	FInputAnalysis Analysis;

	// 最後の断片（最後の ';' より後ろ）を探す
	int32 Start = 0;
	TArray<FScanToken> Tokens;
	for ( ;; )
	{
		Tokens.Empty();
		int32 End = 0;
		Scan( Line, Start, Tokens, End );
		if ( End >= Line.Len() ) break;
		Start = End + 1;
	}

	Analysis.CurrentStart = Line.Len();
	const int32 NumTokens = Tokens.Num();
	if ( NumTokens > 0 )
	{
		const FScanToken& Last = Tokens[NumTokens - 1];
		const bool bOpen = !Last.bTerminated;
		const bool bTyping = bOpen || Last.End >= Line.Len();   // 最後の引数のすぐ後ろで入力が終わっている（空白が続いていない）
		const int32 NumDone = bTyping ? NumTokens - 1 : NumTokens;
		for ( int32 i = 0; i < NumDone; ++i ) Analysis.Tokens.Add( Tokens[i].Value );
		if ( bTyping )
		{
			Analysis.CurrentPrefix = Last.Value;
			Analysis.CurrentStart = Last.Start;
			Analysis.bInQuote = bOpen;
		}
	}
	return Analysis;
}
