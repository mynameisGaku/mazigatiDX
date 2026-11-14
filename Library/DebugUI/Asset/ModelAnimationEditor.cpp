#include "ModelAnimationEditor.h"
#include "ModelAnimationEditorInternal.h"
#include "Library/DebugUI/Common/EditorLog.h"
#include "Library/Core/Interfaces/Render/IDrawExecutor.h"


#include "Library/Framework/Render/Draw/DrawCommand.h"
#include "Library/Core/Log/Log.h"
#include "Library/Core/Types/CString.h"
#include "Library/Core/Types/Numeric.h"
#include "Library/DebugUI/Common/EditorCommon.h"
#include "Library/Framework/Asset/AssetSubsystem.h"
#include "Library/Framework/Engine/Engine.h"
#include "Library/Framework/Render/Draw/DrawContext.h"
#include "Library/Framework/Asset/Texture/RenderTarget.h"
#include "Library/Framework/Render/Camera/Camera.h"
#include "Library/Framework/Render/State/RenderState3D.h"
#include "Library/Platform/System/FileSystem.h"

using namespace NModelAnimationEditorUI;

namespace NModelAnimationEditorUI
{
	void SetStatus( FState& S, const FString& Text, bool bError )
	{
		S.StatusMessage = Text;
		S.bStatusIsError = bError;
		if ( bError )
		{
			LOG( LogEditorUI, Warning, "%s", *Text );
		}
	}

	void RescanFiles( FState& S, bool bForce )
	{
		CAssetSubsystem* Assets = GetAssets();
		if ( Assets == nullptr )
		{
			return;
		}
		const double Now = ImGui::GetTime();
		if ( !bForce && Now - S.LastScanTime < 3.0 )
		{
			return;
		}
		S.LastScanTime = Now;

		const FString Root = Assets->GetRootDirectory();
		S.ModelFiles = NFileSystem::ListFilesRecursive( Root, NModelFormat::GetAllModelExtensions() );
		S.DefinitionFiles = NFileSystem::ListFilesRecursive( Root, { DefinitionExtension } );
	}

	const FModelClipDef* SelectedClip( const FState& S )
	{
		return S.Document.Get().Clips.IsValidIndex( S.SelectedClip ) ? &S.Document.Get().Clips[S.SelectedClip] : nullptr;
	}

	TSharedPtr<CModel> GetEffectiveMeshModel( const FState& S )
	{
		if ( S.Asset.IsValid() && S.Asset->GetModel().IsValid() && S.Asset->GetModel()->HasMesh() )
		{
			return S.Asset->GetModel();
		}
		if ( S.PreviewModel.IsValid() && S.PreviewModel->HasMesh() )
		{
			return S.PreviewModel;
		}
		return TSharedPtr<CModel>();
	}

	//=========================================================================
	// 開く・保存
	//=========================================================================
	void ResetSelection( FState& S )
	{
		S.SelectedClip = S.Document.Get().Clips.IsEmpty() ? INDEX_NONE : 0;
		S.SelectedEvent = INDEX_NONE;
		S.BlendFrom = S.Document.Get().Clips.Num() > 0 ? 0 : INDEX_NONE;
		S.BlendTo = S.Document.Get().Clips.Num() > 1 ? 1 : INDEX_NONE;
		S.BuiltModelPath = "?";          // 必ず作り直す
		S.BuiltPreviewModelPath = "?";
		S.bFitRequested = true;
	}

	bool SaveCurrent( FState& S, const FString& AssetPath )
	{
		CAssetSubsystem* Assets = GetAssets();
		if ( Assets == nullptr || AssetPath.IsEmpty() )
		{
			return false;
		}

		FString Path = NormalizePath( AssetPath );
		if ( !Path.EndsWith( DefinitionExtension, ESearchCase::IgnoreCase ) )
		{
			Path += DefinitionExtension;
		}
		if ( !Assets->SaveModelAnimationDefinition( Path, S.Document.Get() ) )
		{
			SetStatus( S, "Failed to save '" + Path + "' (see the log).", true );
			return false;
		}
		S.FilePath = Path;
		S.Document.MarkSaved();

		if ( S.bExportMV1OnSave )
		{
			const FModelBatchExportResult Batch = Assets->ExportModelsUsedBy( S.Document.Get() );
			if ( !Batch.Items.IsEmpty() )
			{
				SetStatus( S, FString::Printf( "Saved %s (.mv1 output: %s)", *Path, *Batch.GetSummary() ), Batch.NumFailed > 0 );
				RescanFiles( S, true );
				return true;
			}
		}
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
		if ( Action == FState::EPending::Open )
		{
			NDebugUI::OpenModelAnimationFile( S, Path );
		}
		else if ( Action == FState::EPending::New )
		{
			NDebugUI::NewModelAnimationFromModel( S, Path );
		}
		else if ( Action == FState::EPending::NewMotionPack )
		{
			NDebugUI::NewMotionPack( S, Path );
		}
	}
}

