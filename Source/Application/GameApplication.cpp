#include "GameApplication.h"

#include "Library/Core/Interfaces/Platform/IPlatform.h"
#include "Library/Core/Log/Log.h"
#include "Library/Core/Profiler/ProfilerScope.h"
#include "Library/Core/Types/CString.h"
#include "Library/Framework/Asset/AssetManager.h"
#include "Library/Framework/Engine/Engine.h"
#include "Library/Framework/World/WorldSpawner.h"
#include "Library/Physics/WorldPhysics.h"
#include "Library/Platform/CommandLine.h"
#include "Source/Application/StartupHook.h"
#include "Source/Scene/GameScene.h"
#include "Source/Scene/TitleScene.h"

DEFINE_LOG_CATEGORY_STATIC( LogGame, Log )

void CGameApplication::OnInit()
{
	// デバッグ表示の窓に、アセットの画面・エディタを並べる。ログの窓とコンソールの準備は、Init で行う
	Overlay.OnDrawPanelItems = [this]() { NDebugUI::DrawAssetToolsButtons( AssetTools ); };
	Overlay.OnDrawWindows = [this]()
	{
		PROFILE_SCOPE_OWNER( "Debug", "Game", "AssetTools" );
		NDebugUI::DrawAssetTools( AssetTools );
	};
	Overlay.Init();

	// ワールド（.world）の Collider を、物理の剛体にできるようにする（Framework は Physics を知らないので、Physics 側の設置を登録する）。
	// 物理のあるシーン（IPhysicsScene）に置くときだけ、剛体になる
	NPhysicsWorld::RegisterSpawners( FWorldSpawnerRegistry::Global() );

	CSceneManager& Scenes = GetSceneManager();
	Scenes.RegisterScene<CTitleScene>( "Title" );
	Scenes.RegisterScene<CGameScene>( "Game" );
	// 起動時の窓口（自己確認など、別のリポジトリのものが差し込む）。--selftest=ecs / assets / world で起動したときは、そのシーンから始まる
	if ( !NStartupHook::Run( Scenes ) ) Scenes.ChangeScene( "Title" );

	// --export-models（.mv1 が無い・古いものだけ）/ --export-models=force（すべて作り直す）: アセットのルートフォルダの下の
	// .fbx / .obj / .x / .pmx などを .mv1 にして、そのまま終了する（アセット出力の自動化用。結果はログに出る）
	const FString CommandLine = NCommandLine::Get();
	if ( NCString::Find( *CommandLine, "--export-models" ) != nullptr )
	{
		if ( CAssetManager* Assets = GetAppSubsystem<CAssetManager>() )
		{
			const FModelBatchExportResult Batch = Assets->ExportAllModels( NCString::Find( *CommandLine, "--export-models=force" ) != nullptr );
			LOG( LogGame, Log, "Exported the models in '%s' as .mv1: %s", *Assets->GetRootDirectory(), *Batch.GetSummary() );
		}
		if ( IPlatform* Platform = GetAppSubsystem<IPlatform>() ) Platform->RequestExit();
	}

	LOG( LogGame, Log, "Game started" );
}

void CGameApplication::OnTick( float32 DeltaTime )
{
	Overlay.Tick();
}

void CGameApplication::OnDraw()
{
	Overlay.Draw( *this );
}

void CGameApplication::OnTerm()
{
	NDebugUI::ShutdownAssetTools( AssetTools );
	LOG( LogGame, Log, "Game finished" );
	Overlay.Term();
}
