// SPDX-License-Identifier: MIT
#include "AIEditorInternal.h"
#include "Library/DebugUI/Common/EditorLog.h"

#include "Library/Core/Log/Log.h"
#include "Library/Core/Types/CString.h"
#include "Library/DebugUI/Common/EditorCommon.h"
#include "Library/Framework/AI/AIHotReload.h"
#include "Library/Framework/AI/BehaviorTree/BehaviorTreeRuntime.h"
#include "Library/Framework/AI/Registry/AIRegistry.h"
#include "Library/Framework/AI/Registry/AIPartsCatalog.h"
#include "Library/Framework/AI/StateMachine/StateMachineRuntime.h"
#include "Library/Framework/Asset/AssetSubsystem.h"
#include "Library/Framework/Asset/Data/SpriteSheet.h"
#include "Library/Framework/Engine/Engine.h"
#include "Library/Framework/GameInstance/GameInstance.h"
#include "Library/Framework/Scene/Scene.h"
#include "Library/Math/MathUtils.h"
#include "Library/Platform/System/FileSystem.h"

namespace
{
	using FState = FAIEditorState;

	CAssetSubsystem* GetAssets()
	{
		return GetAppSubsystem<CAssetSubsystem>();
	}

	FString NormalizePath( const FString& Path )
	{
		return Path.Replace( "\\", "/" );
	}

	const char* ExtensionOf( EAIEditorTab Tab )
	{
		switch ( Tab )
		{
		case EAIEditorTab::Blackboard:
			return NAIEditor::BlackboardExtension;
		case EAIEditorTab::StateMachine:
			return NAIEditor::StateMachineExtension;
		default:
			return NAIEditor::BehaviorTreeExtension;
		}
	}

	const char* NameOf( EAIEditorTab Tab )
	{
		switch ( Tab )
		{
		case EAIEditorTab::Blackboard:
			return "blackboard";
		case EAIEditorTab::StateMachine:
			return "state machine";
		default:
			return "behavior tree";
		}
	}

	bool IsDirty( const FState& S, EAIEditorTab Tab )
	{
		switch ( Tab )
		{
		case EAIEditorTab::Blackboard:
			return S.Blackboard.IsDirty();
		case EAIEditorTab::StateMachine:
			return S.StateMachine.IsDirty();
		default:
			return S.BehaviorTree.IsDirty();
		}
	}

	const FString& PathOf( const FState& S, EAIEditorTab Tab )
	{
		switch ( Tab )
		{
		case EAIEditorTab::Blackboard:
			return S.BlackboardPath;
		case EAIEditorTab::StateMachine:
			return S.StateMachinePath;
		default:
			return S.BehaviorTreePath;
		}
	}

	const TArray<FString>& FilesOf( const FState& S, EAIEditorTab Tab )
	{
		switch ( Tab )
		{
		case EAIEditorTab::Blackboard:
			return S.BlackboardFiles;
		case EAIEditorTab::StateMachine:
			return S.StateMachineFiles;
		default:
			return S.BehaviorTreeFiles;
		}
	}

	bool CanUndo( const FState& S, EAIEditorTab Tab )
	{
		switch ( Tab )
		{
		case EAIEditorTab::Blackboard:
			return S.Blackboard.CanUndo();
		case EAIEditorTab::StateMachine:
			return S.StateMachine.CanUndo();
		default:
			return S.BehaviorTree.CanUndo();
		}
	}

	bool CanRedo( const FState& S, EAIEditorTab Tab )
	{
		switch ( Tab )
		{
		case EAIEditorTab::Blackboard:
			return S.Blackboard.CanRedo();
		case EAIEditorTab::StateMachine:
			return S.StateMachine.CanRedo();
		default:
			return S.BehaviorTree.CanRedo();
		}
	}

	void Undo( FState& S, EAIEditorTab Tab )
	{
		switch ( Tab )
		{
		case EAIEditorTab::Blackboard:
			S.Blackboard.Undo();
			break;
		case EAIEditorTab::StateMachine:
			S.StateMachine.Undo();
			break;
		default:
			S.BehaviorTree.Undo();
			break;
		}
	}

	void Redo( FState& S, EAIEditorTab Tab )
	{
		switch ( Tab )
		{
		case EAIEditorTab::Blackboard:
			S.Blackboard.Redo();
			break;
		case EAIEditorTab::StateMachine:
			S.StateMachine.Redo();
			break;
		default:
			S.BehaviorTree.Redo();
			break;
		}
	}

	void ResetSelection( FState& S )
	{
		S.SelectedKey = INDEX_NONE;
		S.SelectedState = INDEX_NONE;
		S.SelectedTransition.Clear();
		S.SelectedNode = INDEX_NONE;
		S.NameBufferKey = INDEX_NONE;
		S.StateMachineView.bFitted = false;
		S.BehaviorTreeView.bFitted = false;
		S.bIssuesValid = false;
	}

