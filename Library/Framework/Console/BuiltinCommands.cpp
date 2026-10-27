#include "BuiltinCommands.h"


#include "Library/Core/File/File.h"
#include "Library/Core/Interfaces/Job/IJobSystem.h"
#include "Library/Core/Interfaces/Platform/IPlatform.h"
#include "Library/Core/Interfaces/Profiler/IProfiler.h"
#include "Library/Core/Log/LogManager.h"
#include "Library/Core/Log/LogVerbosity.h"
#include "Library/Core/Types/Numeric.h"
#include "Library/Core/Types/Utility.h"
#include "Library/Framework/Asset/AssetManager.h"
#include "Library/Framework/Audio/AudioSystem.h"
#include "Library/Framework/Component/TransformComponent.h"
#include "Library/Framework/Console/ConsoleParse.h"
#include "Library/Framework/Engine/Engine.h"
#include "Library/Framework/Profiler/ProfilerExport.h"
#include "Library/Framework/Scene/Scene.h"
#include "Library/Math/MathUtils.h"
#include "Library/Platform/FileSystem.h"

namespace
{
	const char* CategoryConsole = "Console";
	const char* CategoryVariables = "Variables";
	const char* CategoryEngine = "Engine";
	const char* CategoryWindow = "Window";
	const char* CategoryScene = "Scene";
	const char* CategoryObjects = "Objects";
	const char* CategoryEcs = "ECS";
	const char* CategoryAssets = "Assets";
	const char* CategoryAudio = "Audio";
	const char* CategoryProfiler = "Profiler";
	const char* CategoryLog = "Log";
	const char* CategoryInfo = "Info";

	FString FormatBytes( int64 Bytes )
	{
		const char* Units[] = { "B", "KB", "MB", "GB", "TB" };
		float64 Value = static_cast<float64>( Bytes );
		int32 Unit = 0;
		while ( Value >= 1024.0 && Unit < 4 )
		{
			Value /= 1024.0;
			++Unit;
		}
		return Unit == 0 ? FString::Printf( "%lld B", static_cast<long long>( Bytes ) ) : FString::Printf( "%.1f %s", Value, Units[Unit] );
	}

	FString Pad( const FString& Text, int32 Width )
	{
		FString Result = Text;
		while ( Result.Len() < Width ) Result += " ";
		return Result;
	}

	FString Vec3Text( const FVector3& V )
	{
		return FString::Printf( "(%.3f, %.3f, %.3f)", V.x, V.y, V.z );
	}

	CSceneManager* FindSceneManager()
	{
		CEngine* Engine = CEngine::Get();
		CGameInstance* GameInstance = Engine != nullptr ? Engine->GetGameInstance() : nullptr;
		return GameInstance != nullptr ? &GameInstance->GetSceneManager() : nullptr;
	}

	CScene* FindCurrentScene()
	{
		CSceneManager* Manager = FindSceneManager();
		return Manager != nullptr ? Manager->GetCurrentScene() : nullptr;
	}

	// 今のシーンのオブジェクトを Id で探す（無ければ空）
	TSharedPtr<CGameObject> FindObjectByIdSafe( int64 Id )
	{
		CScene* Scene = FindCurrentScene();
		if ( Scene == nullptr || Id < 0 ) return TSharedPtr<CGameObject>();
		return Scene->FindObjectById( static_cast<uint64>( Id ) );
	}

	TArray<FString> GetSceneNames()
	{
		CSceneManager* Manager = FindSceneManager();
		return Manager != nullptr ? Manager->GetRegisteredSceneNames() : TArray<FString>();
	}

	TArray<FString> GetBusNames()
	{
		TArray<FString> Names;
		for ( int32 i = 0; i < static_cast<int32>( EAudioBus::Count ); ++i ) Names.Add( ToString( static_cast<EAudioBus>( i ) ) );
		return Names;
	}

	bool ParseBus( const FString& Text, EAudioBus& OutBus )
	{
		return TryParse( Text, OutBus );
	}

	TArray<FString> GetVerbosityNames()
	{
		TArray<FString> Names;
		for ( int32 i = 0; i <= static_cast<int32>( ELogVerbosity::VeryVerbose ); ++i ) Names.Add( LogVerbosityToString( static_cast<ELogVerbosity>( i ) ) );
		return Names;
	}

	TArray<FString> GetLogCategoryNames()
	{
		TArray<FString> Names;
		for ( FLogCategory* Category : CLogManager::Get().GetCategories() ) Names.Add( Category->GetName() );
		Names.Sort();
		return Names;
	}

	FLogCategory* FindLogCategory( const FString& Name )
	{
		for ( FLogCategory* Category : CLogManager::Get().GetCategories() )
		{
			if ( Name.Equals( Category->GetName(), ESearchCase::IgnoreCase ) ) return Category;
		}
		return nullptr;
	}

	// バイト列が UTF-8 として正しいか（ASCII だけでも true）
	bool IsValidUtf8( const uint8* Bytes, int32 Num )
	{
		int32 i = 0;
		while ( i < Num )
		{
			const uint8 Ch = Bytes[i];
			int32 Extra = 0;
			if ( Ch < 0x80 ) Extra = 0;
			else if ( ( Ch & 0xE0 ) == 0xC0 ) Extra = 1;
			else if ( ( Ch & 0xF0 ) == 0xE0 ) Extra = 2;
			else if ( ( Ch & 0xF8 ) == 0xF0 ) Extra = 3;
			else return false;
			for ( int32 k = 1; k <= Extra; ++k )
			{
				if ( i + k >= Num || ( Bytes[i + k] & 0xC0 ) != 0x80 ) return false;
			}
			i += Extra + 1;
		}
		return true;
	}

	bool ReadTextFile( const FString& Path, FString& OutText )
	{
		TArray<uint8> Raw;
		if ( !NFile::ReadAllBytes( Path, Raw ) ) return false;
		// UTF-8 の BOM は読み飛ばす
		int32 Begin = 0;
		if ( Raw.Num() >= 3 && Raw[0] == 0xEF && Raw[1] == 0xBB && Raw[2] == 0xBF ) Begin = 3;
		const uint8* const Bytes = Raw.GetData() + Begin;
		const int32 Num = Raw.Num() - Begin;
		const FString Text( reinterpret_cast<const char*>( Bytes ), Num );
		// UTF-8 の文字が入っていれば UTF-8 として読み、そうでなければ（ASCII だけ・CP932）そのまま読む
		bool bHasHigh = false;
		for ( int32 i = 0; i < Num; ++i ) bHasHigh = bHasHigh || Bytes[i] >= 0x80;
		if ( bHasHigh && IsValidUtf8( Bytes, Num ) ) OutText = FString::FromUtf8( *Text );
		else OutText = Text;
		return true;
	}

	// パスのフォルダの部分（最後の区切りの手前。区切りが無ければ空）。Shift-JIS の 2 バイト目が 0x5C の文字を区切りと間違えない
	FString GetPathDirectory( const FString& Path )
	{
		int32 LastSeparator = INDEX_NONE;
		for ( int32 i = 0; i < Path.Len(); ++i )
		{
			const unsigned char Ch = static_cast<unsigned char>( Path[i] );
			if ( NConsole::IsLeadByte( Ch ) )
			{
				++i;
				continue;
			}
			if ( Ch == '/' || Ch == '\\' ) LastSeparator = i;
		}
		return LastSeparator == INDEX_NONE ? FString() : Path.Left( LastSeparator );
	}

	bool IsAbsolutePath( const FString& Path )
	{
		return Path.Len() >= 2 && ( Path[1] == ':' || ( ( Path[0] == '/' || Path[0] == '\\' ) && ( Path[1] == '/' || Path[1] == '\\' ) ) );
	}

	// 実行中のスクリプトのフォルダ（入れ子の exec が、読み込み元のフォルダを基準に探すため）
	FString GScriptDirectory;

	// 引数（コマンド名を除く）の Start 番目から後ろを、引用符つきでつないだ 1 行にする（repeat / alias が、中のコマンドをそのまま渡す用）
	FString JoinQuoted( const TArray<FString>& Args, int32 Start )
	{
		FString Line;
		for ( int32 i = Start; i < Args.Num(); ++i )
		{
			if ( i > Start ) Line += " ";
			Line += NConsole::Quote( Args[i] );
		}
		return Line;
	}

