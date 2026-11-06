#include "EditorApplication.h"

#include "Library/Core/Log/Log.h"
#include "Library/Framework/Asset/AssetSubsystem.h"
#include "Library/Framework/Engine/Engine.h"
#include "Library/Framework/Scene/SceneManager.h"
#include "Library/Platform/System/CommandLine.h"
#include "Source/Scene/EditorScene.h"
#include "VendorModules/ImGui/imgui.h"

DEFINE_LOG_CATEGORY_STATIC( LogEditor, Log )

void CEditorApplication::OnInit()
{
	// 編集するゲームのアセットのルートフォルダー
	if ( const TOptional<FString> Root = NCommandLine::GetValue( "assets" ) )
	{
		if ( CAssetSubsystem* Assets = GetAppSubsystem<CAssetSubsystem>() )
		{
			Assets->SetRootDirectory( Root.GetValue() );
		}
	}
	if ( CAssetSubsystem* Assets = GetAppSubsystem<CAssetSubsystem>() )
	{
		LOG( LogEditor, Log, "Editor: asset folder '%s'", *Assets->GetRootDirectory() );
	}

	Overlay.OnDrawWindows = [this]()
	{
		DrawMenuBar();
		NDebugUI::DrawAssetTools( Tools );
	};
	Overlay.Init();
	Overlay.SetDebugWindowVisible( false );
	Overlay.SetStatusLineVisible( false );

	// 起動引数で何も開かないときは、プロジェクトのブラウザー（アセットの一覧）を開く
	const bool bToolRequested = NCommandLine::GetValue( "tool" ).IsSet() || NCommandLine::GetValue( "ai-file" ).IsSet() || NCommandLine::GetValue( "world" ).IsSet() || NCommandLine::GetValue( "open" ).IsSet();
	Tools.bShowBrowser = !bToolRequested;

	GetSceneManager().RegisterScene<CEditorScene>( "Editor" );
	GetSceneManager().ChangeScene( "Editor" );
}

void CEditorApplication::DrawMenuBar()
{
	if ( !ImGui::BeginMainMenuBar() )
	{
		return;
	}
	if ( ImGui::BeginMenu( "Tools" ) )
	{
		NDebugUI::DrawAssetToolsMenuItems( Tools );
		ImGui::EndMenu();
	}
	if ( CAssetSubsystem* Assets = GetAppSubsystem<CAssetSubsystem>() )
	{
		ImGui::TextDisabled( "  assets: %s", *Assets->GetRootDirectory() );
	}
	ImGui::EndMainMenuBar();
}

void CEditorApplication::OnTick( float32 DeltaTime )
{
	Overlay.Tick();
}

void CEditorApplication::OnDraw()
{
	Overlay.Draw( *this );
}

void CEditorApplication::OnTerm()
{
	NDebugUI::ShutdownAssetTools( Tools );
	Overlay.Term();
}