	//=========================================================================
	// 開く・新規・保存
	//=========================================================================
	bool OpenTab( FState& S, EAIEditorTab Tab, const FString& AssetPath )
	{
		CAssetSubsystem* Assets = GetAssets();
		if ( Assets == nullptr )
		{
			return false;
		}
		const FString Path = NormalizePath( AssetPath );
		switch ( Tab )
		{
		case EAIEditorTab::Blackboard:
		{
			const TSharedPtr<CBlackboardAsset> Asset = Assets->LoadBlackboard( Path );
			if ( !Asset.IsValid() )
			{
				NAIEditor::SetStatus( S, "Not a valid blackboard: " + Path, true );
				return false;
			}
			S.Blackboard.Reset( Asset->GetDefinition() );
			S.BlackboardPath = Path;
			break;
		}
		case EAIEditorTab::StateMachine:
		{
			const TSharedPtr<CStateMachineAsset> Asset = Assets->LoadStateMachine( Path );
			if ( !Asset.IsValid() )
			{
				NAIEditor::SetStatus( S, "Not a valid state machine: " + Path, true );
				return false;
			}
			S.StateMachine.Reset( Asset->GetDefinition() );
			S.StateMachinePath = Path;
			break;
		}
		default:
		{
			const TSharedPtr<CBehaviorTreeAsset> Asset = Assets->LoadBehaviorTree( Path );
			if ( !Asset.IsValid() )
			{
				NAIEditor::SetStatus( S, "Not a valid behavior tree: " + Path, true );
				return false;
			}
			S.BehaviorTree.Reset( Asset->GetDefinition() );
			S.BehaviorTreePath = Path;
			break;
		}
		}
		S.Tab = Tab;
		S.bSelectTabRequested = true;
		ResetSelection( S );
		NAIEditor::SetStatus( S, "Opened " + Path );
		return true;
	}

	void NewTab( FState& S, EAIEditorTab Tab )
	{
		switch ( Tab )
		{
		case EAIEditorTab::Blackboard:
			S.Blackboard.Reset( FBlackboardDefinition() );
			S.BlackboardPath.Empty();
			break;
		case EAIEditorTab::StateMachine:
		{
			// 1 つ目のステートを置いておく（入口が無いと、保存しても使えないため）
			FStateMachineDefinition Definition;
			FStateDef Idle;
			Idle.Name = "Idle";
			Definition.States.Add( Idle );
			Definition.Entry = "Idle";
			S.StateMachine.Reset( Definition );
			S.StateMachinePath.Empty();
			break;
		}
		default:
		{
			// ルートの Selector を置いておく
			FBehaviorTreeDefinition Definition;
			FBTNodeDef Root;
			Root.Type = EBTNodeType::Selector;
			Definition.Nodes.Add( Root );
			S.BehaviorTree.Reset( Definition );
			S.BehaviorTreePath.Empty();
			break;
		}
		}
		S.SaveAsPath.Empty();
		ResetSelection( S );
		NAIEditor::SetStatus( S, FString::Printf( "New %s. Edit it, then File > Save as.", NameOf( Tab ) ) );
	}

	bool SaveTab( FState& S, EAIEditorTab Tab, const FString& AssetPath )
	{
		CAssetSubsystem* Assets = GetAssets();
		if ( Assets == nullptr || AssetPath.IsEmpty() )
		{
			return false;
		}
		FString Path = NormalizePath( AssetPath );
		if ( !Path.EndsWith( ExtensionOf( Tab ), ESearchCase::IgnoreCase ) )
		{
			Path += ExtensionOf( Tab );
		}
		bool bSaved = false;
		switch ( Tab )
		{
		case EAIEditorTab::Blackboard:
			bSaved = Assets->SaveBlackboard( Path, S.Blackboard.Get() );
			if ( bSaved )
			{
				S.BlackboardPath = Path;
				S.Blackboard.MarkSaved();
				// 参照先として覚えていたものは、古くなった
				S.CachedBlackboardPath.Empty();
			}
			break;
		case EAIEditorTab::StateMachine:
			bSaved = Assets->SaveStateMachine( Path, S.StateMachine.Get() );
			if ( bSaved )
			{
				S.StateMachinePath = Path;
				S.StateMachine.MarkSaved();
			}
			break;
		default:
			bSaved = Assets->SaveBehaviorTree( Path, S.BehaviorTree.Get() );
			if ( bSaved )
			{
				S.BehaviorTreePath = Path;
				S.BehaviorTree.MarkSaved();
			}
			break;
		}
		if ( !bSaved )
		{
			NAIEditor::SetStatus( S, "Failed to save '" + Path + "' (see the log).", true );
			return false;
		}
		// 動いているシーンの AI へ反映する（ホットリロード）
		int32 NumReloaded = 0;
		CEngine* Engine = CEngine::Get();
		CGameInstance* GameInstance = Engine != nullptr ? Engine->GetGameInstance() : nullptr;
		if ( CScene* Scene = GameInstance != nullptr ? GameInstance->GetSceneManager().GetCurrentScene() : nullptr )
		{
			NumReloaded = NAI::ReloadAsset( *Scene, Path );
		}
		NAIEditor::SetStatus( S, NumReloaded > 0 ? FString::Printf( "Saved %s (reloaded %d running component(s))", *Path, NumReloaded ) : "Saved " + Path );
		NAIEditor::RescanFiles( S, true );
		return true;
	}

