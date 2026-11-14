#include "SpriteSheetEditor.h"
#include "SpriteSheetEditorInternal.h"
#include "Library/DebugUI/Common/EditorLog.h"


#include "Library/Core/Collection/Sort.h"
#include "Library/Core/Log/Log.h"
#include "Library/Core/Types/CString.h"
#include "Library/Core/Types/Numeric.h"
#include "Library/DebugUI/Common/EditorCommon.h"
#include "Library/Framework/Asset/AssetSubsystem.h"
#include "Library/Framework/Engine/Engine.h"
#include "Library/Platform/System/FileSystem.h"

using namespace NSpriteSheetEditorUI;

namespace NSpriteSheetEditorUI
{
	void SetStatus( FState& S, const FString& Text, bool bError )
	{
		S.StatusMessage = Text;
		S.bStatusIsError = bError;
		if ( bError ) LOG( LogEditorUI, Warning, "%s", *Text );
	}

	//=========================================================================
	// ファイルの一覧
	//=========================================================================
	void RescanFiles( FState& S, bool bForce )
	{
		CAssetSubsystem* Assets = GetAssets();
		if ( Assets == nullptr ) return;
		const double Now = ImGui::GetTime();
		if ( !bForce && Now - S.LastScanTime < 3.0 ) return;
		S.LastScanTime = Now;

		const FString Root = Assets->GetRootDirectory();
		S.TextureFiles = NFileSystem::ListFilesRecursive( Root, { ".png", ".jpg", ".jpeg", ".bmp", ".tga", ".dds" } );
		S.SheetFiles = NFileSystem::ListFilesRecursive( Root, { SheetExtension } );
	}

	void RestartPreview( FState& S, bool bKeepTime )
	{
		const float32 Normalized = bKeepTime ? S.Preview.GetNormalizedTime() : 0.0f;
		S.Preview.Sheet = S.Sheet;
		S.PreviewClip = S.SelectedClip;
		const FSpriteClip* Clip = S.Sheet.IsValid() ? S.Sheet->GetClip( S.SelectedClip ) : nullptr;
		if ( Clip != nullptr && !Clip->Frames.IsEmpty() )
		{
			S.Preview.Speed = S.PreviewSpeed;
			S.Preview.PlayIndex( S.SelectedClip );
			if ( bKeepTime ) S.Preview.Seek( Normalized );
			if ( !S.bPlaying ) S.Preview.Pause();
		}
		else
		{
			S.Preview.ShowFrame( S.SelectedFrame >= 0 ? S.SelectedFrame : 0 );
		}
	}

	void SyncSheet( FState& S )
	{
		CAssetSubsystem* Assets = GetAssets();
		const FSpriteSheetDefinition& Definition = S.Document.Get();
		if ( Assets == nullptr ) return;

		if ( S.SheetTexturePath != Definition.TexturePath )
		{
			// 画像が変わった（または初めて）: 作り直す。読めなかったときも、同じパスで何度も試さない
			S.SheetTexturePath = Definition.TexturePath;
			S.Sheet = Definition.TexturePath.IsEmpty() ? TSharedPtr<CSpriteSheet>() : Assets->CreateSpriteSheet( Definition, "editor" );
			S.SheetRevision = S.Document.GetRevision();
			RestartPreview( S, false );
			return;
		}
		if ( S.SheetRevision != S.Document.GetRevision() && S.Sheet.IsValid() )
		{
			S.Sheet->SetDefinition( Definition );
			S.SheetRevision = S.Document.GetRevision();
			// 選択が範囲外になっていたら直す
			if ( !Definition.Clips.IsValidIndex( S.SelectedClip ) ) S.SelectedClip = Definition.Clips.IsEmpty() ? INDEX_NONE : Definition.Clips.Num() - 1;
			if ( !Definition.Grids.IsValidIndex( S.SelectedGrid ) ) S.SelectedGrid = Definition.Grids.IsEmpty() ? INDEX_NONE : Definition.Grids.Num() - 1;
			if ( S.SelectedFrame >= Definition.GetNumFrames() ) S.SelectedFrame = INDEX_NONE;
			const FSpriteClip* Clip = Definition.Clips.IsValidIndex( S.SelectedClip ) ? &Definition.Clips[S.SelectedClip] : nullptr;
			if ( Clip == nullptr || S.SelectedStep >= Clip->Frames.Num() ) S.SelectedStep = INDEX_NONE;
			if ( Clip == nullptr || S.SelectedEvent >= Clip->Events.Num() ) S.SelectedEvent = INDEX_NONE;
			RestartPreview( S, true );
		}
		if ( S.PreviewClip != S.SelectedClip ) RestartPreview( S, false );
	}

