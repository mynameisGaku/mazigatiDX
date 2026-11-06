// SPDX-License-Identifier: MIT
#include "AIEditorInternal.h"

#include "Library/DebugUI/Common/EditorCommon.h"
#include "Library/Framework/AI/Registry/AIRegistry.h"

//=============================================================================
// 部品（登録した Task / Condition / Service / ステートの処理）の呼び出しの編集
//   名前の一覧と、パラメータの入力欄は、レジストリのスキーマから自動で作る（部品ごとの画面のコードは書かない）。
//=============================================================================
namespace
{
	using EPartKind = NAIEditor::EPartKind;

	struct FPartInfo
	{
		FString Name;
		FString Category;
		FString Description;
		const FParamSchema* Schema = nullptr;
	};

	TArray<FPartInfo> ListParts( EPartKind Kind )
	{
		TArray<FPartInfo> Parts;
		const CAIRegistry& Registry = NAI::GetRegistry();
		const auto AddAll = [&Parts]( const auto& Entries )
		{
			for ( const auto& Entry : Entries )
			{
				FPartInfo Info;
				Info.Name = Entry.Name;
				Info.Category = Entry.Category;
				Info.Description = Entry.Description;
				Info.Schema = &Entry.Schema;
				Parts.Add( Info );
			}
		};
		switch ( Kind )
		{
		case EPartKind::StateAction:
			AddAll( Registry.GetStateActions() );
			break;
		case EPartKind::Condition:
			AddAll( Registry.GetConditions() );
			break;
		case EPartKind::Task:
			AddAll( Registry.GetTasks() );
			break;
		case EPartKind::Service:
			AddAll( Registry.GetServices() );
			break;
		}
		return Parts;
	}

	// 名前（別名でもよい）の部品のスキーマと説明。無ければ nullptr
	const FParamSchema* FindSchema( EPartKind Kind, const FString& Name, const FString** OutDescription )
	{
		const CAIRegistry& Registry = NAI::GetRegistry();
		switch ( Kind )
		{
		case EPartKind::StateAction:
			if ( const CAIRegistry::FStateActionEntry* Entry = Registry.FindStateAction( Name ) )
			{
				*OutDescription = &Entry->Description;
				return &Entry->Schema;
			}
			break;
		case EPartKind::Condition:
			if ( const CAIRegistry::FConditionEntry* Entry = Registry.FindCondition( Name ) )
			{
				*OutDescription = &Entry->Description;
				return &Entry->Schema;
			}
			break;
		case EPartKind::Task:
			if ( const CAIRegistry::FTaskEntry* Entry = Registry.FindTask( Name ) )
			{
				*OutDescription = &Entry->Description;
				return &Entry->Schema;
			}
			break;
		case EPartKind::Service:
			if ( const CAIRegistry::FServiceEntry* Entry = Registry.FindService( Name ) )
			{
				*OutDescription = &Entry->Description;
				return &Entry->Schema;
			}
			break;
		}
		return nullptr;
	}
}

bool NAIEditor::DrawCallEditor( FAIEditorState& State, const char* Label, FAICall& Call, EPartKind Kind, const FString& BlackboardPath )
{
	bool bChanged = false;
	ImGui::PushID( Label );
	ImGui::AlignTextToFramePadding();
	ImGui::TextUnformatted( Label );
	ImGui::SameLine( 130.0f );
	ImGui::SetNextItemWidth( -FLT_MIN );

	TArray<FString> Names;
	for ( const FPartInfo& Info : ListParts( Kind ) )
	{
		Names.Add( Info.Name );
	}
	FString Name = Call.Do;
	if ( NDebugUI::DrawNameCombo( "##part", Name, Names, "(choose)" ) )
	{
		Call.Do = Name;
		Call.Params = NlohmannJson();
		bChanged = true;
	}

	const FString* Description = nullptr;
	const FParamSchema* Schema = Call.Do.IsEmpty() ? nullptr : FindSchema( Kind, Call.Do, &Description );
	if ( !Call.Do.IsEmpty() && Schema == nullptr )
	{
		ImGui::TextColored( ImVec4( 1.0f, 0.45f, 0.4f, 1.0f ), "'%s' is not registered (the game will skip it).", UI_TEXT( Call.Do ) );
	}
	else if ( Schema != nullptr )
	{
		if ( Description != nullptr && !Description->IsEmpty() )
		{
			ImGui::TextDisabled( "%s", UI_TEXT( *Description ) );
		}
		ImGui::Indent( 8.0f );
		bChanged |= NDebugUI::DrawParamInspector( *Schema, Call.Params, MakeInspectContext( State, BlackboardPath ) );
		ImGui::Unindent( 8.0f );
	}
	ImGui::PopID();
	return bChanged;
}

FString NAIEditor::DrawPartMenuItems( EPartKind Kind )
{
	const TArray<FPartInfo> Parts = ListParts( Kind );
	if ( Parts.IsEmpty() )
	{
		ImGui::TextDisabled( "(no parts are registered)" );
		return FString();
	}
	// カテゴリ（登録順。空は General）
	TArray<FString> Categories;
	for ( const FPartInfo& Info : Parts )
	{
		Categories.AddUnique( Info.Category.IsEmpty() ? FString( "General" ) : Info.Category );
	}
	FString Chosen;
	for ( const FString& Category : Categories )
	{
		if ( !ImGui::BeginMenu( UI_TEXT( Category ) ) )
		{
			continue;
		}
		for ( const FPartInfo& Info : Parts )
		{
			if ( ( Info.Category.IsEmpty() ? FString( "General" ) : Info.Category ) != Category )
			{
				continue;
			}
			if ( ImGui::MenuItem( UI_TEXT( Info.Name ) ) )
			{
				Chosen = Info.Name;
			}
			if ( !Info.Description.IsEmpty() && ImGui::IsItemHovered() )
			{
				ImGui::SetTooltip( "%s", UI_TEXT( Info.Description ) );
			}
		}
		ImGui::EndMenu();
	}
	return Chosen;
}