	void RequestAction( FState& S, FState::EPending Action, const FString& Path )
	{
		if ( IsDirty( S, S.Tab ) )
		{
			S.Pending = Action;
			S.PendingPath = Path;
			S.bRequestOpenUnsavedPopup = true;
			return;
		}
		if ( Action == FState::EPending::Open )
		{
			OpenTab( S, S.Tab, Path );
		}
		else if ( Action == FState::EPending::New )
		{
			NewTab( S, S.Tab );
		}
	}

	//=========================================================================
	// 部品のカタログ（ゲームが書き出した、部品の名前とスキーマ）
	//=========================================================================
	void LoadPartsCatalog( FState& S )
	{
		S.bPartsCatalogTried = true;
		CAssetSubsystem* Assets = GetAssets();
		if ( Assets == nullptr )
		{
			return;
		}
		TArray<FAIIssue> Issues;
		const int32 Count = NAI::LoadPartsCatalog( *Assets, NAI::GetRegistry(), FString(), &Issues );
		if ( Count < 0 )
		{
			// カタログが無い（ゲームの部品は「未登録」と出る）。ゲームの実行ファイルでは、本物の部品が登録されているので、気にしない
			NAIEditor::SetStatus( S, FString::Printf( "No parts catalog (%s). Game-specific parts show as not registered; write it from the game with the console command 'ai.parts'.", NAI::DefaultPartsCatalogPath ) );
		}
		else
		{
			NAIEditor::SetStatus( S, FString::Printf( "Parts catalog: %d part(s) added from %s.", Count, NAI::DefaultPartsCatalogPath ) );
		}
		S.bIssuesValid = false;
	}