	void ResetSelection( FState& S )
	{
		S.SelectedGrid = S.Document.Get().Grids.IsEmpty() ? INDEX_NONE : 0;
		S.SelectedFrame = INDEX_NONE;
		S.SelectedClip = S.Document.Get().Clips.IsEmpty() ? INDEX_NONE : 0;
		S.SelectedStep = INDEX_NONE;
		S.SelectedEvent = INDEX_NONE;
		S.SheetTexturePath = "?";   // 必ず作り直す
		S.bFitRequested = true;
	}

	bool SaveCurrent( FState& S, const FString& AssetPath )
	{
		CAssetSubsystem* Assets = GetAssets();
		if ( Assets == nullptr || AssetPath.IsEmpty() ) return false;

		FString Path = NormalizePath( AssetPath );
		if ( !Path.EndsWith( SheetExtension, ESearchCase::IgnoreCase ) ) Path += SheetExtension;
		if ( !Assets->SaveSpriteSheetDefinition( Path, S.Document.Get() ) )
		{
			SetStatus( S, "Failed to save '" + Path + "' (see the log).", true );
			return false;
		}
		S.FilePath = Path;
		S.Document.MarkSaved();
		SetStatus( S, "Saved " + Path );
		RescanFiles( S, true );
		return true;
	}

	void RequestAction( FState& S, FState::EPending Action, const FString& Path )
	{
		if ( S.Document.IsDirty() )
		{
			S.Pending = Action;
			S.PendingPath = Path;
			S.bRequestOpenUnsavedPopup = true;
			return;
		}
		if ( Action == FState::EPending::Open ) NDebugUI::OpenSpriteSheetFile( S, Path );
		else if ( Action == FState::EPending::New ) NDebugUI::NewSpriteSheetFromTexture( S, Path );
	}
}

//=============================================================================
// 公開
//=============================================================================
bool NDebugUI::OpenSpriteSheetFile( FSpriteSheetEditorState& State, const FString& AssetPath )
{
	CAssetSubsystem* Assets = GetAssets();
	if ( Assets == nullptr ) return false;

	const TOptional<FString> Full = Assets->ResolvePath( AssetPath );
	if ( !Full.IsSet() )
	{
		SetStatus( State, "File not found: " + AssetPath, true );
		return false;
	}
	NJson::CJsonObject Json;
	FSpriteSheetDefinition Definition;
	if ( !Json.LoadFromFile( Full.Get( FString() ) ) || !FSpriteSheetDefinition::FromJson( Json.GetRoot(), Definition ) )
	{
		SetStatus( State, "Not a valid sprite sheet definition: " + AssetPath, true );
		return false;
	}

	State.Document.Reset( Definition );
	State.FilePath = NormalizePath( AssetPath );
	ResetSelection( State );
	SetStatus( State, "Opened " + State.FilePath );
	return true;
}

bool NDebugUI::NewSpriteSheetFromTexture( FSpriteSheetEditorState& State, const FString& TexturePath )
{
	FSpriteSheetDefinition Definition;
	Definition.TexturePath = NormalizePath( TexturePath );
	Definition.Grids.Add( FSpriteSheetGrid() );
	State.Document.Reset( Definition );
	State.FilePath.Empty();
	State.SaveAsPath.Empty();
	ResetSelection( State );
	SetStatus( State, "New sprite sheet for " + Definition.TexturePath + ". Set up the grid, add clips, then File > Save as." );
	return true;
}