//=============================================================================
// 公開
//=============================================================================
bool NDebugUI::OpenModelAnimationFile( FModelAnimationEditorState& State, const FString& AssetPath )
{
	CAssetSubsystem* Assets = GetAssets();
	if ( Assets == nullptr )
	{
		return false;
	}

	const TOptional<FString> Full = Assets->ResolvePath( AssetPath );
	if ( !Full.IsSet() )
	{
		SetStatus( State, "File not found: " + AssetPath, true );
		return false;
	}
	NJson::CJsonObject Json;
	FModelAnimationDefinition Definition;
	if ( !Json.LoadFromFile( Full.Get( FString() ) ) || !FModelAnimationDefinition::FromJson( Json.GetRoot(), Definition ) )
	{
		SetStatus( State, "Not a valid model animation definition: " + AssetPath, true );
		return false;
	}

	State.Document.Reset( Definition );
	State.FilePath = NormalizePath( AssetPath );
	ResetSelection( State );
	SetStatus( State, "Opened " + State.FilePath );
	return true;
}

bool NDebugUI::NewModelAnimationFromModel( FModelAnimationEditorState& State, const FString& ModelPath )
{
	const FString Normalized = NormalizePath( ModelPath );
	FModelAnimationDefinition Definition;
	CAssetSubsystem* Assets = GetAssets();
	const TSharedPtr<CModel> Loaded = ( Assets != nullptr && !Normalized.IsEmpty() ) ? Assets->LoadModel( Normalized ) : TSharedPtr<CModel>();
	if ( Loaded.IsValid() && Loaded->IsAnimationOnly() )
	{
		// アニメーションだけのファイル（メッシュ 0）が選ばれたら、モーション専用パックにして全アニメーションを入れる
		for ( const FModelClipInfo& Info : Loaded->GetClips() )
		{
			FModelClipDef Clip;
			Clip.Name = Info.Name.IsEmpty() ? FString::Printf( "Anim%d", Info.AnimIndex ) : Info.Name;
			Clip.SourcePath = Normalized;
			Clip.AnimIndex = Info.AnimIndex;
			Clip.AnimName = Info.Name;
			Definition.Clips.Add( Clip );
		}
		State.Document.Reset( Definition );
		State.FilePath.Empty();
		State.SaveAsPath.Empty();
		ResetSelection( State );
		SetStatus( State, "New motion pack from animation-only file " + Normalized + ". Pick a Preview model in the Model tab to view in 3D." );
		return true;
	}

	Definition.ModelPath = Normalized;
	State.Document.Reset( Definition );
	State.FilePath.Empty();
	State.SaveAsPath.Empty();
	ResetSelection( State );
	SetStatus( State, "New animation set for " + Definition.ModelPath + ". Add clips from the model, then File > Save as." );
	return true;
}

bool NDebugUI::NewMotionPack( FModelAnimationEditorState& State, const FString& PreviewModelPath )
{
	FModelAnimationDefinition Definition;
	Definition.PreviewModelPath = NormalizePath( PreviewModelPath );
	State.Document.Reset( Definition );
	State.FilePath.Empty();
	State.SaveAsPath.Empty();
	ResetSelection( State );
	SetStatus( State, "New motion pack (no bound model). Add clips from animation files, and optionally pick a Preview model." );
	return true;
}

void NDebugUI::ReleaseModelAnimationEditor( FModelAnimationEditorState& State )
{
	State.PreviewTarget.Reset();
	State.Animator = FModelAnimator();
	State.Asset.Reset();
	State.PreviewModel.Reset();
}