	//=========================================================================
	// メニューバーと、ファイルのポップアップ
	//=========================================================================
	void DrawMenuBar( FState& S, bool* pOpen )
	{
		if ( !ImGui::BeginMenuBar() )
		{
			return;
		}
		const EAIEditorTab Tab = S.Tab;
		bool bOpenOpenPopup = false;
		bool bOpenSaveAsPopup = false;

		if ( ImGui::BeginMenu( "File" ) )
		{
			if ( ImGui::MenuItem( "New" ) )
			{
				RequestAction( S, FState::EPending::New, FString() );
			}
			if ( ImGui::MenuItem( "Open..." ) )
			{
				bOpenOpenPopup = true;
			}
			ImGui::Separator();
			if ( ImGui::MenuItem( "Save", "Ctrl+S" ) )
			{
				if ( PathOf( S, Tab ).IsEmpty() )
				{
					bOpenSaveAsPopup = true;
				}
				else
				{
					SaveTab( S, Tab, PathOf( S, Tab ) );
				}
			}
			if ( ImGui::MenuItem( "Save as..." ) )
			{
				bOpenSaveAsPopup = true;
			}
			if ( ImGui::MenuItem( "Reload from disk", nullptr, false, !PathOf( S, Tab ).IsEmpty() ) )
			{
				RequestAction( S, FState::EPending::Open, PathOf( S, Tab ) );
			}
			if ( ImGui::MenuItem( "Reload parts catalog" ) )
			{
				LoadPartsCatalog( S );
			}
			ImGui::Separator();
			if ( ImGui::MenuItem( "Close window" ) && pOpen != nullptr )
			{
				*pOpen = false;
			}
			ImGui::EndMenu();
		}
		if ( ImGui::BeginMenu( "Edit" ) )
		{
			if ( ImGui::MenuItem( "Undo", "Ctrl+Z", false, CanUndo( S, Tab ) ) )
			{
				Undo( S, Tab );
			}
			if ( ImGui::MenuItem( "Redo", "Ctrl+Y", false, CanRedo( S, Tab ) ) )
			{
				Redo( S, Tab );
			}
			ImGui::EndMenu();
		}
		ImGui::TextDisabled( "  %s%s", PathOf( S, Tab ).IsEmpty() ? "(unsaved)" : UI_TEXT( PathOf( S, Tab ) ), IsDirty( S, Tab ) ? " *" : "" );
		ImGui::EndMenuBar();

		if ( bOpenOpenPopup )
		{
			NAIEditor::RescanFiles( S, true );
			ImGui::OpenPopup( "Open file" );
		}
		if ( bOpenSaveAsPopup )
		{
			if ( S.SaveAsPath.IsEmpty() )
			{
				S.SaveAsPath = FString( "AI/New" ) + ExtensionOf( Tab );
			}
			ImGui::OpenPopup( "Save file as" );
		}
		if ( S.bRequestOpenUnsavedPopup )
		{
			ImGui::OpenPopup( "Unsaved changes" );
			S.bRequestOpenUnsavedPopup = false;
		}

		const ImVec2 PopupSize( 460.0f, 0.0f );
		ImGui::SetNextWindowSize( PopupSize );
		if ( ImGui::BeginPopupModal( "Open file", nullptr, ImGuiWindowFlags_AlwaysAutoResize ) )
		{
			ImGui::TextUnformatted( UI_TEXT( FString::Printf( "Choose a %s (*%s under the asset folder):", NameOf( Tab ), ExtensionOf( Tab ) ) ) );
			NDebugUI::FileCombo( "##OpenFile", S.OpenSelection, FilesOf( S, Tab ), "(choose a file)" );
			ImGui::BeginDisabled( S.OpenSelection.IsEmpty() );
			if ( ImGui::Button( "Open" ) )
			{
				RequestAction( S, FState::EPending::Open, S.OpenSelection );
				ImGui::CloseCurrentPopup();
			}
			ImGui::EndDisabled();
			ImGui::SameLine();
			if ( ImGui::Button( "Cancel" ) )
			{
				ImGui::CloseCurrentPopup();
			}
			ImGui::EndPopup();
		}
		ImGui::SetNextWindowSize( PopupSize );
		if ( ImGui::BeginPopupModal( "Save file as", nullptr, ImGuiWindowFlags_AlwaysAutoResize ) )
		{
			ImGui::TextUnformatted( UI_TEXT( FString::Printf( "Path under the asset folder (the %s extension is added):", ExtensionOf( Tab ) ) ) );
			NDebugUI::InputString( "##SaveAs", S.SaveAsPath );
			if ( ImGui::Button( "Save" ) && !S.SaveAsPath.IsEmpty() )
			{
				if ( SaveTab( S, Tab, S.SaveAsPath ) )
				{
					ImGui::CloseCurrentPopup();
				}
			}
			ImGui::SameLine();
			if ( ImGui::Button( "Cancel" ) )
			{
				ImGui::CloseCurrentPopup();
			}
			ImGui::EndPopup();
		}
		if ( ImGui::BeginPopupModal( "Unsaved changes", nullptr, ImGuiWindowFlags_AlwaysAutoResize ) )
		{
			ImGui::TextUnformatted( UI_TEXT( FString::Printf( "The %s has unsaved changes.", NameOf( Tab ) ) ) );
			const auto Continue = [&]()
			{
				const FState::EPending Action = S.Pending;
				const FString Path = S.PendingPath;
				S.Pending = FState::EPending::None;
				if ( Action == FState::EPending::Open )
				{
					OpenTab( S, Tab, Path );
				}
				else if ( Action == FState::EPending::New )
				{
					NewTab( S, Tab );
				}
			};
			if ( ImGui::Button( "Save and continue" ) )
			{
				if ( !PathOf( S, Tab ).IsEmpty() && SaveTab( S, Tab, PathOf( S, Tab ) ) )
				{
					Continue();
					ImGui::CloseCurrentPopup();
				}
				else if ( PathOf( S, Tab ).IsEmpty() )
				{
					NAIEditor::SetStatus( S, "Use File > Save as... first (this file has no path yet).", true );
					ImGui::CloseCurrentPopup();
				}
			}
			ImGui::SameLine();
			if ( ImGui::Button( "Discard changes" ) )
			{
				switch ( Tab )
				{
				case EAIEditorTab::Blackboard:
					S.Blackboard.MarkSaved();
					break;
				case EAIEditorTab::StateMachine:
					S.StateMachine.MarkSaved();
					break;
				default:
					S.BehaviorTree.MarkSaved();
					break;
				}
				Continue();
				ImGui::CloseCurrentPopup();
			}
			ImGui::SameLine();
			if ( ImGui::Button( "Cancel" ) )
			{
				S.Pending = FState::EPending::None;
				ImGui::CloseCurrentPopup();
			}
			ImGui::EndPopup();
		}
	}