void NDebugUI::DrawSpriteSheetEditor( FSpriteSheetEditorState& S, bool* pOpen )
{
	const ImGuiViewport* MainViewport = ImGui::GetMainViewport();
	ImGui::SetNextWindowPos( ImVec2( MainViewport->Pos.x + 16.0f, MainViewport->Pos.y + 38.0f ), ImGuiCond_FirstUseEver );
	ImGui::SetNextWindowSize( ImVec2( NMath::Min( 1320.0f, MainViewport->Size.x - 32.0f ), NMath::Min( 780.0f, MainViewport->Size.y - 50.0f ) ), ImGuiCond_FirstUseEver );
	char Title[256];
	NCString::Format( Title, sizeof( Title ), "Sprite Sheet Editor%s###SpriteSheetEditor", S.Document.IsDirty() ? " *" : "" );
	if ( !ImGui::Begin( Title, pOpen, ImGuiWindowFlags_MenuBar ) )
	{
		ImGui::End();
		return;
	}

	RescanFiles( S, false );
	SyncSheet( S );
	DrawMenuBar( S, pOpen );

	// ショートカット
	ImGuiIO& IO = ImGui::GetIO();
	if ( ImGui::IsWindowFocused( ImGuiFocusedFlags_RootAndChildWindows ) && !IO.WantTextInput )
	{
		if ( ImGui::IsKeyChordPressed( ImGuiMod_Ctrl | ImGuiKey_Z ) ) S.Document.Undo();
		if ( ImGui::IsKeyChordPressed( ImGuiMod_Ctrl | ImGuiKey_Y ) || ImGui::IsKeyChordPressed( ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_Z ) ) S.Document.Redo();
		if ( ImGui::IsKeyChordPressed( ImGuiMod_Ctrl | ImGuiKey_S ) && !S.FilePath.IsEmpty() ) SaveCurrent( S, S.FilePath );
	}

	const float FooterHeight = ImGui::GetFrameHeightWithSpacing() + 2.0f;
	const float LeftWidth = 360.0f;
	const float RightWidth = 430.0f;

	// 左
	ImGui::BeginChild( "##Left", ImVec2( LeftWidth, -FooterHeight ), ImGuiChildFlags_Borders );
	if ( ImGui::BeginTabBar( "##LeftTabs" ) )
	{
		const auto TabFlags = [&S]( int32 Index ) { return S.ForceLeftTab == Index ? ImGuiTabItemFlags_SetSelected : ImGuiTabItemFlags_None; };
		if ( ImGui::BeginTabItem( "Source", nullptr, TabFlags( 0 ) ) ) { DrawSourceTab( S ); ImGui::EndTabItem(); }
		if ( ImGui::BeginTabItem( "Grid", nullptr, TabFlags( 1 ) ) ) { DrawGridTab( S ); ImGui::EndTabItem(); }
		if ( ImGui::BeginTabItem( "Frames", nullptr, TabFlags( 2 ) ) ) { DrawFramesTab( S ); ImGui::EndTabItem(); }
		if ( ImGui::BeginTabItem( "Clips", nullptr, TabFlags( 3 ) ) ) { DrawClipsTab( S ); ImGui::EndTabItem(); }
		ImGui::EndTabBar();
		S.ForceLeftTab = INDEX_NONE;
	}
	ImGui::EndChild();

	// 中央（画像）
	ImGui::SameLine();
	ImGui::BeginChild( "##Center", ImVec2( -RightWidth - ImGui::GetStyle().ItemSpacing.x, -FooterHeight ), ImGuiChildFlags_Borders, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse );
	DrawCanvas( S );
	ImGui::EndChild();

	// 右（プレビュー・タイムライン）
	ImGui::SameLine();
	ImGui::BeginChild( "##Right", ImVec2( 0.0f, -FooterHeight ), ImGuiChildFlags_Borders );
	DrawPreview( S, IO.DeltaTime );
	DrawTimeline( S );
	ImGui::EndChild();

	// 下
	if ( S.bStatusIsError ) ImGui::TextColored( ImVec4( 1.0f, 0.5f, 0.4f, 1.0f ), "%s", UI_TEXT( S.StatusMessage ) );
	else ImGui::TextDisabled( "%s", UI_TEXT( S.StatusMessage ) );

	// どの項目も操作されていなければ、連続した編集を区切る
	if ( !ImGui::IsAnyItemActive() ) S.Document.EndEditSession();
	ImGui::End();
}