	//-------------------------------------------------------------------------
	// help
	//-------------------------------------------------------------------------
	void PrintCommandDetails( FConsoleContext& Context, const FConsoleCommandInfo& Info )
	{
		Context.Print( Info.Name + " - " + ( Info.Description.IsEmpty() ? FString( "(no description)" ) : Info.Description ) );
		Context.Print( "  usage: " + Info.GetUsage() );
		// 説明のある引数だけ、詳しく出す
		for ( const FConsoleArgDesc& Arg : Info.Args )
		{
			if ( Arg.Description.IsEmpty() ) continue;
			Context.Print( "  " + Arg.Name + " (" + Arg.TypeName + ( Arg.bOptional ? ", optional" : "" ) + "): " + Arg.Description );
		}
		if ( Info.bVariable )
		{
			FString Value;
			if ( Context.GetRegistry().GetVariableText( Info.Name, Value ) ) Context.Print( "  current value: " + Value );
		}
	}

	bool PrintHelp( FConsoleContext& Context, const TOptional<FString>& Name )
	{
		FConsoleRegistry& Registry = Context.GetRegistry();
		if ( Name.IsSet() )
		{
			if ( const FConsoleCommandInfo* Info = Registry.Find( Name.GetValue() ) )
			{
				PrintCommandDetails( Context, *Info );
				return true;
			}
			if ( const FString* Alias = Registry.FindAlias( Name.GetValue() ) )
			{
				Context.Print( Name.GetValue() + " is an alias for: " + *Alias );
				return true;
			}
			Context.Error( "unknown command '" + Name.GetValue() + "'. Use 'find <text>' to search." );
			return false;
		}

		Context.Print( "Commands (type 'help <command>' for details, Tab to complete):" );
		FString CurrentGroup;
		for ( const FConsoleCommandInfo* Info : Registry.GetCommands() )
		{
			const FString Group = Info->Category.IsEmpty() ? FString( "General" ) : Info->Category;
			if ( Group != CurrentGroup )
			{
				CurrentGroup = Group;
				Context.Print( "[" + Group + "]" );
			}
			Context.Print( "  " + Pad( Info->Name, 22 ) + Info->Description );
		}
		const TArray<TPair<FString, FString>> Aliases = Registry.GetAliases();
		if ( !Aliases.IsEmpty() )
		{
			Context.Print( "[Aliases]" );
			for ( const TPair<FString, FString>& Alias : Aliases ) Context.Print( "  " + Pad( Alias.Key, 22 ) + Alias.Value );
		}
		return true;
	}

	//-------------------------------------------------------------------------
	// assets
	//-------------------------------------------------------------------------
	FString DescribeAssets( CAssetManager& Assets )
	{
		Assets.UpdateStats();
		const FAssetStats& Stats = Assets.GetStats();
		FString Text = FString::Printf( "%d asset(s), ~%s in memory (peak ~%s), %s on disk\n", Stats.TotalCount, *FormatBytes( Stats.TotalMemoryBytes ), *FormatBytes( Stats.PeakMemoryBytes ), *FormatBytes( Stats.TotalFileBytes ) );
		for ( int32 i = 0; i < NumAssetTypes; ++i )
		{
			const FAssetTypeStats& Type = Stats.Types[i];
			Text += FString::Printf( "  %-15s %4d  ~%-10s file %-10s load %.1f ms%s\n", ToString( static_cast<EAssetType>( i ) ), Type.Count, *FormatBytes( Type.MemoryBytes ), *FormatBytes( Type.FileBytes ), Type.LoadMs,
									 Type.NumNotReady > 0 ? " (loading)" : "" );
		}
		Text += FString::Printf( "loads %d (%.1f ms total, slowest %.1f ms), %d shared, %d failed, %d loading now", Stats.NumLoads, Stats.TotalLoadMs, Stats.MaxLoadMs, Stats.NumCacheHits, Stats.NumLoadFailures, Stats.NumLoading );
		if ( Stats.Backend.VideoMemoryTotal > 0 ) Text += FString::Printf( "\nvideo memory %s / %s, process %s", *FormatBytes( Stats.Backend.VideoMemoryUsed ), *FormatBytes( Stats.Backend.VideoMemoryTotal ), *FormatBytes( Stats.Backend.ProcessWorkingSet ) );
		Text += FString::Printf( "\nhandles: %d graph, %d model, %d sound, %d font", Stats.Backend.NumTextureHandles, Stats.Backend.NumModelHandles, Stats.Backend.NumSoundHandles, Stats.Backend.NumFontHandles );
		return Text;
	}