	void DrawStatusLine( const FState& S )
	{
		if ( S.StatusMessage.IsEmpty() )
		{
			return;
		}
		ImGui::Separator();
		if ( S.bStatusIsError )
		{
			ImGui::TextColored( ImVec4( 1.0f, 0.5f, 0.45f, 1.0f ), "%s", UI_TEXT( S.StatusMessage ) );
		}
		else
		{
			ImGui::TextDisabled( "%s", UI_TEXT( S.StatusMessage ) );
		}
	}
}

//=============================================================================
// 共有する関数
//=============================================================================
void NAIEditor::SetStatus( FAIEditorState& State, const FString& Text, bool bError )
{
	State.StatusMessage = Text;
	State.bStatusIsError = bError;
	if ( bError )
	{
		LOG( LogEditorUI, Warning, "%s", *Text );
	}
}

const FBlackboardDefinition* NAIEditor::FindBlackboard( FAIEditorState& State, const FString& Path )
{
	if ( Path.IsEmpty() )
	{
		return nullptr;
	}
	if ( Path == State.BlackboardPath )
	{
		return &State.Blackboard.Get();
	}
	if ( State.CachedBlackboardPath != Path )
	{
		State.CachedBlackboardPath = Path;
		State.bCachedBlackboardValid = false;
		if ( CAssetSubsystem* Assets = GetAssets() )
		{
			if ( const TSharedPtr<CBlackboardAsset> Asset = Assets->LoadBlackboard( Path ); Asset.IsValid() )
			{
				State.CachedBlackboard = Asset->GetDefinition();
				State.bCachedBlackboardValid = true;
			}
		}
	}
	return State.bCachedBlackboardValid ? &State.CachedBlackboard : nullptr;
}

FAIInspectContext NAIEditor::MakeInspectContext( FAIEditorState& State, const FString& BlackboardPath )
{
	FAIInspectContext Context;
	Context.Blackboard = FindBlackboard( State, BlackboardPath );
	Context.ClipNames = &State.ClipNames;
	return Context;
}

void NAIEditor::RescanFiles( FAIEditorState& State, bool bForce )
{
	CAssetSubsystem* Assets = GetAssets();
	if ( Assets == nullptr )
	{
		return;
	}
	const double Now = ImGui::GetTime();
	if ( !bForce && Now - State.LastScanTime < 3.0 )
	{
		return;
	}
	State.LastScanTime = Now;

	const FString Root = Assets->GetRootDirectory();
	State.BlackboardFiles = NFileSystem::ListFilesRecursive( Root, { BlackboardExtension } );
	State.StateMachineFiles = NFileSystem::ListFilesRecursive( Root, { StateMachineExtension } );
	State.BehaviorTreeFiles = NFileSystem::ListFilesRecursive( Root, { BehaviorTreeExtension } );

	// スプライトシートのクリップの名前（Clip のパラメータ・ステートの Clip の選択肢）
	State.ClipNames.Reset();
	for ( const FString& File : NFileSystem::ListFilesRecursive( Root, { ".gxsprite" } ) )
	{
		const TOptional<FString> Full = Assets->ResolvePath( File );
		NJson::CJsonObject Json;
		FSpriteSheetDefinition Definition;
		if ( Full.IsSet() && Json.LoadFromFile( Full.Get( FString() ) ) && FSpriteSheetDefinition::FromJson( Json.GetRoot(), Definition ) )
		{
			for ( const FSpriteClip& Clip : Definition.Clips )
			{
				State.ClipNames.AddUnique( Clip.Name );
			}
		}
	}
	// 参照先のブラックボードも読み直す
	State.CachedBlackboardPath.Empty();
	State.bIssuesValid = false;
}

void NAIEditor::PropagateKeyRename( FAIEditorState& State, const FString& OldName, const FString& NewName )
{
	if ( State.BlackboardPath.IsEmpty() || OldName == NewName )
	{
		return;
	}
	if ( State.StateMachine.Get().Blackboard == State.BlackboardPath )
	{
		NStateMachineEdit::RenameBlackboardKey( State.StateMachine, OldName, NewName );
	}
	if ( State.BehaviorTree.Get().Blackboard == State.BlackboardPath )
	{
		NBehaviorTreeEdit::RenameBlackboardKey( State.BehaviorTree, OldName, NewName );
	}
}

int32 NAIEditor::CountKeyUsage( const FAIEditorState& State, const FString& KeyName )
{
	int32 Count = 0;
	if ( !State.BlackboardPath.IsEmpty() )
	{
		Count += State.StateMachine.Get().Blackboard == State.BlackboardPath ? NStateMachineEdit::CountBlackboardKeyUsage( State.StateMachine.Get(), KeyName ) : 0;
		Count += State.BehaviorTree.Get().Blackboard == State.BlackboardPath ? NBehaviorTreeEdit::CountBlackboardKeyUsage( State.BehaviorTree.Get(), KeyName ) : 0;
	}
	return Count;
}

