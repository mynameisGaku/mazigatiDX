// SPDX-License-Identifier: MIT
#include "BuiltinCommandHelpers.h"

namespace NConsole::NBuiltin
{
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
		// UTF-8 として正しければそのまま読む。そうでなければ、昔の Shift-JIS のスクリプトとして UTF-8 に直して読む
		OutText = Text.IsValidUtf8() ? Text : FString::FromCodePage( Text.GetData(), Text.Len(), 932 );
		return true;
	}

	// パスのフォルダの部分（最後の区切りの手前。区切りが無ければ空）
	FString GetPathDirectory( const FString& Path )
	{
		const int32 LastSeparator = NMath::Max( Path.FindLast( "/" ), Path.FindLast( "\\" ) );
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
	FString DescribeAssets( CAssetSubsystem& Assets )
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
}