	const char* NotRunning = "not available (the engine or the subsystem is not running)";
}

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
	const auto Add = [&Registry]( const char* Name, const char* Description, auto&& Function, const TArray<FConsoleArgSpec>& Args, const char* Category )
	{
		Registry.Add( Name, Description, Forward<decltype( Function )>( Function ), Args, Category ).Keep();
	};

	//-------------------------------------------------------------------------
	// Console
	//-------------------------------------------------------------------------
	Add( "help", "Show the list of commands, or the usage of one command",
		 []( FConsoleContext& Context, TOptional<FString> Name ) { return PrintHelp( Context, Name ); },
		 { { "command", "Name of the command (Tab completes it). Without it, all commands are listed." } }, CategoryConsole );
	Registry.SetArgCompleter( "help", 0, [&Registry]()
	{
		TArray<FString> Names;
		for ( const FConsoleCommandInfo* Info : Registry.GetCommands() ) Names.Add( Info->Name );
		return Names;
	} );

	Add( "find", "Search the command names and descriptions for a text",
		 []( FConsoleContext& Context, const FString& Text )
		 {
			 int32 Count = 0;
			 for ( const FConsoleCommandInfo* Info : Context.GetRegistry().GetCommands() )
			 {
				 if ( Info->Name.Contains( Text, ESearchCase::IgnoreCase ) || Info->Description.Contains( Text, ESearchCase::IgnoreCase ) )
				 {
					 Context.Print( "  " + Pad( Info->Name, 22 ) + Info->Description );
					 ++Count;
				 }
			 }
			 if ( Count == 0 ) Context.Print( "no command matches '" + Text + "'" );
		 },
		 { { "text", "Part of a command name or description (not case sensitive)." } }, CategoryConsole );

	Add( "echo", "Print a text to the console",
		 []( const FConsoleRest& Text ) { return Text.Text; }, { { "text", "The text to print (all the rest of the line)." } }, CategoryConsole );

	Add( "clear", "Clear the console output", []( FConsoleContext& Context ) { Context.ClearOutput(); }, {}, CategoryConsole );

	Add( "history", "Show the commands entered before",
		 []( FConsoleContext& Context, TOptional<int32> Count )
		 {
			 const FConsoleHistory& History = GetGlobalHistory();
			 const int32 Show = Count.IsSet() ? NMath::Max( Count.GetValue(), 1 ) : 20;
			 const int32 First = NMath::Max( History.Num() - Show, 0 );
			 for ( int32 i = First; i < History.Num(); ++i ) Context.Print( FString::Printf( "  %3d  %s", i + 1, *History.Get( i ) ) );
			 if ( History.Num() == 0 ) Context.Print( "(no history yet)" );
		 },
		 { { "count", "How many of the latest entries to show (default 20)." } }, CategoryConsole );

	Add( "alias", "Define, show or list aliases (a short name for a command line)",
		 []( FConsoleContext& Context, TOptional<FString> Name, TOptional<FConsoleRest> )
		 {
			 FConsoleRegistry& Registry = Context.GetRegistry();
			 if ( !Name.IsSet() )
			 {
				 const TArray<TPair<FString, FString>> Aliases = Registry.GetAliases();
				 if ( Aliases.IsEmpty() ) Context.Print( "(no aliases) Define one with: alias <name> <command line>" );
				 for ( const TPair<FString, FString>& Alias : Aliases ) Context.Print( "  " + Pad( Alias.Key, 16 ) + Alias.Value );
				 return true;
			 }
			 if ( Context.GetArgs().Num() < 2 )
			 {
				 const FString* Line = Registry.FindAlias( Name.GetValue() );
				 if ( Line == nullptr )
				 {
					 Context.Error( "no alias named '" + Name.GetValue() + "'" );
					 return false;
				 }
				 Context.Print( Name.GetValue() + " = " + *Line );
				 return true;
			 }
			 FString Error;
			 if ( !Registry.SetAlias( Name.GetValue(), JoinQuoted( Context.GetArgs(), 1 ), Error ) )
			 {
				 Context.Error( Error );
				 return false;
			 }
			 Context.Print( "alias " + Name.GetValue() + " = " + JoinQuoted( Context.GetArgs(), 1 ) );
			 return true;
		 },
		 { { "name", "The alias name. Without a command line, shows that alias." }, { "command", "The command line the alias runs. Arguments typed after the alias are added to its end." } }, CategoryConsole );

	Add( "unalias", "Remove an alias",
		 []( FConsoleContext& Context, const FString& Name )
		 {
			 if ( Context.GetRegistry().RemoveAlias( Name ) ) return true;
			 Context.Error( "no alias named '" + Name + "'" );
			 return false;
		 },
		 { "name" }, CategoryConsole );

	Add( "exec", "Run the commands in a text file (one per line; lines starting with # or // are comments)",
		 []( FConsoleContext& Context, const FString& Path )
		 {
			 FString Error;
			 int32 NumCommands = 0, NumFailed = 0;
			 if ( !ExecuteScriptFile( Context.GetRegistry(), Path, Context.GetOutput(), Error, &NumCommands, &NumFailed ) )
			 {
				 Context.Error( Error );
				 return false;
			 }
			 Context.Print( FString::Printf( "ran %d command(s) from %s%s", NumCommands, *Path, NumFailed > 0 ? *FString::Printf( ", %d failed", NumFailed ) : "" ) );
			 return NumFailed == 0;
		 },
		 { { "file", "Path of the script. Searched in the working folder, near the executable, then in the project's Config folder." } }, CategoryConsole );

	Add( "repeat", "Run a command line several times",
		 []( FConsoleContext& Context, int32 Count, const FConsoleRest& )
		 {
			 if ( Count < 1 || Count > 1000 )
			 {
				 Context.Error( "count must be between 1 and 1000" );
				 return false;
			 }
			 const FString Line = JoinQuoted( Context.GetArgs(), 1 );
			 if ( Line.IsEmpty() )
			 {
				 Context.Error( "give a command line to repeat: repeat <count> <command...>" );
				 return false;
			 }
			 bool bOk = true;
			 for ( int32 i = 0; i < Count; ++i )
			 {
				 const EConsoleResult Result = Context.GetRegistry().Execute( Line, Context.GetOutput() );
				 bOk = bOk && ( Result == EConsoleResult::Ok );
			 }
			 return bOk;
		 },
		 { { "count", "How many times (1 - 1000)." }, { "command", "The command line to run." } }, CategoryConsole );

	Add( "quit", "Exit the application",
		 []( FConsoleContext& Context )
		 {
			 IPlatform* Platform = GetAppSubsystem<IPlatform>();
			 if ( Platform == nullptr ) return false;
			 Context.Print( "exiting..." );
			 Platform->RequestExit();
			 return true;
		 },
		 {}, CategoryConsole );
	Add( "exit", "Exit the application (same as quit)", []( FConsoleContext& Context ) { return Context.GetRegistry().Execute( "quit", Context.GetOutput() ) == EConsoleResult::Ok; }, {}, CategoryConsole );

	//-------------------------------------------------------------------------
	// Variables
	//-------------------------------------------------------------------------
	Add( "vars", "List the console variables and their values",
		 []( FConsoleContext& Context, TOptional<FString> Filter )
		 {
			 int32 Count = 0;
			 for ( const FConsoleCommandInfo* Info : Context.GetRegistry().GetCommands() )
			 {
				 if ( !Info->bVariable ) continue;
				 if ( Filter.IsSet() && !Info->Name.Contains( Filter.GetValue(), ESearchCase::IgnoreCase ) ) continue;
				 FString Value;
				 Context.GetRegistry().GetVariableText( Info->Name, Value );
				 Context.Print( "  " + Pad( Info->Name, 22 ) + Pad( Value, 14 ) + Info->Description );
				 ++Count;
			 }
			 if ( Count == 0 ) Context.Print( "(no variables)" );
		 },
		 { { "filter", "Only the variables whose name contains this text." } }, CategoryVariables );

	Add( "set", "Set a console variable (same as typing its name and the value)",
		 []( FConsoleContext& Context, const FString& Name, const FString& Value )
		 {
			 const FConsoleCommandInfo* Info = Context.GetRegistry().Find( Name );
			 if ( Info == nullptr || !Info->bVariable )
			 {
				 Context.Error( "'" + Name + "' is not a variable (see 'vars')" );
				 return false;
			 }
			 return Context.GetRegistry().Execute( NConsole::Quote( Name ) + " " + NConsole::Quote( Value ), Context.GetOutput() ) == EConsoleResult::Ok;
		 },
		 { { "name", "The variable name." }, { "value", "The new value." } }, CategoryVariables );
	Registry.SetArgCompleter( "set", 0, [&Registry]()
	{
		TArray<FString> Names;
		for ( const FConsoleCommandInfo* Info : Registry.GetCommands() )
		{
			if ( Info->bVariable ) Names.Add( Info->Name );
		}
		return Names;
	} );

	Add( "toggle", "Flip a true / false console variable",
		 []( FConsoleContext& Context, const FString& Name )
		 {
			 FString Text;
			 bool bValue = false;
			 if ( !Context.GetRegistry().GetVariableText( Name, Text ) || !NConsole::ParseBool( Text, bValue ) )
			 {
				 Context.Error( "'" + Name + "' is not a true / false variable (see 'vars')" );
				 return false;
			 }
			 return Context.GetRegistry().Execute( NConsole::Quote( Name ) + ( bValue ? " false" : " true" ), Context.GetOutput() ) == EConsoleResult::Ok;
		 },
		 { { "name", "The variable name." } }, CategoryVariables );
	Registry.SetArgCompleter( "toggle", 0, [&Registry]()
	{
		TArray<FString> Names;
		for ( const FConsoleCommandInfo* Info : Registry.GetCommands() )
		{
			FString Text;
			bool bValue = false;
			if ( Info->bVariable && Registry.GetVariableText( Info->Name, Text ) && NConsole::ParseBool( Text, bValue ) ) Names.Add( Info->Name );
		}
		return Names;
	} );

	Add( "reset", "Put a console variable back to the value it had when it was registered",
		 []( FConsoleContext& Context, const FString& Name )
		 {
			 if ( !Context.GetRegistry().ResetVariable( Name ) )
			 {
				 Context.Error( "'" + Name + "' is not a variable (see 'vars')" );
				 return false;
			 }
			 FString Value;
			 Context.GetRegistry().GetVariableText( Name, Value );
			 Context.Print( Name + " = " + Value );
			 return true;
		 },
		 { { "name", "The variable name." } }, CategoryVariables );
	Registry.SetArgCompleter( "reset", 0, [&Registry]()
	{
		TArray<FString> Names;
		for ( const FConsoleCommandInfo* Info : Registry.GetCommands() )
		{
			if ( Info->bVariable ) Names.Add( Info->Name );
		}
		return Names;
	} );

	// 変数
	Registry.AddVariable<float32>( "timescale", []() { CEngine* Engine = CEngine::Get(); return Engine != nullptr ? Engine->GetTimeControl().GetScale() : 1.0f; },
								   []( const float32& Value ) { if ( CEngine* Engine = CEngine::Get() ) Engine->GetTimeControl().SetScale( Value ); },
								   "Game speed multiplier (1 = normal, 0.5 = half speed, 0 = frozen)",
								   [] { TConsoleVariableOptions<float32> Options; Options.Min = 0.0f; Options.Max = FEngineTimeControl::MaxScale; return Options; }(), CategoryEngine ).Keep();
	Registry.AddVariable<FString>( "window.title", []() { IPlatform* Platform = GetAppSubsystem<IPlatform>(); return Platform != nullptr ? Platform->GetWindowTitle() : FString(); },
								   []( const FString& Value ) { if ( IPlatform* Platform = GetAppSubsystem<IPlatform>() ) Platform->SetWindowTitle( Value ); },
								   "The window title", TConsoleVariableOptions<FString>(), CategoryWindow ).Keep();
	Registry.AddVariable<bool>( "cursor.visible", []() { IPlatform* Platform = GetAppSubsystem<IPlatform>(); return Platform != nullptr && Platform->IsMouseCursorVisible(); },
								[]( const bool& Value ) { if ( IPlatform* Platform = GetAppSubsystem<IPlatform>() ) Platform->SetMouseCursorVisible( Value ); },
								"Whether the mouse cursor is shown", TConsoleVariableOptions<bool>(), CategoryWindow ).Keep();
	Registry.AddVariable<float32>( "audio.master", []() { CAudioSystem* Audio = GetAppSubsystem<CAudioSystem>(); return Audio != nullptr ? Audio->GetBusVolume( EAudioBus::Master ) : 1.0f; },
								   []( const float32& Value ) { if ( CAudioSystem* Audio = GetAppSubsystem<CAudioSystem>() ) Audio->SetBusVolume( EAudioBus::Master, Value ); },
								   "Master volume (1 = normal)", [] { TConsoleVariableOptions<float32> Options; Options.Min = 0.0f; Options.Max = 2.0f; return Options; }(), CategoryAudio ).Keep();

	//-------------------------------------------------------------------------
	// Engine / Window
	//-------------------------------------------------------------------------
	Add( "fps", "Show the frame rate, the frame time and the frame count",
		 []()
		 {
			 IPlatform* Platform = GetAppSubsystem<IPlatform>();
			 if ( Platform == nullptr ) return FString( NotRunning );
			 return FString::Printf( "%.1f FPS, frame time %.2f ms, frame %llu", Platform->GetFPS(), Platform->GetDeltaTime() * 1000.0f, static_cast<unsigned long long>( Platform->GetFrameCount() ) );
		 },
		 {}, CategoryEngine );

	Add( "time", "Show the time since start, the time scale and whether the game is paused",
		 []()
		 {
			 IPlatform* Platform = GetAppSubsystem<IPlatform>();
			 CEngine* Engine = CEngine::Get();
			 if ( Platform == nullptr || Engine == nullptr ) return FString( NotRunning );
			 const FEngineTimeControl& Control = Engine->GetTimeControl();
			 return FString::Printf( "%.2f s since start, timescale %s, %s%s", Platform->GetTime(), *NConsole::FormatFloat( Control.GetScale() ), Control.IsPaused() ? "paused" : "running",
									 Control.GetStepsRemaining() > 0 ? *FString::Printf( " (%d step(s) left)", Control.GetStepsRemaining() ) : "" );
		 },
		 {}, CategoryEngine );

	Add( "pause", "Pause or resume the game (Tick gets a delta time of 0; drawing and the debug UI keep running)",
		 []( FConsoleContext& Context, TOptional<bool> On )
		 {
			 CEngine* Engine = CEngine::Get();
			 if ( Engine == nullptr ) return false;
			 FEngineTimeControl& Control = Engine->GetTimeControl();
			 const bool bPause = On.IsSet() ? On.GetValue() : !Control.IsPaused();
			 if ( bPause ) Control.Pause();
			 else Control.Resume();
			 Context.Print( bPause ? "paused (use 'step' to advance, 'resume' to continue)" : "resumed" );
			 return true;
		 },
		 { { "on", "true to pause, false to resume. Without it, switches." } }, CategoryEngine );

	Add( "resume", "Resume the game after pause",
		 []( FConsoleContext& Context )
		 {
			 CEngine* Engine = CEngine::Get();
			 if ( Engine == nullptr ) return false;
			 Engine->GetTimeControl().Resume();
			 Context.Print( "resumed" );
			 return true;
		 },
		 {}, CategoryEngine );

	Add( "step", "Advance the paused game by a few frames (1 frame = 1/60 s)",
		 []( FConsoleContext& Context, TOptional<int32> Frames )
		 {
			 CEngine* Engine = CEngine::Get();
			 if ( Engine == nullptr ) return false;
			 const int32 Count = Frames.IsSet() ? NMath::Max( Frames.GetValue(), 1 ) : 1;
			 Engine->GetTimeControl().Step( Count );
			 Context.Print( FString::Printf( "stepping %d frame(s)", Count ) );
			 return true;
		 },
		 { { "frames", "How many frames to advance (default 1). Also pauses the game." } }, CategoryEngine );

	Add( "jobs", "Show the number of job system worker threads",
		 []()
		 {
			 IJobSystem* Jobs = GetAppSubsystem<IJobSystem>();
			 return Jobs != nullptr ? FString::Printf( "%d worker thread(s)", Jobs->GetNumWorkers() ) : FString( NotRunning );
		 },
		 {}, CategoryEngine );

	Add( "memory", "Show the process memory, the video memory and the asset memory",
		 []()
		 {
			 CAssetManager* Assets = GetAppSubsystem<CAssetManager>();
			 if ( Assets == nullptr ) return FString( NotRunning );
			 Assets->UpdateStats();
			 const FAssetStats& Stats = Assets->GetStats();
			 FString Text = FString::Printf( "process %s", *FormatBytes( Stats.Backend.ProcessWorkingSet ) );
			 if ( Stats.Backend.VideoMemoryTotal > 0 ) Text += FString::Printf( ", video %s / %s", *FormatBytes( Stats.Backend.VideoMemoryUsed ), *FormatBytes( Stats.Backend.VideoMemoryTotal ) );
			 Text += FString::Printf( ", assets ~%s (peak ~%s)", *FormatBytes( Stats.TotalMemoryBytes ), *FormatBytes( Stats.PeakMemoryBytes ) );
			 return Text;
		 },
		 {}, CategoryEngine );

	Add( "screenshot", "Save a screenshot (PNG) of the current frame, debug UI included",
		 []( FConsoleContext& Context, TOptional<FString> Path, TOptional<float32> Delay )
		 {
			 IPlatform* Platform = GetAppSubsystem<IPlatform>();
			 if ( Platform == nullptr ) return false;
			 FString Target;
			 if ( Path.IsSet() && !Path.GetValue().IsEmpty() ) Target = Path.GetValue();
			 else Target = FString::Printf( "%s_%s.png", *NFileSystem::GetExecutableName(), *NFileSystem::GetCurrentLocalTime().ToFileNameString() );
			 const bool bAbsolute = Target.Find( ":" ) != INDEX_NONE || Target.StartsWith( "/" ) || Target.StartsWith( "\\" );
			 if ( !bAbsolute ) Target = NFileSystem::FindProjectRoot() / "Saved" / "Screenshots" / Target;
			 if ( !Target.EndsWith( ".png", ESearchCase::IgnoreCase ) ) Target += ".png";
			 Target = Target.Replace( "\\", "/" );
			 const int32 Slash = Target.FindLast( "/" );
			 if ( Slash > 0 ) NFileSystem::EnsureDirectory( Target.Left( Slash ) );
			 const float32 Seconds = Delay.IsSet() ? NMath::Max( Delay.GetValue(), 0.0f ) : 0.0f;
			 const int32 DelayFrames = static_cast<int32>( Seconds * NMath::Max( Platform->GetFPS(), 1.0f ) + 0.5f );
			 if ( !Platform->RequestScreenshot( Target, DelayFrames ) )
			 {
				 Context.Error( "the platform cannot take a screenshot" );
				 return false;
			 }
			 Context.Print( DelayFrames > 0 ? FString::Printf( "saving %s in %s s (%d frame(s))", *Target, *NConsole::FormatFloat( Seconds ), DelayFrames ) : "saving " + Target + " at the end of this frame" );
			 return true;
		 },
		 { { "path", "Output file. A relative path is placed under Saved/Screenshots. Default: <project>_<date and time>.png" },
		   { "delay", "Seconds to wait before taking it, so you can set the screen up (default 0)." } }, CategoryEngine );

	Add( "window", "Show the window size, mode and title",
		 []()
		 {
			 IPlatform* Platform = GetAppSubsystem<IPlatform>();
			 if ( Platform == nullptr ) return FString( NotRunning );
			 return FString::Printf( "%d x %d, %s, title '%s'", Platform->GetScreenWidth(), Platform->GetScreenHeight(), WindowModeToString( Platform->GetWindowMode() ), *Platform->GetWindowTitle() );
		 },
		 {}, CategoryWindow );

	Add( "windowmode", "Show or change the window mode",
		 []( FConsoleContext& Context, TOptional<FString> Mode )
		 {
			 IPlatform* Platform = GetAppSubsystem<IPlatform>();
			 if ( Platform == nullptr ) return false;
			 if ( !Mode.IsSet() )
			 {
				 Context.Print( WindowModeToString( Platform->GetWindowMode() ) );
				 return true;
			 }
			 const TOptional<EWindowMode> Parsed = WindowModeFromString( Mode.GetValue() );
			 if ( !Parsed.IsSet() )
			 {
				 Context.Error( "unknown window mode '" + Mode.GetValue() + "' (windowed / borderless / fullscreen)" );
				 return false;
			 }
			 Platform->SetWindowMode( Parsed.GetValue() );
			 Context.Print( FString( "window mode: " ) + WindowModeToString( Platform->GetWindowMode() ) );
			 return true;
		 },
		 { { "mode", "windowed, borderless or fullscreen. Without it, shows the current mode." } }, CategoryWindow );
	Registry.SetArgChoices( "windowmode", 0, { "windowed", "borderless", "fullscreen" } );

	Add( "fullscreen", "Switch between the window and the last full screen mode",
		 []( FConsoleContext& Context )
		 {
			 IPlatform* Platform = GetAppSubsystem<IPlatform>();
			 if ( Platform == nullptr ) return false;
			 Platform->ToggleFullscreen();
			 Context.Print( FString( "window mode: " ) + WindowModeToString( Platform->GetWindowMode() ) );
			 return true;
		 },
		 {}, CategoryWindow );

	//-------------------------------------------------------------------------
	// Scene / Objects / ECS
	//-------------------------------------------------------------------------
	Add( "scene", "Show the current scene, or change to another registered scene",
		 []( FConsoleContext& Context, TOptional<FString> Name )
		 {
			 CSceneManager* Manager = FindSceneManager();
			 if ( Manager == nullptr ) return false;
			 if ( !Name.IsSet() )
			 {
				 const CScene* Scene = Manager->GetCurrentScene();
				 Context.Print( Scene != nullptr ? "current scene: " + Scene->GetName() + FString::Printf( " (%d object(s))", Scene->GetNumObjects() ) : FString( "no scene is running" ) );
				 return true;
			 }
			 if ( !Manager->IsSceneRegistered( Name.GetValue() ) )
			 {
				 Context.Error( "scene '" + Name.GetValue() + "' is not registered (see 'scenes')" );
				 return false;
			 }
			 Manager->ChangeScene( Name.GetValue() );
			 Context.Print( "changing to " + Name.GetValue() + " at the start of the next frame" );
			 return true;
		 },
		 { { "name", "A registered scene name (see 'scenes'). Without it, shows the current scene." } }, CategoryScene );
	Registry.SetArgCompleter( "scene", 0, []() { return GetSceneNames(); } );

	Add( "scenes", "List the registered scenes and the scenes on the stack",
		 []( FConsoleContext& Context )
		 {
			 CSceneManager* Manager = FindSceneManager();
			 if ( Manager == nullptr ) return false;
			 Context.Print( "registered: " + FString::Join( GetSceneNames(), ", " ) );
			 FString Stack;
			 for ( const TUniquePtr<CScene>& Scene : Manager->GetSceneStack() ) Stack += ( Stack.IsEmpty() ? "" : " > " ) + Scene->GetName();
			 Context.Print( "stack (bottom to top): " + ( Stack.IsEmpty() ? FString( "(empty)" ) : Stack ) );
			 return true;
		 },
		 {}, CategoryScene );

	Add( "push", "Push a registered scene on top of the current one (the scene below pauses)",
		 []( FConsoleContext& Context, const FString& Name )
		 {
			 CSceneManager* Manager = FindSceneManager();
			 if ( Manager == nullptr ) return false;
			 if ( !Manager->IsSceneRegistered( Name ) )
			 {
				 Context.Error( "scene '" + Name + "' is not registered (see 'scenes')" );
				 return false;
			 }
			 Manager->PushScene( Name );
			 return true;
		 },
		 { "name" }, CategoryScene );
	Registry.SetArgCompleter( "push", 0, []() { return GetSceneNames(); } );

	Add( "pop", "Remove the top scene (the scene below resumes)",
		 []()
		 {
			 CSceneManager* Manager = FindSceneManager();
			 if ( Manager == nullptr ) return false;
			 Manager->PopScene();
			 return true;
		 },
		 {}, CategoryScene );

	Add( "reload", "Restart the current scene",
		 []( FConsoleContext& Context )
		 {
			 CSceneManager* Manager = FindSceneManager();
			 const CScene* Scene = Manager != nullptr ? Manager->GetCurrentScene() : nullptr;
			 if ( Scene == nullptr || !Manager->IsSceneRegistered( Scene->GetName() ) )
			 {
				 Context.Error( "the current scene is not a registered scene" );
				 return false;
			 }
			 Manager->ChangeScene( Scene->GetName() );
			 Context.Print( "restarting " + Scene->GetName() + " at the start of the next frame" );
			 return true;
		 },
		 {}, CategoryScene );

	Add( "objects", "List the objects in the current scene (id, name, position, state)",
		 []( FConsoleContext& Context, TOptional<FString> Filter )
		 {
			 CScene* Scene = FindCurrentScene();
			 if ( Scene == nullptr ) return false;
			 constexpr int32 MaxLines = 200;
			 int32 Matched = 0;
			 for ( const TSharedPtr<CGameObject>& Object : Scene->GetObjects() )
			 {
				 const FString Name = Object->GetDebugName();
				 if ( Filter.IsSet() && !Name.Contains( Filter.GetValue(), ESearchCase::IgnoreCase ) ) continue;
				 if ( ++Matched > MaxLines ) continue;
				 FString Line = FString::Printf( "  #%-5llu %s", static_cast<unsigned long long>( Object->GetId() ), *Name );
				 if ( const CTransformComponent* Transform = Object->GetComponent<CTransformComponent>() ) Line += "  " + Vec3Text( Transform->GetWorldPosition() );
				 if ( !Object->IsVisible() ) Line += "  [hidden]";
				 if ( !Object->IsTickEnabled() ) Line += "  [no tick]";
				 if ( Object->IsPendingDestroy() ) Line += "  [destroying]";
				 Context.Print( Line );
			 }
			 if ( Matched > MaxLines ) Context.Print( FString::Printf( "  ... %d more (narrow it with a filter)", Matched - MaxLines ) );
			 Context.Print( FString::Printf( "%d object(s)%s", Matched, Filter.IsSet() ? " matched" : "" ) );
			 return true;
		 },
		 { { "filter", "Only the objects whose name contains this text." } }, CategoryObjects );

	Add( "object", "Show the details of one object: parent, children, transform and components",
		 []( FConsoleContext& Context, int64 Id )
		 {
			 if ( FindCurrentScene() == nullptr ) return false;
			 const TSharedPtr<CGameObject> Object = FindObjectByIdSafe( Id );
			 if ( !Object.IsValid() )
			 {
				 Context.Error( FString::Printf( "no object with id %lld in this scene (see 'objects')", static_cast<long long>( Id ) ) );
				 return false;
			 }
			 Context.Print( Object->GetDebugName() + FString::Printf( "  (type %s)", *Object->GetTypeName() ) );
			 Context.Print( FString::Printf( "  visible %s, tick %s, draw order %d%s", Object->IsVisible() ? "yes" : "no", Object->IsTickEnabled() ? "on" : "off", Object->GetDrawOrder(),
											 Object->IsPendingDestroy() ? ", destroying" : "" ) );
			 if ( const CGameObject* Parent = Object->GetParent() ) Context.Print( "  parent: " + Parent->GetDebugName() );
			 for ( const CGameObject* Child : Object->GetChildren() ) Context.Print( "  child: " + Child->GetDebugName() );
			 if ( const CTransformComponent* Transform = Object->GetComponent<CTransformComponent>() )
			 {
				 const FTransform& Local = Transform->GetLocalTransform();
				 const FVector3 Euler = Local.Rotation.ToEuler();
				 Context.Print( "  local position " + Vec3Text( Local.Position ) + ", rotation (deg) " + Vec3Text( FVector3( NMath::ToDegrees( Euler.x ), NMath::ToDegrees( Euler.y ), NMath::ToDegrees( Euler.z ) ) ) + ", scale " + Vec3Text( Local.Scale ) );
				 Context.Print( "  world position " + Vec3Text( Transform->GetWorldPosition() ) );
			 }
			 for ( int32 i = 0; i < Object->GetNumComponents(); ++i )
			 {
				 if ( const CComponent* Component = Object->GetComponentAt( i ) ) Context.Print( "  component: " + Component->GetTypeName() );
			 }
			 return true;
		 },
		 { { "id", "The object id shown by 'objects' (the number after #)." } }, CategoryObjects );

	Add( "destroy", "Destroy an object (and its children)",
		 []( FConsoleContext& Context, int64 Id )
		 {
			 const TSharedPtr<CGameObject> Object = FindObjectByIdSafe( Id );
			 if ( !Object.IsValid() )
			 {
				 Context.Error( FString::Printf( "no object with id %lld in this scene (see 'objects')", static_cast<long long>( Id ) ) );
				 return false;
			 }
			 Context.Print( "destroying " + Object->GetDebugName() );
			 Object->Destroy();
			 return true;
		 },
		 { { "id", "The object id shown by 'objects'." } }, CategoryObjects );

	Add( "visible", "Show or hide an object (its Draw is skipped while hidden)",
		 []( FConsoleContext& Context, int64 Id, TOptional<bool> On )
		 {
			 const TSharedPtr<CGameObject> Object = FindObjectByIdSafe( Id );
			 if ( !Object.IsValid() )
			 {
				 Context.Error( FString::Printf( "no object with id %lld in this scene (see 'objects')", static_cast<long long>( Id ) ) );
				 return false;
			 }
			 const bool bShow = On.IsSet() ? On.GetValue() : !Object->IsVisible();
			 Object->SetVisible( bShow );
			 Context.Print( Object->GetDebugName() + ( bShow ? " is visible" : " is hidden" ) );
			 return true;
		 },
		 { { "id", "The object id shown by 'objects'." }, { "on", "true to show, false to hide. Without it, switches." } }, CategoryObjects );

	Add( "tick", "Turn the Tick of an object on or off",
		 []( FConsoleContext& Context, int64 Id, TOptional<bool> On )
		 {
			 const TSharedPtr<CGameObject> Object = FindObjectByIdSafe( Id );
			 if ( !Object.IsValid() )
			 {
				 Context.Error( FString::Printf( "no object with id %lld in this scene (see 'objects')", static_cast<long long>( Id ) ) );
				 return false;
			 }
			 const bool bEnable = On.IsSet() ? On.GetValue() : !Object->IsTickEnabled();
			 Object->SetTickEnabled( bEnable );
			 Context.Print( Object->GetDebugName() + ( bEnable ? ": tick on" : ": tick off" ) );
			 return true;
		 },
		 { { "id", "The object id shown by 'objects'." }, { "on", "true to turn on, false to turn off. Without it, switches." } }, CategoryObjects );

	Add( "teleport", "Move an object to a world position",
		 []( FConsoleContext& Context, int64 Id, const FVector3& Position )
		 {
			 const TSharedPtr<CGameObject> Object = FindObjectByIdSafe( Id );
			 CTransformComponent* Transform = Object.IsValid() ? Object->GetComponent<CTransformComponent>() : nullptr;
			 if ( Transform == nullptr )
			 {
				 Context.Error( FString::Printf( "object %lld does not exist or has no transform (see 'objects')", static_cast<long long>( Id ) ) );
				 return false;
			 }
			 Transform->SetWorldPosition( Position );
			 Context.Print( Object->GetDebugName() + " -> " + Vec3Text( Transform->GetWorldPosition() ) );
			 return true;
		 },
		 { { "id", "The object id shown by 'objects'." }, { "position", "World position: three numbers, x y z." } }, CategoryObjects );

	Add( "ecs", "Show the ECS statistics of the current scene (entities and systems)",
		 []()
		 {
			 CScene* Scene = FindCurrentScene();
			 if ( Scene == nullptr ) return FString( "no scene is running" );
			 const NEcs::FSystemScheduler& Systems = Scene->GetSystems();
			 return FString::Printf( "scene %s: %d entit(ies), %d object(s), %d system(s) (update stages: %d pre, %d post; draw: %d)", *Scene->GetName(), Scene->GetEcsWorld().GetNumEntities(), Scene->GetNumObjects(),
									 Systems.GetNumSystems(), Systems.GetNumStages( NEcs::EPhase::PreUpdate ), Systems.GetNumStages( NEcs::EPhase::PostUpdate ), Systems.GetNumStages( NEcs::EPhase::Draw ) );
		 },
		 {}, CategoryEcs );

	Add( "systems", "List the ECS systems of the current scene (phase and parallel stage)",
		 []( FConsoleContext& Context )
		 {
			 CScene* Scene = FindCurrentScene();
			 if ( Scene == nullptr ) return false;
			 const NEcs::FSystemScheduler& Systems = Scene->GetSystems();
			 const char* Phases[] = { "PreUpdate", "PostUpdate", "Draw" };
			 for ( int32 i = 0; i < Systems.GetNumSystems(); ++i )
			 {
				 Context.Print( FString::Printf( "  %-11s stage %d  %s", Phases[static_cast<int32>( Systems.GetSystemPhase( i ) )], Systems.GetSystemStage( i ), Systems.GetSystemName( i ) ) );
			 }
			 Context.Print( FString::Printf( "%d system(s)", Systems.GetNumSystems() ) );
			 return true;
		 },
		 {}, CategoryEcs );

	//-------------------------------------------------------------------------
	// Assets
	//-------------------------------------------------------------------------
	Add( "assets", "Show the asset statistics: counts, memory, load times, video memory",
		 []()
		 {
			 CAssetManager* Assets = GetAppSubsystem<CAssetManager>();
			 return Assets != nullptr ? DescribeAssets( *Assets ) : FString( NotRunning );
		 },
		 {}, CategoryAssets );

	Add( "assets.list", "List the loaded assets (type, path, references, memory, load time)",
		 []( FConsoleContext& Context, TOptional<FString> Filter )
		 {
			 CAssetManager* Assets = GetAppSubsystem<CAssetManager>();
			 if ( Assets == nullptr ) return false;
			 TArray<FAssetInfo> Infos = Assets->GetLoadedAssets();
			 Infos.StableSort( []( const FAssetInfo& A, const FAssetInfo& B ) { return A.MemoryBytes > B.MemoryBytes; } );
			 int32 Count = 0;
			 for ( const FAssetInfo& Info : Infos )
			 {
				 if ( Filter.IsSet() && !Info.Path.Contains( Filter.GetValue(), ESearchCase::IgnoreCase ) && !FString( ToString( Info.Type ) ).Contains( Filter.GetValue(), ESearchCase::IgnoreCase ) ) continue;
				 Context.Print( FString::Printf( "  %-14s %-10s refs %-3d load %6.1f ms  %s%s", ToString( Info.Type ), *FormatBytes( Info.MemoryBytes ), Info.RefCount, Info.LoadMs, *Info.Path, Info.bReady ? "" : "  (loading)" ) );
				 ++Count;
			 }
			 Context.Print( FString::Printf( "%d asset(s)", Count ) );
			 return true;
		 },
		 { { "filter", "Only the assets whose path or type contains this text (for example Texture)." } }, CategoryAssets );

	Add( "assets.dump", "Write the asset list and statistics to the log",
		 []( FConsoleContext& Context )
		 {
			 CAssetManager* Assets = GetAppSubsystem<CAssetManager>();
			 if ( Assets == nullptr ) return false;
			 Assets->DumpToLog();
			 Context.Print( "written to the log (category LogAsset)" );
			 return true;
		 },
		 {}, CategoryAssets );

	Add( "assets.release", "Forget the records of assets nobody uses any more",
		 []( FConsoleContext& Context )
		 {
			 CAssetManager* Assets = GetAppSubsystem<CAssetManager>();
			 if ( Assets == nullptr ) return false;
			 Assets->ReleaseUnused();
			 Context.Print( "done" );
			 return true;
		 },
		 {}, CategoryAssets );

	Add( "assets.root", "Show the asset root folder",
		 []()
		 {
			 CAssetManager* Assets = GetAppSubsystem<CAssetManager>();
			 return Assets != nullptr ? Assets->GetRootDirectory() : FString( NotRunning );
		 },
		 {}, CategoryAssets );

	Add( "assets.export", "Convert the models under the asset root (.fbx .obj .x .pmx ...) to .mv1",
		 []( FConsoleContext& Context, TOptional<bool> Force )
		 {
			 CAssetManager* Assets = GetAppSubsystem<CAssetManager>();
			 if ( Assets == nullptr ) return false;
			 const FModelBatchExportResult Batch = Assets->ExportAllModels( Force.IsSet() && Force.GetValue() );
			 for ( const FModelExportResult& Item : Batch.Items )
			 {
				 if ( Item.bSuccess ) Context.Print( "  " + Item.GetSummary() );
				 else Context.Error( "  " + Item.GetSummary() );
			 }
			 Context.Print( Batch.GetSummary() );
			 return Batch.NumFailed == 0;
		 },
		 { { "force", "true to convert everything again. By default only the missing or outdated .mv1 files are made." } }, CategoryAssets );

	//-------------------------------------------------------------------------
	// Audio
	//-------------------------------------------------------------------------
	Add( "audio", "Show the audio status: voices, pause, listener and the bus volumes",
		 []( FConsoleContext& Context )
		 {
			 CAudioSystem* Audio = GetAppSubsystem<CAudioSystem>();
			 if ( Audio == nullptr ) return false;
			 const FAudioListener& Listener = Audio->GetListener();
			 Context.Print( FString::Printf( "%d voice(s)%s, listener at %s facing %s", Audio->GetNumVoices(), Audio->IsPaused() ? " (paused)" : "", *Vec3Text( Listener.Position ), *Vec3Text( Listener.Forward ) ) );
			 for ( int32 i = 0; i < static_cast<int32>( EAudioBus::Count ); ++i )
			 {
				 const EAudioBus Bus = static_cast<EAudioBus>( i );
				 Context.Print( FString::Printf( "  %-8s volume %s%s", ToString( Bus ), *NConsole::FormatFloat( Audio->GetBusVolume( Bus ) ), Audio->IsBusMuted( Bus ) ? "  (muted)" : "" ) );
			 }
			 return true;
		 },
		 {}, CategoryAudio );

	Add( "audio.voices", "List the sounds that are playing",
		 []( FConsoleContext& Context )
		 {
			 CAudioSystem* Audio = GetAppSubsystem<CAudioSystem>();
			 if ( Audio == nullptr ) return false;
			 const TArray<FVoiceInfo> Voices = Audio->GetVoiceInfos();
			 for ( const FVoiceInfo& Voice : Voices )
			 {
				 Context.Print( FString::Printf( "  %-8s vol %.2f pan %+.2f pitch %.2f age %5.1f s %s%s%s%s  %s", ToString( Voice.Bus ), Voice.Volume, Voice.Pan, Voice.Pitch, Voice.Age, Voice.bLooping ? "loop " : "",
												 Voice.bPaused ? "paused " : "", Voice.bFading ? "fading " : "", Voice.bSpatial ? "3D" : "", *Voice.SoundPath ) );
			 }
			 Context.Print( FString::Printf( "%d voice(s)", Voices.Num() ) );
			 return true;
		 },
		 {}, CategoryAudio );

	Add( "audio.volume", "Show or set the volume of an audio bus",
		 []( FConsoleContext& Context, const FString& BusName, TOptional<float32> Volume )
		 {
			 CAudioSystem* Audio = GetAppSubsystem<CAudioSystem>();
			 EAudioBus Bus = EAudioBus::Master;
			 if ( Audio == nullptr ) return false;
			 if ( !ParseBus( BusName, Bus ) )
			 {
				 Context.Error( "unknown bus '" + BusName + "' (" + FString::Join( GetBusNames(), ", " ) + ")" );
				 return false;
			 }
			 if ( Volume.IsSet() ) Audio->SetBusVolume( Bus, NMath::Max( Volume.GetValue(), 0.0f ) );
			 Context.Print( FString( ToString( Bus ) ) + " volume " + NConsole::FormatFloat( Audio->GetBusVolume( Bus ) ) );
			 return true;
		 },
		 { { "bus", "Master, Music, Sfx, Ambient, Voice or Ui." }, { "volume", "0 = silent, 1 = normal. Without it, shows the current volume." } }, CategoryAudio );
	Registry.SetArgChoices( "audio.volume", 0, GetBusNames() );

	Add( "audio.mute", "Mute or unmute an audio bus",
		 []( FConsoleContext& Context, const FString& BusName, TOptional<bool> On )
		 {
			 CAudioSystem* Audio = GetAppSubsystem<CAudioSystem>();
			 EAudioBus Bus = EAudioBus::Master;
			 if ( Audio == nullptr ) return false;
			 if ( !ParseBus( BusName, Bus ) )
			 {
				 Context.Error( "unknown bus '" + BusName + "' (" + FString::Join( GetBusNames(), ", " ) + ")" );
				 return false;
			 }
			 const bool bMute = On.IsSet() ? On.GetValue() : !Audio->IsBusMuted( Bus );
			 Audio->SetBusMuted( Bus, bMute );
			 Context.Print( FString( ToString( Bus ) ) + ( bMute ? " muted" : " unmuted" ) );
			 return true;
		 },
		 { { "bus", "Master, Music, Sfx, Ambient, Voice or Ui." }, { "on", "true to mute, false to unmute. Without it, switches." } }, CategoryAudio );
	Registry.SetArgChoices( "audio.mute", 0, GetBusNames() );

	Add( "audio.pause", "Pause or resume all sounds",
		 []( FConsoleContext& Context, TOptional<bool> On )
		 {
			 CAudioSystem* Audio = GetAppSubsystem<CAudioSystem>();
			 if ( Audio == nullptr ) return false;
			 const bool bPause = On.IsSet() ? On.GetValue() : !Audio->IsPaused();
			 Audio->SetPaused( bPause );
			 Context.Print( bPause ? "audio paused" : "audio resumed" );
			 return true;
		 },
		 { { "on", "true to pause, false to resume. Without it, switches." } }, CategoryAudio );

	Add( "audio.stop", "Stop all sounds",
		 []( FConsoleContext& Context, TOptional<float32> Fade )
		 {
			 CAudioSystem* Audio = GetAppSubsystem<CAudioSystem>();
			 if ( Audio == nullptr ) return false;
			 Audio->StopAll( Fade.IsSet() ? NMath::Max( Fade.GetValue(), 0.0f ) : 0.0f );
			 Context.Print( "stopped" );
			 return true;
		 },
		 { { "fade", "Fade-out time in seconds (default 0)." } }, CategoryAudio );

	Add( "audio.play", "Play a sound file once (or in a loop) to try it",
		 []( FConsoleContext& Context, const FString& Path, TOptional<FString> BusName, TOptional<float32> Volume, TOptional<float32> Pitch, TOptional<bool> Loop )
		 {
			 CAudioSystem* Audio = GetAppSubsystem<CAudioSystem>();
			 CAssetManager* Assets = GetAppSubsystem<CAssetManager>();
			 if ( Audio == nullptr || Assets == nullptr ) return false;
			 FSoundPlayParams Params;
			 if ( BusName.IsSet() && !ParseBus( BusName.GetValue(), Params.Bus ) )
			 {
				 Context.Error( "unknown bus '" + BusName.GetValue() + "' (" + FString::Join( GetBusNames(), ", " ) + ")" );
				 return false;
			 }
			 if ( Volume.IsSet() ) Params.Volume = Volume.GetValue();
			 if ( Pitch.IsSet() ) Params.Pitch = Pitch.GetValue();
			 Params.bLoop = Loop.IsSet() && Loop.GetValue();
			 const TSharedPtr<CSound> Sound = Assets->LoadSound( Path );
			 if ( !Sound.IsValid() )
			 {
				 Context.Error( "could not load the sound '" + Path + "' (see the log)" );
				 return false;
			 }
			 const FSoundHandle Handle = Audio->Play( Sound, Params );
			 Context.Print( Audio->IsPlaying( Handle ) ? "playing " + Path : "could not start " + Path + " (too many sounds?)" );
			 return Audio->IsPlaying( Handle );
		 },
		 { { "file", "Sound file path, relative to the asset root." }, { "bus", "Which bus to play on (default Sfx)." }, { "volume", "Volume (default 1)." }, { "pitch", "Pitch multiplier (default 1)." }, { "loop", "true to loop (stop it with audio.stop)." } },
		 CategoryAudio );
	Registry.SetArgChoices( "audio.play", 1, GetBusNames() );

	//-------------------------------------------------------------------------
	// Profiler
	//-------------------------------------------------------------------------
	Add( "profiler", "Turn the profiler on or off, or show whether it is recording",
		 []( FConsoleContext& Context, TOptional<bool> On )
		 {
			 IProfiler* Profiler = GetAppSubsystem<IProfiler>();
			 if ( Profiler == nullptr ) return false;
			 if ( On.IsSet() ) Profiler->SetEnabled( On.GetValue() );
			 Context.Print( Profiler->IsEnabled() ? "profiler: recording" : "profiler: off" );
			 return true;
		 },
		 { { "on", "true to start recording, false to stop. Without it, shows the state." } }, CategoryProfiler );

	Add( "profiler.capture", "Record a number of frames, then stop",
		 []( FConsoleContext& Context, int32 Frames )
		 {
			 IProfiler* Profiler = GetAppSubsystem<IProfiler>();
			 if ( Profiler == nullptr ) return false;
			 if ( Frames < 1 )
			 {
				 Context.Error( "frames must be 1 or more" );
				 return false;
			 }
			 Profiler->CaptureFrames( Frames );
			 Context.Print( FString::Printf( "recording %d frame(s)", Frames ) );
			 return true;
		 },
		 { { "frames", "How many frames to record." } }, CategoryProfiler );

	Add( "profiler.clear", "Throw away the recorded profiler frames",
		 []( FConsoleContext& Context )
		 {
			 IProfiler* Profiler = GetAppSubsystem<IProfiler>();
			 if ( Profiler == nullptr ) return false;
			 Profiler->Clear();
			 Context.Print( "cleared" );
			 return true;
		 },
		 {}, CategoryProfiler );

	Add( "profiler.stats", "Show how much the profiler has recorded",
		 []()
		 {
			 IProfiler* Profiler = GetAppSubsystem<IProfiler>();
			 if ( Profiler == nullptr ) return FString( NotRunning );
			 return FString::Printf( "%s, %d frame(s), %lld sample(s), %s stored (limit %d MB%s), overhead ~%.0f ns per sample", Profiler->IsEnabled() ? "recording" : "off", Profiler->GetNumFrames(),
									 static_cast<long long>( Profiler->GetNumStoredSamples() ), *FormatBytes( Profiler->GetStoredBytes() ), Profiler->GetMemoryLimitMB(), Profiler->IsMemoryLimitReached() ? ", REACHED" : "",
									 Profiler->GetOverheadNsPerSample() );
		 },
		 {}, CategoryProfiler );

	Add( "profiler.save", "Save the recorded frames as a Chrome trace (open it at ui.perfetto.dev)",
		 []( FConsoleContext& Context, TOptional<FString> Path )
		 {
			 IProfiler* Profiler = GetAppSubsystem<IProfiler>();
			 if ( Profiler == nullptr ) return false;
			 if ( Profiler->GetNumFrames() == 0 )
			 {
				 Context.Error( "nothing is recorded (use 'profiler on' first)" );
				 return false;
			 }
			 FString Target = Path.IsSet() && !Path.GetValue().IsEmpty() ? Path.GetValue() : NProfilerExport::MakeDefaultTracePath();
			 Target = Target.Replace( "\\", "/" );
			 const int32 Slash = Target.FindLast( "/" );
			 if ( Slash > 0 ) NFileSystem::EnsureDirectory( Target.Left( Slash ) );
			 if ( !NProfilerExport::WriteChromeTrace( *Profiler, 0, -1, Target ) )
			 {
				 Context.Error( "could not write " + Target );
				 return false;
			 }
			 Context.Print( "saved " + Target );
			 return true;
		 },
		 { { "path", "Output .json file. Default: Saved/Profiler/<project>_<date and time>.json" } }, CategoryProfiler );

	//-------------------------------------------------------------------------
	// Log
	//-------------------------------------------------------------------------
	Add( "log", "Show or change the log level of the log categories",
		 []( FConsoleContext& Context, TOptional<FString> Category, TOptional<FString> Level )
		 {
			 CLogManager& Manager = CLogManager::Get();
			 if ( !Category.IsSet() )
			 {
				 TArray<FLogCategory*> Categories = Manager.GetCategories();
				 Categories.StableSort( []( const FLogCategory* A, const FLogCategory* B ) { return FString( A->GetName() ) < FString( B->GetName() ); } );
				 for ( const FLogCategory* Item : Categories )
				 {
					 Context.Print( FString::Printf( "  %-24s %-12s%s", Item->GetName(), LogVerbosityToString( Item->GetVerbosity() ), Item->GetVerbosity() != Item->GetDefaultVerbosity() ? *FString::Printf( "(default %s)", LogVerbosityToString( Item->GetDefaultVerbosity() ) ) : "" ) );
				 }
				 return true;
			 }
			 if ( Category.GetValue().Equals( "reset", ESearchCase::IgnoreCase ) )
			 {
				 Manager.ResetAllCategoriesToDefault();
				 Context.Print( "all log levels are back to their defaults" );
				 return true;
			 }
			 ELogVerbosity Verbosity = ELogVerbosity::Log;
			 if ( Level.IsSet() && !LogVerbosityFromString( *Level.GetValue(), Verbosity ) )
			 {
				 Context.Error( "unknown level '" + Level.GetValue() + "' (" + FString::Join( GetVerbosityNames(), ", " ) + ")" );
				 return false;
			 }
			 if ( Category.GetValue().Equals( "all", ESearchCase::IgnoreCase ) )
			 {
				 if ( !Level.IsSet() )
				 {
					 Context.Error( "give a level: log all <level>" );
					 return false;
				 }
				 Manager.SetAllCategoriesVerbosity( Verbosity );
				 Context.Print( FString( "all log categories: " ) + LogVerbosityToString( Verbosity ) );
				 return true;
			 }
			 FLogCategory* Found = FindLogCategory( Category.GetValue() );
			 if ( Found == nullptr )
			 {
				 Context.Error( "no log category named '" + Category.GetValue() + "' (see 'log')" );
				 return false;
			 }
			 if ( Level.IsSet() ) Found->SetVerbosity( Verbosity );
			 Context.Print( FString( Found->GetName() ) + ": " + LogVerbosityToString( Found->GetVerbosity() ) );
			 return true;
		 },
		 { { "category", "A log category name (for example LogAsset), 'all', or 'reset' to restore every default. Without it, lists the categories." },
		   { "level", "NoLogging, Fatal, Error, Warning, Display, Log, Verbose or VeryVerbose. Without it, shows the level." } },
		 CategoryLog );
	Registry.SetArgCompleter( "log", 0, []()
	{
		TArray<FString> Names = GetLogCategoryNames();
		Names.Add( "all" );
		Names.Add( "reset" );
		return Names;
	} );
	Registry.SetArgChoices( "log", 1, GetVerbosityNames() );

	//-------------------------------------------------------------------------
	// Info
	//-------------------------------------------------------------------------
	Add( "version", "Show the build configuration, the compiler and the build date",
		 []()
		 {
#if defined( _DEBUG )
			 const char* Configuration = "Debug";
#else
			 const char* Configuration = "Release";
#endif
			 return FString::Printf( "%s build, MSVC %d, built %s %s, debug console %s", Configuration, static_cast<int>( _MSC_VER ), __DATE__, __TIME__, COG_DEBUG_CONSOLE ? "enabled" : "disabled" );
		 },
		 {}, CategoryInfo );

	Add( "cmdline", "Show the command line the application was started with",
		 []() { return FString( GetCommandLineA() ); }, {}, CategoryInfo );

	Add( "cwd", "Show the working folder, the executable folder and the project root",
		 []( FConsoleContext& Context )
		 {
			 Context.Print( "working folder:    " + NFileSystem::GetWorkingDirectory() );
			 Context.Print( "executable folder: " + NFileSystem::GetExecutableDirectory() );
			 Context.Print( "project root:      " + NFileSystem::FindProjectRoot() );
		 },
		 {}, CategoryInfo );

	Add( "date", "Show the current local date and time",
		 []()
		 {
			 const FFileTime Now = NFileSystem::GetCurrentLocalTime();
			 return FString::Printf( "%04d-%02d-%02d %02d:%02d:%02d", Now.Year, Now.Month, Now.Day, Now.Hour, Now.Minute, Now.Second );
		 },
		 {}, CategoryInfo );
}
