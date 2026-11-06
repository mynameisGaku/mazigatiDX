// SPDX-License-Identifier: MIT
#include "AssetBrowser.h"

#include "Library/Core/Collection/Sort.h"
#include "Library/DebugUI/Common/EditorCommon.h"
#include "Library/Framework/Asset/AssetSubsystem.h"
#include "Library/Framework/Engine/Engine.h"
#include "Library/Math/MathUtils.h"
#include "Library/Platform/System/FileSystem.h"
#include "VendorModules/ImGui/imgui.h"

namespace
{
	struct FKindInfo
	{
		const char* Extension;
		EAssetBrowserKind Kind;
	};

	constexpr FKindInfo KindTable[] =
	{
		{ ".gxsprite", EAssetBrowserKind::SpriteSheet },
		{ ".gxmodelanim", EAssetBrowserKind::ModelAnimation },
		{ ".gxworld", EAssetBrowserKind::World },
		{ ".gxeffect", EAssetBrowserKind::Effect },
		{ ".gxblackboard", EAssetBrowserKind::Blackboard },
		{ ".gxstate", EAssetBrowserKind::StateMachine },
		{ ".gxbt", EAssetBrowserKind::BehaviorTree },
		{ ".gxparts", EAssetBrowserKind::PartsCatalog },
		{ ".png", EAssetBrowserKind::Texture },
		{ ".jpg", EAssetBrowserKind::Texture },
		{ ".jpeg", EAssetBrowserKind::Texture },
		{ ".bmp", EAssetBrowserKind::Texture },
		{ ".tga", EAssetBrowserKind::Texture },
		{ ".dds", EAssetBrowserKind::Texture },
		{ ".mv1", EAssetBrowserKind::Model },
		{ ".fbx", EAssetBrowserKind::Model },
		{ ".obj", EAssetBrowserKind::Model },
		{ ".x", EAssetBrowserKind::Model },
		{ ".pmx", EAssetBrowserKind::Model },
		{ ".wav", EAssetBrowserKind::Sound },
		{ ".ogg", EAssetBrowserKind::Sound },
		{ ".mp3", EAssetBrowserKind::Sound },
	};

	ImVec4 KindColor( EAssetBrowserKind Kind )
	{
		switch ( Kind )
		{
		case EAssetBrowserKind::SpriteSheet:
			return ImVec4( 0.55f, 0.85f, 0.55f, 1.0f );
		case EAssetBrowserKind::ModelAnimation:
			return ImVec4( 0.95f, 0.75f, 0.45f, 1.0f );
		case EAssetBrowserKind::World:
			return ImVec4( 0.55f, 0.75f, 1.0f, 1.0f );
		case EAssetBrowserKind::Effect:
			return ImVec4( 0.95f, 0.6f, 0.9f, 1.0f );
		case EAssetBrowserKind::Blackboard:
		case EAssetBrowserKind::StateMachine:
		case EAssetBrowserKind::BehaviorTree:
		case EAssetBrowserKind::PartsCatalog:
			return ImVec4( 0.95f, 0.9f, 0.45f, 1.0f );
		default:
			return ImVec4( 0.6f, 0.62f, 0.68f, 1.0f );
		}
	}

	bool InCategory( int32 Category, EAssetBrowserKind Kind )
	{
		switch ( Category )
		{
		case 0:
			return true;
		case 1:
			return NDebugUI::HasEditor( Kind );
		case 2:
			return Kind == EAssetBrowserKind::SpriteSheet;
		case 3:
			return Kind == EAssetBrowserKind::ModelAnimation;
		case 4:
			return Kind == EAssetBrowserKind::World;
		case 5:
			return Kind == EAssetBrowserKind::Effect;
		case 6:
			return Kind == EAssetBrowserKind::Blackboard || Kind == EAssetBrowserKind::StateMachine || Kind == EAssetBrowserKind::BehaviorTree || Kind == EAssetBrowserKind::PartsCatalog;
		case 7:
			return Kind == EAssetBrowserKind::Texture;
		case 8:
			return Kind == EAssetBrowserKind::Model;
		default:
			return Kind == EAssetBrowserKind::Sound;
		}
	}

