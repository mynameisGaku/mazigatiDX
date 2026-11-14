// SPDX-License-Identifier: MIT
#include "BuiltinCommands.h"

#include "Library/Framework/Console/BuiltinCommandHelpers.h"

using namespace NConsole::NBuiltin;

//=============================================================================
// アプリ全体の履歴と出力
//=============================================================================
FConsoleHistory& NConsole::GetGlobalHistory()
{
	static FConsoleHistory History( 200 );
	return History;
}

FConsoleBuffer& NConsole::GetGlobalOutput()
{
	static FConsoleBuffer Buffer( 4000 );
	return Buffer;
}

//=============================================================================
// スクリプトの実行
//=============================================================================
FString NConsole::FindScriptFile( const FString& Path )
{
	if ( Path.IsEmpty() ) return FString();
	if ( NFileSystem::FileExists( Path ) ) return Path;
	if ( TOptional<FString> Found = NFileSystem::FindFile( Path ); Found.IsSet() ) return Found.GetValue();
	const FString InConfig = NFileSystem::FindProjectRoot() / "Config" / Path;
	if ( NFileSystem::FileExists( InConfig ) ) return InConfig;
	return FString();
}

bool NConsole::ExecuteScriptFile( FConsoleRegistry& Registry, const FString& Path, IConsoleOutput& Output, FString& OutError, int32* OutNumCommands, int32* OutNumFailed )
{
	static int32 Depth = 0;
	if ( OutNumCommands != nullptr ) *OutNumCommands = 0;
	if ( OutNumFailed != nullptr ) *OutNumFailed = 0;
	if ( Depth >= 8 )
	{
		OutError = "exec is nested too deeply (more than 8 levels)";
		return false;
	}
	// スクリプトの中の相対パスは、そのスクリプトのフォルダから先に探す
	FString Found;
	if ( !GScriptDirectory.IsEmpty() && !IsAbsolutePath( Path ) && NFileSystem::FileExists( GScriptDirectory / Path ) ) Found = GScriptDirectory / Path;
	else Found = FindScriptFile( Path );
	if ( Found.IsEmpty() )
	{
		OutError = "file not found: " + Path;
		return false;
	}
	FString Text;
	if ( !ReadTextFile( Found, Text ) )
	{
		OutError = "could not read " + Found;
		return false;
	}

	++Depth;
	const FString OuterScriptDirectory = GScriptDirectory;
	GScriptDirectory = GetPathDirectory( Found );
	int32 NumCommands = 0, NumFailed = 0, LineNumber = 0;
	for ( const FString& Line : Text.ParseIntoLines( false ) )
	{
		++LineNumber;
		if ( NConsole::SplitCommands( Line ).IsEmpty() ) continue;
		++NumCommands;
		const EConsoleResult Result = Registry.Execute( Line, Output );
		if ( Result != EConsoleResult::Ok && Result != EConsoleResult::Empty )
		{
			++NumFailed;
			Output.Write( EConsoleLine::Error, FString::Printf( "  (%s line %d: %s)", *Found, LineNumber, *Line.TrimStartAndEnd() ) );
		}
	}
	GScriptDirectory = OuterScriptDirectory;
	--Depth;
	if ( OutNumCommands != nullptr ) *OutNumCommands = NumCommands;
	if ( OutNumFailed != nullptr ) *OutNumFailed = NumFailed;
	return true;
}

//=============================================================================
// 組み込みのコマンド
//=============================================================================
void NConsole::RegisterBuiltinCommands( FConsoleRegistry& Registry )
{
	// 分類ごとに、登録する（help に出る順）
	RegisterConsoleCommands( Registry );
	RegisterVariableCommands( Registry );
	RegisterEngineCommands( Registry );
	RegisterSceneCommands( Registry );
	RegisterAssetCommands( Registry );
	RegisterAudioCommands( Registry );
	RegisterProfilerCommands( Registry );
	RegisterDebugViewCommands( Registry );
	RegisterAICommands( Registry );
	RegisterLogCommands( Registry );
	RegisterInfoCommands( Registry );
}