void NDebugUI::DrawModelAnimationEditor( FModelAnimationEditorState& S, bool* pOpen )
{
	const ImGuiViewport* MainViewport = ImGui::GetMainViewport();
	ImGui::SetNextWindowPos( ImVec2( MainViewport->Pos.x + 16.0f, MainViewport->Pos.y + 38.0f ), ImGuiCond_FirstUseEver );
	ImGui::SetNextWindowSize( ImVec2( NMath::Min( 1320.0f, MainViewport->Size.x - 32.0f ), NMath::Min( 780.0f, MainViewport->Size.y - 50.0f ) ), ImGuiCond_FirstUseEver );
	char Title[256];
	NCString::Format( Title, sizeof( Title ), "Model Animation Editor%s###ModelAnimationEditor", S.Document.IsDirty() ? " *" : "" );
	if ( !ImGui::Begin( Title, pOpen, ImGuiWindowFlags_MenuBar ) )
	{
		ImGui::End();
		return;
	}

	RescanFiles( S, false );
	SyncPreview( S );
	DrawMenuBar( S, pOpen );

	ImGuiIO& IO = ImGui::GetIO();
	if ( ImGui::IsWindowFocused( ImGuiFocusedFlags_RootAndChildWindows ) && !IO.WantTextInput )
	{
		if ( ImGui::IsKeyChordPressed( ImGuiMod_Ctrl | ImGuiKey_Z ) )
		{
			S.Document.Undo();
		}
		if ( ImGui::IsKeyChordPressed( ImGuiMod_Ctrl | ImGuiKey_Y ) || ImGui::IsKeyChordPressed( ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_Z ) )
		{
			S.Document.Redo();
		}
		if ( ImGui::IsKeyChordPressed( ImGuiMod_Ctrl | ImGuiKey_S ) && !S.FilePath.IsEmpty() )
		{
			SaveCurrent( S, S.FilePath );
		}
	}

	// 再生（速さはプレビューのスライダー。出来事を集める）
	S.Animator.Speed = S.PreviewSpeed;
	if ( S.bPlaying && SelectedClip( S ) != nullptr )
	{
		S.Animator.Update( IO.DeltaTime );
		for ( const FString& Event : S.Animator.GetEvents() )
		{
			S.EventFlashes.Add( TPair<FString, float32> { Event, 0.5f } );
		}
	}
	for ( int32 i = S.EventFlashes.Num() - 1; i >= 0; --i )
	{
		S.EventFlashes[i].Value -= IO.DeltaTime;
		if ( S.EventFlashes[i].Value <= 0.0f )
		{
			S.EventFlashes.RemoveAt( i );
		}
	}

	const float FooterHeight = ImGui::GetFrameHeightWithSpacing() + 2.0f;
	const float LeftWidth = 380.0f;

	ImGui::BeginChild( "##Left", ImVec2( LeftWidth, -FooterHeight ), ImGuiChildFlags_Borders );
	if ( ImGui::BeginTabBar( "##LeftTabs" ) )
	{
		const auto TabFlags = [&S]( int32 Index ) { return S.ForceLeftTab == Index ? ImGuiTabItemFlags_SetSelected : ImGuiTabItemFlags_None; };
		if ( ImGui::BeginTabItem( "Clips", nullptr, TabFlags( 0 ) ) )
		{
			DrawClipsTab( S ); ImGui::EndTabItem();
		}
		if ( ImGui::BeginTabItem( "Model", nullptr, TabFlags( 1 ) ) )
		{
			DrawModelTab( S ); ImGui::EndTabItem();
		}
		if ( ImGui::BeginTabItem( "Used by", nullptr, TabFlags( 2 ) ) )
		{
			NDebugUI::DrawAssetUsesPanel( S.Uses, S.FilePath );
			ImGui::EndTabItem();
		}
		ImGui::EndTabBar();
		S.ForceLeftTab = INDEX_NONE;
	}
	ImGui::EndChild();

	ImGui::SameLine();
	ImGui::BeginChild( "##Right", ImVec2( 0.0f, -FooterHeight ) );
	ImGui::BeginChild( "##Viewport", ImVec2( 0.0f, -230.0f ), ImGuiChildFlags_Borders, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse );
	DrawViewport( S );
	ImGui::EndChild();
	ImGui::BeginChild( "##Timeline", ImVec2( 0.0f, 0.0f ), ImGuiChildFlags_Borders );
	DrawTimeline( S );
	ImGui::EndChild();
	ImGui::EndChild();

	if ( S.bStatusIsError )
	{
		ImGui::TextColored( ImVec4( 1.0f, 0.5f, 0.4f, 1.0f ), "%s", UI_TEXT( S.StatusMessage ) );
	}
	else
	{
		ImGui::TextDisabled( "%s", UI_TEXT( S.StatusMessage ) );
	}

	if ( !ImGui::IsAnyItemActive() )
	{
		S.Document.EndEditSession();
	}
	ImGui::End();
}