	void Rescan( FAssetBrowserState& State, bool bForce )
	{
		CAssetSubsystem* Assets = GetAppSubsystem<CAssetSubsystem>();
		if ( Assets == nullptr )
		{
			return;
		}
		const FString Root = Assets->GetRootDirectory();
		const double Now = ImGui::GetTime();
		if ( !bForce && Root == State.ScannedRoot && Now - State.LastScanTime < 3.0 )
		{
			return;
		}
		State.LastScanTime = Now;
		State.ScannedRoot = Root;
		TArray<FString> Extensions;
		for ( const FKindInfo& Info : KindTable )
		{
			Extensions.Add( Info.Extension );
		}
		TArray<FString> Files = NFileSystem::ListFilesRecursive( Root, Extensions );
		NAlgo::Sort( Files.GetData(), Files.Num() );
		State.Entries.Reset();
		for ( const FString& File : Files )
		{
			FAssetBrowserEntry Entry;
			Entry.Path = File;
			if ( NDebugUI::TryGetAssetBrowserKind( File, Entry.Kind ) )
			{
				const TOptional<FString> Full = Assets->ResolvePath( File );
				Entry.Size = Full.IsSet() ? NFileSystem::GetFileSize( Full.Get( FString() ) ) : 0;
				State.Entries.Add( Entry );
			}
		}
	}

	void Open( FAssetBrowserState& State, const TCallable<bool( const FString& )>& OnOpen, const FString& Path )
	{
		State.SelectedPath = Path;
		if ( !OnOpen || !OnOpen( Path ) )
		{
			State.StatusMessage = "Could not open '" + Path + "' (it has no editor, or the editor has unsaved changes: see its window).";
			State.bStatusIsError = true;
		}
		else
		{
			State.StatusMessage = "Opened " + Path;
			State.bStatusIsError = false;
		}
	}
}

bool NDebugUI::TryGetAssetBrowserKind( const FString& Path, EAssetBrowserKind& OutKind )
{
	for ( const FKindInfo& Info : KindTable )
	{
		if ( Path.EndsWith( Info.Extension, ESearchCase::IgnoreCase ) )
		{
			OutKind = Info.Kind;
			return true;
		}
	}
	return false;
}

bool NDebugUI::HasEditor( EAssetBrowserKind Kind )
{
	return Kind == EAssetBrowserKind::SpriteSheet || Kind == EAssetBrowserKind::ModelAnimation || Kind == EAssetBrowserKind::World || Kind == EAssetBrowserKind::Effect
		|| Kind == EAssetBrowserKind::Blackboard || Kind == EAssetBrowserKind::StateMachine || Kind == EAssetBrowserKind::BehaviorTree;
}

const char* NDebugUI::ToString( EAssetBrowserKind Kind )
{
	switch ( Kind )
	{
	case EAssetBrowserKind::SpriteSheet:
		return "Sprite sheet";
	case EAssetBrowserKind::ModelAnimation:
		return "Model animation";
	case EAssetBrowserKind::World:
		return "World";
	case EAssetBrowserKind::Effect:
		return "Effect";
	case EAssetBrowserKind::Blackboard:
		return "Blackboard";
	case EAssetBrowserKind::StateMachine:
		return "State machine";
	case EAssetBrowserKind::BehaviorTree:
		return "Behavior tree";
	case EAssetBrowserKind::PartsCatalog:
		return "AI parts catalog";
	case EAssetBrowserKind::Texture:
		return "Texture";
	case EAssetBrowserKind::Model:
		return "Model";
	default:
		return "Sound";
	}
}

