// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Framework/Console/BuiltinCommands.h"
#include "Library/Framework/Capture/ScreenCaptureSubsystem.h"
#include "Library/Core/File/File.h"
#include "Library/Core/Interfaces/Job/IJobSystem.h"
#include "Library/Core/Interfaces/Platform/IPlatform.h"
#include "Library/Core/Interfaces/Profiler/IProfiler.h"
#include "Library/Core/Log/LogManager.h"
#include "Library/Core/Log/LogVerbosity.h"
#include "Library/Core/Types/Numeric.h"
#include "Library/Core/Types/Utility.h"
#include "Library/Framework/AI/BehaviorTreeComponent.h"
#include "Library/Framework/AI/Registry/AIPartsCatalog.h"
#include "Library/Framework/AI/StateMachineComponent.h"
#include "Library/Framework/Asset/AssetSubsystem.h"
#include "Library/Framework/Audio/AudioSubsystem.h"
#include "Library/Framework/Component/TransformComponent.h"
#include "Library/Framework/Console/ConsoleParse.h"
#include "Library/Framework/DebugView/DebugViews.h"
#include "Library/Framework/Engine/Engine.h"
#include "Library/Framework/Profiler/ProfilerExport.h"
#include "Library/Framework/Scene/Scene.h"
#include "Library/Math/MathUtils.h"
#include "Library/Platform/System/CommandLine.h"
#include "Library/Platform/System/FileSystem.h"

//=============================================================================
// NConsole::NBuiltin: 組み込みのコマンドの中身（分類ごとの登録と、共通の補助）
//   RegisterBuiltinCommands が、分類ごとの登録の関数を、順に呼ぶ。分類ごとに、1 ファイル:
//     BuiltinConsoleCommands.cpp     コンソールそのもの（help・clear・history・echo・exec・alias など）
//     BuiltinVariableCommands.cpp    変数（一覧・読む・書く）
//     BuiltinEngineCommands.cpp      エンジンとウィンドウ（終了・時間の進め方・フルスクリーン・撮影など）
//     BuiltinSceneCommands.cpp       シーン・オブジェクト・ECS
//     BuiltinAssetCommands.cpp       アセット
//     BuiltinAudioCommands.cpp       音
//     BuiltinProfilerCommands.cpp    プロファイラー
//     BuiltinDebugViewCommands.cpp   デバッグビュー（ゲームの上に重ねて描く表示）
//     BuiltinAICommands.cpp          AI（ステートマシン・ビヘイビアツリー）
//     BuiltinLogCommands.cpp         ログ
//     BuiltinInfoCommands.cpp        情報（バージョン・メモリなど）
//   組み込みのコマンドの実装（Builtin*.cpp）の中だけで使う。
//=============================================================================
namespace NConsole::NBuiltin
{
	// コマンドを登録して、残す（Keep）
	template<typename TFunction>
	void AddCommand( FConsoleRegistry& Registry, const char* Name, const char* Description, TFunction&& Function, const TArray<FConsoleArgSpec>& Args, const char* Category )
	{
		Registry.Add( Name, Description, Forward<TFunction>( Function ), Args, Category ).Keep();
	}

	// 分類ごとの登録
	void RegisterConsoleCommands( FConsoleRegistry& Registry );
	void RegisterVariableCommands( FConsoleRegistry& Registry );
	void RegisterEngineCommands( FConsoleRegistry& Registry );
	void RegisterSceneCommands( FConsoleRegistry& Registry );
	void RegisterAssetCommands( FConsoleRegistry& Registry );
	void RegisterAudioCommands( FConsoleRegistry& Registry );
	void RegisterProfilerCommands( FConsoleRegistry& Registry );
	void RegisterDebugViewCommands( FConsoleRegistry& Registry );
	void RegisterAICommands( FConsoleRegistry& Registry );
	void RegisterLogCommands( FConsoleRegistry& Registry );
	void RegisterInfoCommands( FConsoleRegistry& Registry );

	inline constexpr const char* CategoryConsole = "Console";
	inline constexpr const char* CategoryVariables = "Variables";
	inline constexpr const char* CategoryEngine = "Engine";
	inline constexpr const char* CategoryWindow = "Window";
	inline constexpr const char* CategoryScene = "Scene";
	inline constexpr const char* CategoryObjects = "Objects";
	inline constexpr const char* CategoryAI = "AI";
	inline constexpr const char* CategoryEcs = "ECS";
	inline constexpr const char* CategoryAssets = "Assets";
	inline constexpr const char* CategoryAudio = "Audio";
	inline constexpr const char* CategoryProfiler = "Profiler";
	inline constexpr const char* CategoryViews = "Debug views";
	inline constexpr const char* CategoryLog = "Log";
	inline constexpr const char* CategoryInfo = "Info";

	FString FormatBytes( int64 Bytes );

	FString Pad( const FString& Text, int32 Width );

	FString Vec3Text( const FVector3& V );

	CSceneManager* FindSceneManager();

	CScene* FindCurrentScene();

	// 今のシーンのオブジェクトを Id で探す（無ければ空）
	TSharedPtr<CGameObject> FindObjectByIdSafe( int64 Id );

	TArray<FString> GetSceneNames();

	TArray<FString> GetBusNames();

	bool ParseBus( const FString& Text, EAudioBus& OutBus );

	TArray<FString> GetVerbosityNames();

	TArray<FString> GetLogCategoryNames();

	FLogCategory* FindLogCategory( const FString& Name );

	bool ReadTextFile( const FString& Path, FString& OutText );

	// パスのフォルダの部分（最後の区切りの手前。区切りが無ければ空）
	FString GetPathDirectory( const FString& Path );

	bool IsAbsolutePath( const FString& Path );

	// 実行中のスクリプトのフォルダ（入れ子の exec が、読み込み元のフォルダを基準に探すため）
	extern FString GScriptDirectory;

	// 引数（コマンド名を除く）の Start 番目から後ろを、引用符つきでつないだ 1 行にする（repeat / alias が、中のコマンドをそのまま渡す用）
	FString JoinQuoted( const TArray<FString>& Args, int32 Start );

	//-------------------------------------------------------------------------
	// help
	//-------------------------------------------------------------------------
	void PrintCommandDetails( FConsoleContext& Context, const FConsoleCommandInfo& Info );

	bool PrintHelp( FConsoleContext& Context, const TOptional<FString>& Name );

	//-------------------------------------------------------------------------
	// assets
	//-------------------------------------------------------------------------
	FString DescribeAssets( CAssetSubsystem& Assets );

	inline constexpr const char* NotRunning = "not available (the engine or the subsystem is not running)";
}
