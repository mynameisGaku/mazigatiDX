#include "CommandLine.h"

#include "Library/Platform/DxLib/DxLibInclude.h"

FString NCommandLine::Get()
{
	// UTF-16 で受け取って UTF-8 にする（A 版はシステムのコードページなので、日本語の引数が UTF-8 にならない）
	const wchar_t* CommandLine = GetCommandLineW();
	return CommandLine != nullptr ? FString( CommandLine ) : FString();
}

TArray<FString> NCommandLine::Split( const FString& CommandLine )
{
	TArray<FString> Tokens;
	FString Current;
	bool bInQuotes = false;
	// 空の引用符 "" も、空の引数として数える
	bool bHasToken = false;

	for ( int32 i = 0; i < CommandLine.Len(); ++i )
	{
		const char Ch = CommandLine[i];
		if ( Ch == '"' )
		{
			bInQuotes = !bInQuotes;
			bHasToken = true;
			continue;
		}
		if ( !bInQuotes && ( Ch == ' ' || Ch == '\t' ) )
		{
			if ( bHasToken )
			{
				Tokens.Add( Current );
				Current = FString();
				bHasToken = false;
			}
			continue;
		}
		Current += Ch;
		bHasToken = true;
	}
	if ( bHasToken ) Tokens.Add( Current );
	return Tokens;
}

bool NCommandLine::HasFlag( const FString& CommandLine, const char* Name )
{
	const FString Flag = FString( "--" ) + Name;
	const TArray<FString> Tokens = Split( CommandLine );
	for ( int32 i = 1; i < Tokens.Num(); ++i )
	{
		if ( Tokens[i] == Flag ) return true;
	}
	return false;
}

bool NCommandLine::HasFlag( const char* Name )
{
	return HasFlag( Get(), Name );
}

TOptional<FString> NCommandLine::GetValue( const FString& CommandLine, const char* Name )
{
	const FString Prefix = FString( "--" ) + Name + "=";
	const TArray<FString> Tokens = Split( CommandLine );
	for ( int32 i = 1; i < Tokens.Num(); ++i )
	{
		if ( Tokens[i].StartsWith( Prefix ) ) return Tokens[i].RightChop( Prefix.Len() );
	}
	return {};
}

TOptional<FString> NCommandLine::GetValue( const char* Name )
{
	return GetValue( Get(), Name );
}