void NDebugUI::DrawAssetBrowser( FAssetBrowserState& State, const TCallable<bool( const FString& )>& OnOpen, bool* pOpen )
{
	const ImGuiViewport* MainViewport = ImGui::GetMainViewport();
	ImGui::SetNextWindowPos( ImVec2( MainViewport->Pos.x + 20.0f, MainViewport->Pos.y + 44.0f ), ImGuiCond_FirstUseEver );
	ImGui::SetNextWindowSize( ImVec2( NMath::Min( 560.0f, MainViewport->Size.x - 40.0f ), NMath::Min( 640.0f, MainViewport->Size.y - 60.0f ) ), ImGuiCond_FirstUseEver );
	if ( !ImGui::Begin( "Project", pOpen ) )
	{
		ImGui::End();
		return;
	}
	CAssetSubsystem* Assets = GetAppSubsystem<CAssetSubsystem>();
	if ( Assets == nullptr )
	{
		ImGui::TextUnformatted( "The asset manager is not running." );
		ImGui::End();
		return;
	}
	Rescan( State, false );

	ImGui::TextDisabled( "%s", UI_TEXT( Assets->GetRootDirectory() ) );
	ImGui::SetNextItemWidth( 200.0f );
	NDebugUI::InputStringWithHint( "##browserfilter", "filter by name", State.Filter );
	ImGui::SameLine();
	ImGui::SetNextItemWidth( 150.0f );
	ImGui::Combo( "##browsercategory", &State.Category, "All\0With an editor\0Sprite sheets\0Animations\0Worlds\0Effects\0AI\0Textures\0Models\0Sounds\0" );
	ImGui::SameLine();
	if ( ImGui::Button( "Rescan" ) )
	{
		Rescan( State, true );
	}

	// 一覧
	int32 NumShown = 0;
	const float ListHeight = NMath::Max( ImGui::GetContentRegionAvail().y - 46.0f, 100.0f );
	if ( ImGui::BeginTable( "##browsertable", 3, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable, ImVec2( 0.0f, ListHeight ) ) )
	{
		ImGui::TableSetupScrollFreeze( 0, 1 );
		ImGui::TableSetupColumn( "Path", ImGuiTableColumnFlags_WidthStretch, 3.0f );
		ImGui::TableSetupColumn( "Type", ImGuiTableColumnFlags_WidthFixed, 110.0f );
		ImGui::TableSetupColumn( "Size", ImGuiTableColumnFlags_WidthFixed, 70.0f );
		ImGui::TableHeadersRow();
		for ( int32 Index = 0; Index < State.Entries.Num(); ++Index )
		{
			const FAssetBrowserEntry& Entry = State.Entries[Index];
			if ( !InCategory( State.Category, Entry.Kind ) || ( !State.Filter.IsEmpty() && !Entry.Path.Contains( State.Filter, ESearchCase::IgnoreCase ) ) )
			{
				continue;
			}
			++NumShown;
			ImGui::PushID( Index );
			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex( 0 );
			const bool bEditable = NDebugUI::HasEditor( Entry.Kind );
			if ( !bEditable )
			{
				ImGui::PushStyleColor( ImGuiCol_Text, ImGui::GetStyleColorVec4( ImGuiCol_TextDisabled ) );
			}
			const bool bSelected = Entry.Path == State.SelectedPath;
			if ( ImGui::Selectable( UI_TEXT( Entry.Path ), bSelected, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowDoubleClick ) )
			{
				State.SelectedPath = Entry.Path;
				if ( ImGui::IsMouseDoubleClicked( ImGuiMouseButton_Left ) && bEditable )
				{
					Open( State, OnOpen, Entry.Path );
				}
			}
			if ( !bEditable )
			{
				ImGui::PopStyleColor();
			}
			if ( ImGui::BeginPopupContextItem( "##browsercontext" ) )
			{
				if ( ImGui::MenuItem( "Open in editor", nullptr, false, bEditable ) )
				{
					Open( State, OnOpen, Entry.Path );
				}
				if ( ImGui::MenuItem( "Copy path" ) )
				{
					ImGui::SetClipboardText( *Entry.Path );
				}
				ImGui::EndPopup();
			}
			ImGui::TableSetColumnIndex( 1 );
			ImGui::TextColored( KindColor( Entry.Kind ), "%s", NDebugUI::ToString( Entry.Kind ) );
			ImGui::TableSetColumnIndex( 2 );
			ImGui::TextUnformatted( UI_TEXT( NDebugUI::FormatBytes( Entry.Size ) ) );
			ImGui::PopID();
		}
		ImGui::EndTable();
	}
	ImGui::TextDisabled( "%d file(s) shown. Double-click a file to open it in its editor (right-click for more).", NumShown );
	if ( !State.StatusMessage.IsEmpty() )
	{
		if ( State.bStatusIsError )
		{
			ImGui::TextColored( ImVec4( 1.0f, 0.5f, 0.45f, 1.0f ), "%s", UI_TEXT( State.StatusMessage ) );
		}
		else
		{
			ImGui::TextDisabled( "%s", UI_TEXT( State.StatusMessage ) );
		}
	}
	ImGui::End();
}