//=============================================================================
// 検証
//=============================================================================
namespace
{
	bool IsMemberSegment( const FString& Segment )
	{
		return Segment.StartsWith( "OnEnter[" ) || Segment.StartsWith( "OnTick[" ) || Segment.StartsWith( "OnExit[" ) || Segment.StartsWith( "OnClipEvent[" ) || Segment.StartsWith( "Transitions[" ) || Segment.StartsWith( "AnyTransitions[" ) || Segment.StartsWith( "When[" );
	}

	int32 IndexInBrackets( const FString& Segment )
	{
		const int32 Open = Segment.Find( "[" );
		return Open == INDEX_NONE ? -1 : NCString::ToInt32( *Segment.Mid( Open + 1 ) );
	}

	// 問題の場所（コンパイルのときの書式。"States/Run/OnEnter[0]"・"States/Locomotion/Transitions[1]/When[0]"）から、ステートと遷移を探す
	void JumpToStateMachineIssue( FState& S, const FString& Where )
	{
		const FStateMachineDefinition& Definition = S.StateMachine.Get();
		const TArray<FString> Segments = Where.ParseIntoArray( "/" );
		int32 Index = ( !Segments.IsEmpty() && Segments[0] == "States" ) ? 1 : 0;
		FString Path;
		for ( ; Index < Segments.Num() && !IsMemberSegment( Segments[Index] ); ++Index )
		{
			Path += ( Path.IsEmpty() ? "" : "/" ) + Segments[Index];
		}
		const int32 State = Path.IsEmpty() ? -1 : Definition.FindByPath( -1, Path );
		if ( !Path.IsEmpty() && State < 0 )
		{
			return;
		}
		S.SelectedState = INDEX_NONE;
		S.SelectedTransition.Clear();
		if ( Index < Segments.Num() && ( Segments[Index].StartsWith( "Transitions[" ) || Segments[Index].StartsWith( "AnyTransitions[" ) ) )
		{
			S.SelectedTransition.Machine = State;
			S.SelectedTransition.bAny = Segments[Index].StartsWith( "AnyTransitions[" );
			S.SelectedTransition.Index = IndexInBrackets( Segments[Index] );
		}
		else if ( State >= 0 )
		{
			S.SelectedState = State;
		}
		S.bFocusRequested = true;
	}

	// 問題の場所（"Root/Attack/Wait #5/Decorators[0]" のような、ルートからのノードの名前）から、ノードを探す
	void JumpToBehaviorTreeIssue( FState& S, const FString& Where )
	{
		const FBehaviorTreeDefinition& Definition = S.BehaviorTree.Get();
		TArray<FString> Segments = Where.ParseIntoArray( "/" );
		while ( !Segments.IsEmpty() && ( Segments.Last().StartsWith( "Decorators[" ) || Segments.Last().StartsWith( "Services[" ) ) )
		{
			Segments.Pop();
		}
		if ( Segments.IsEmpty() || Definition.Nodes.IsEmpty() || Definition.GetNodeLabel( 0 ) != Segments[0] )
		{
			return;
		}
		int32 Node = 0;
		for ( int32 Index = 1; Index < Segments.Num(); ++Index )
		{
			int32 Found = INDEX_NONE;
			for ( const int32 Child : Definition.GetChildren( Node ) )
			{
				if ( Definition.GetNodeLabel( Child ) == Segments[Index] )
				{
					Found = Child;
					break;
				}
			}
			if ( Found == INDEX_NONE )
			{
				return;
			}
			Node = Found;
		}
		S.SelectedNode = Node;
		S.bFocusRequested = true;
	}

	void JumpToIssue( FState& S, const FAIIssue& Issue )
	{
		if ( S.Tab == EAIEditorTab::StateMachine )
		{
			JumpToStateMachineIssue( S, Issue.Where );
		}
		else if ( S.Tab == EAIEditorTab::BehaviorTree )
		{
			JumpToBehaviorTreeIssue( S, Issue.Where );
		}
	}

	void RefreshIssues( FState& S )
	{
		uint32 DocumentRevision = 0;
		uint32 BlackboardRevision = 0;
		FString Path;
		switch ( S.Tab )
		{
		case EAIEditorTab::Blackboard:
			DocumentRevision = S.Blackboard.GetRevision();
			Path = S.BlackboardPath;
			break;
		case EAIEditorTab::StateMachine:
			DocumentRevision = S.StateMachine.GetRevision();
			Path = S.StateMachine.Get().Blackboard;
			break;
		default:
			DocumentRevision = S.BehaviorTree.GetRevision();
			Path = S.BehaviorTree.Get().Blackboard;
			break;
		}
		// 参照先のブラックボードが、開いている文書なら、その編集のたびに計算し直す
		if ( S.Tab != EAIEditorTab::Blackboard && !Path.IsEmpty() && Path == S.BlackboardPath )
		{
			BlackboardRevision = S.Blackboard.GetRevision();
		}
		if ( S.bIssuesValid && S.IssuesTab == S.Tab && S.IssuesDocumentRevision == DocumentRevision && S.IssuesBlackboardRevision == BlackboardRevision && S.IssuesPath == Path )
		{
			return;
		}
		S.Issues.Reset();
		S.IssuesTab = S.Tab;
		S.IssuesDocumentRevision = DocumentRevision;
		S.IssuesBlackboardRevision = BlackboardRevision;
		S.IssuesPath = Path;
		S.bIssuesValid = true;

		if ( S.Tab == EAIEditorTab::Blackboard )
		{
			for ( const FBlackboardKeyDefinition& Key : S.Blackboard.Get().Keys )
			{
				if ( Key.Type == EBlackboardKeyType::Enum && Key.EnumValues.IsEmpty() )
				{
					NAI::AddIssue( &S.Issues, EAIIssueSeverity::Warning, "Keys/" + Key.Name, "An Enum key has no values." );
				}
			}
			return;
		}

		const FBlackboardDefinition* Blackboard = NAIEditor::FindBlackboard( S, Path );
		FBlackboardDefinition Empty;
		if ( Path.IsEmpty() )
		{
			NAI::AddIssue( &S.Issues, EAIIssueSeverity::Warning, "Blackboard", "No blackboard is set (blackboard keys cannot be checked)." );
		}
		else if ( Blackboard == nullptr )
		{
			NAI::AddIssue( &S.Issues, EAIIssueSeverity::Warning, "Blackboard", "The blackboard '" + Path + "' was not found (blackboard keys cannot be checked)." );
		}
		const FBlackboardDefinition& Used = Blackboard != nullptr ? *Blackboard : Empty;
		if ( S.Tab == EAIEditorTab::StateMachine )
		{
			TSharedPtr<FStateMachineDefinition> Copy = MakeShared<FStateMachineDefinition>( S.StateMachine.Get() );
			TUniquePtr<CStateMachine> Compiled = CStateMachine::Create( Copy, NAI::GetRegistry(), Used, &S.Issues );
		}
		else
		{
			TSharedPtr<FBehaviorTreeDefinition> Copy = MakeShared<FBehaviorTreeDefinition>( S.BehaviorTree.Get() );
			TUniquePtr<CBehaviorTree> Compiled = CBehaviorTree::Create( Copy, NAI::GetRegistry(), Used, &S.Issues );
		}
	}
}

void NAIEditor::DrawIssues( FAIEditorState& State )
{
	RefreshIssues( State );
	int32 NumErrors = 0;
	int32 NumWarnings = 0;
	for ( const FAIIssue& Issue : State.Issues )
	{
		NumErrors += Issue.Severity == EAIIssueSeverity::Error ? 1 : 0;
		NumWarnings += Issue.Severity == EAIIssueSeverity::Warning ? 1 : 0;
	}
	const FString Title = FString::Printf( "Issues (%d errors, %d warnings, %d infos)###Issues", NumErrors, NumWarnings, State.Issues.Num() - NumErrors - NumWarnings );
	if ( !ImGui::CollapsingHeader( UI_TEXT( Title ), State.Issues.IsEmpty() ? 0 : ImGuiTreeNodeFlags_DefaultOpen ) )
	{
		return;
	}
	if ( State.Issues.IsEmpty() )
	{
		ImGui::TextDisabled( "No issues." );
		return;
	}
	for ( const FAIIssue& Issue : State.Issues )
	{
		ImVec4 Color( 0.7f, 0.75f, 0.9f, 1.0f );
		const char* Mark = "[i]";
		if ( Issue.Severity == EAIIssueSeverity::Error )
		{
			Color = ImVec4( 1.0f, 0.45f, 0.4f, 1.0f );
			Mark = "[E]";
		}
		else if ( Issue.Severity == EAIIssueSeverity::Warning )
		{
			Color = ImVec4( 0.95f, 0.82f, 0.35f, 1.0f );
			Mark = "[W]";
		}
		ImGui::PushID( static_cast<int32>( &Issue - State.Issues.GetData() ) );
		ImGui::PushStyleColor( ImGuiCol_Text, Color );
		const bool bClicked = ImGui::Selectable( UI_TEXT( FString( Mark ) + " " + ( Issue.Where.IsEmpty() ? Issue.Message : Issue.Where + ": " + Issue.Message ) ) );
		ImGui::PopStyleColor();
		if ( State.Tab != EAIEditorTab::Blackboard && ImGui::IsItemHovered() )
		{
			ImGui::SetTooltip( "Click to select it in the graph." );
		}
		if ( bClicked )
		{
			JumpToIssue( State, Issue );
		}
		ImGui::PopID();
	}
}

//=============================================================================
// 公開
//=============================================================================
bool NDebugUI::OpenAIEditorFile( FAIEditorState& State, const FString& AssetPath )
{
	EAIEditorTab Tab = EAIEditorTab::BehaviorTree;
	if ( AssetPath.EndsWith( NAIEditor::BlackboardExtension, ESearchCase::IgnoreCase ) )
	{
		Tab = EAIEditorTab::Blackboard;
	}
	else if ( AssetPath.EndsWith( NAIEditor::StateMachineExtension, ESearchCase::IgnoreCase ) )
	{
		Tab = EAIEditorTab::StateMachine;
	}
	else if ( !AssetPath.EndsWith( NAIEditor::BehaviorTreeExtension, ESearchCase::IgnoreCase ) )
	{
		NAIEditor::SetStatus( State, "Not an AI asset (.gxblackboard / .gxstate / .gxbt): " + AssetPath, true );
		return false;
	}
	return OpenTab( State, Tab, AssetPath );
}

void NDebugUI::DrawAIEditor( FAIEditorState& S, bool* pOpen )
{
	const ImGuiViewport* MainViewport = ImGui::GetMainViewport();
	ImGui::SetNextWindowPos( ImVec2( MainViewport->Pos.x + 16.0f, MainViewport->Pos.y + 38.0f ), ImGuiCond_FirstUseEver );
	ImGui::SetNextWindowSize( ImVec2( NMath::Min( 1360.0f, MainViewport->Size.x - 32.0f ), NMath::Min( 800.0f, MainViewport->Size.y - 50.0f ) ), ImGuiCond_FirstUseEver );
	char Title[128];
	NCString::Format( Title, sizeof( Title ), "AI Editor%s###AIEditor", ( S.Blackboard.IsDirty() || S.StateMachine.IsDirty() || S.BehaviorTree.IsDirty() ) ? " *" : "" );
	if ( !ImGui::Begin( Title, pOpen, ImGuiWindowFlags_MenuBar ) )
	{
		ImGui::End();
		return;
	}

	if ( !S.bPartsCatalogTried )
	{
		LoadPartsCatalog( S );
	}
	NAIEditor::RescanFiles( S, false );
	DrawMenuBar( S, pOpen );

	// ショートカット（文字を入力しているときは、文字入力に任せる）
	const ImGuiIO& IO = ImGui::GetIO();
	if ( ImGui::IsWindowFocused( ImGuiFocusedFlags_RootAndChildWindows ) && IO.KeyCtrl && !IO.WantTextInput )
	{
		if ( ImGui::IsKeyPressed( ImGuiKey_Z ) )
		{
			Undo( S, S.Tab );
		}
		if ( ImGui::IsKeyPressed( ImGuiKey_Y ) )
		{
			Redo( S, S.Tab );
		}
		if ( ImGui::IsKeyPressed( ImGuiKey_S ) && !PathOf( S, S.Tab ).IsEmpty() )
		{
			SaveTab( S, S.Tab, PathOf( S, S.Tab ) );
		}
	}

	if ( ImGui::BeginTabBar( "##AITabs" ) )
	{
		const auto DrawTab = [&]( const char* Name, const char* Id, EAIEditorTab Value, void ( *Draw )( FAIEditorState& ) )
		{
			const FString Label = FString::Printf( "%s%s###%s", Name, IsDirty( S, Value ) ? " *" : "", Id );
			const ImGuiTabItemFlags Flags = ( S.bSelectTabRequested && S.Tab == Value ) ? ImGuiTabItemFlags_SetSelected : 0;
			if ( ImGui::BeginTabItem( UI_TEXT( Label ), nullptr, Flags ) )
			{
				if ( !S.bSelectTabRequested )
				{
					S.Tab = Value;
				}
				Draw( S );
				ImGui::EndTabItem();
			}
		};
		DrawTab( "Behavior Tree", "BehaviorTree", EAIEditorTab::BehaviorTree, &NAIEditor::DrawBehaviorTreeTab );
		DrawTab( "State Machine", "StateMachine", EAIEditorTab::StateMachine, &NAIEditor::DrawStateMachineTab );
		DrawTab( "Blackboard", "Blackboard", EAIEditorTab::Blackboard, &NAIEditor::DrawBlackboardTab );
		ImGui::EndTabBar();
		S.bSelectTabRequested = false;
	}
	DrawStatusLine( S );
	ImGui::End();
}
