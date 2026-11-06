// SPDX-License-Identifier: MIT
#include "AIEditorInternal.h"

#include "Library/Core/Types/CString.h"
#include "Library/DebugUI/Common/EditorCommon.h"
#include "Library/Math/MathUtils.h"

//=============================================================================
// ブラックボードのタブ
//   キーの一覧を表で編集する（名前・型・既定値・Trigger / Enum の選択肢・使われている数・並べ替えと削除）。
//   名前は入力が終わったとき（Enter かフォーカスが外れたとき）に、改名して、開いているステートマシン・ツリーの参照も直す。
//=============================================================================
namespace
{
	using FState = FAIEditorState;

	constexpr int32 NumKeyTypes = 7;

	EBlackboardKeyType KeyTypeAt( int32 Index )
	{
		return static_cast<EBlackboardKeyType>( Index );
	}

	// 既定値の入力欄。変更されたら新しい値を Out に入れて true
	bool DrawDefaultInput( const FBlackboardKeyDefinition& Key, FBlackboardValue& Out )
	{
		Out = Key.Default;
		switch ( Key.Type )
		{
		case EBlackboardKeyType::Bool:
		{
			bool bValue = Key.Default.Integer != 0;
			if ( ImGui::Checkbox( "##default", &bValue ) )
			{
				Out = FBlackboardValue::MakeBool( bValue );
				return true;
			}
			return false;
		}
		case EBlackboardKeyType::Int:
		{
			int Value = static_cast<int>( Key.Default.Integer );
			if ( ImGui::InputInt( "##default", &Value ) )
			{
				Out = FBlackboardValue::MakeInt( Value );
				return true;
			}
			return false;
		}
		case EBlackboardKeyType::Float:
		{
			float Value = Key.Default.Real;
			if ( ImGui::DragFloat( "##default", &Value, 0.05f ) )
			{
				Out = FBlackboardValue::MakeFloat( Value );
				return true;
			}
			return false;
		}
		case EBlackboardKeyType::Vector3:
		{
			float Value[3] = { Key.Default.Vector.x, Key.Default.Vector.y, Key.Default.Vector.z };
			if ( ImGui::DragFloat3( "##default", Value, 0.05f ) )
			{
				Out = FBlackboardValue::MakeVector( FVector3( Value[0], Value[1], Value[2] ) );
				return true;
			}
			return false;
		}
		case EBlackboardKeyType::Name:
		{
			FString Value = Key.Default.Text;
			if ( NDebugUI::InputString( "##default", Value ) )
			{
				Out = FBlackboardValue::MakeName( Value );
				return true;
			}
			return false;
		}
		case EBlackboardKeyType::Object:
			ImGui::TextDisabled( "(set at run time)" );
			return false;
		case EBlackboardKeyType::Enum:
		{
			if ( Key.EnumValues.IsEmpty() )
			{
				ImGui::TextDisabled( "(add values first)" );
				return false;
			}
			const int32 Selected = static_cast<int32>( NMath::Clamp<int64>( Key.Default.Integer, 0, Key.EnumValues.Num() - 1 ) );
			bool bChanged = false;
			if ( ImGui::BeginCombo( "##default", UI_TEXT( Key.EnumValues[Selected] ) ) )
			{
				for ( int32 Index = 0; Index < Key.EnumValues.Num(); ++Index )
				{
					if ( ImGui::Selectable( UI_TEXT( Key.EnumValues[Index] ), Index == Selected ) )
					{
						Out = FBlackboardValue::MakeEnum( Index );
						bChanged = true;
					}
				}
				ImGui::EndCombo();
			}
			return bChanged;
		}
		}
		return false;
	}

	// Enum の選択肢の編集（ポップアップ）
	void DrawEnumPopup( FState& S, int32 KeyIndex )
	{
		if ( !ImGui::BeginPopup( "EnumValues" ) )
		{
			return;
		}
		if ( KeyIndex < 0 || KeyIndex >= S.Blackboard.Get().Keys.Num() )
		{
			ImGui::EndPopup();
			return;
		}
		ImGui::TextUnformatted( UI_TEXT( "Values of " + S.Blackboard.Get().Keys[KeyIndex].Name ) );
		int32 RemoveIndex = INDEX_NONE;
		const int32 NumValues = S.Blackboard.Get().Keys[KeyIndex].EnumValues.Num();
		for ( int32 Index = 0; Index < NumValues; ++Index )
		{
			ImGui::PushID( Index );
			FString Text = S.Blackboard.Get().Keys[KeyIndex].EnumValues[Index];
			ImGui::SetNextItemWidth( 180.0f );
			if ( NDebugUI::InputString( "##value", Text ) )
			{
				// 入力のたびに改名すると、名前の重複の補正で、入力途中の文字が変わってしまうので、確定したときに反映する
				S.NameBuffer = Text;
				S.NameBufferKey = 1000000 + Index;
			}
			if ( ImGui::IsItemDeactivatedAfterEdit() && S.NameBufferKey == 1000000 + Index )
			{
				NBlackboardEdit::RenameEnumValue( S.Blackboard, KeyIndex, Index, S.NameBuffer );
				S.NameBufferKey = INDEX_NONE;
			}
			ImGui::SameLine();
			if ( ImGui::SmallButton( "x" ) )
			{
				RemoveIndex = Index;
			}
			ImGui::PopID();
		}
		if ( RemoveIndex != INDEX_NONE )
		{
			NBlackboardEdit::RemoveEnumValue( S.Blackboard, KeyIndex, RemoveIndex );
		}
		if ( ImGui::Button( "+ Add value" ) )
		{
			NBlackboardEdit::AddEnumValue( S.Blackboard, KeyIndex, "Value" );
		}
		ImGui::EndPopup();
	}
}

void NAIEditor::DrawBlackboardTab( FAIEditorState& S )
{
	// キーを足す
	ImGui::SetNextItemWidth( 180.0f );
	NDebugUI::InputStringWithHint( "##newkey", "key name", S.NewKeyName );
	ImGui::SameLine();
	ImGui::SetNextItemWidth( 110.0f );
	if ( ImGui::BeginCombo( "##newtype", NBlackboard::ToString( KeyTypeAt( S.NewKeyType ) ) ) )
	{
		for ( int32 Index = 0; Index < NumKeyTypes; ++Index )
		{
			if ( ImGui::Selectable( NBlackboard::ToString( KeyTypeAt( Index ) ), Index == S.NewKeyType ) )
			{
				S.NewKeyType = Index;
			}
		}
		ImGui::EndCombo();
	}
	ImGui::SameLine();
	if ( ImGui::Button( "Add key" ) )
	{
		S.SelectedKey = NBlackboardEdit::AddKey( S.Blackboard, S.NewKeyName, KeyTypeAt( S.NewKeyType ) );
	}
	ImGui::SameLine();
	ImGui::TextDisabled( "%d keys.  Names apply when you press Enter or leave the field.", S.Blackboard.Get().Keys.Num() );

	int32 RemoveIndex = INDEX_NONE;
	int32 MoveFrom = INDEX_NONE;
	int32 MoveTo = INDEX_NONE;
	int32 EnumPopupKey = INDEX_NONE;
	const float TableHeight = NMath::Max( ImGui::GetContentRegionAvail().y - 150.0f, 120.0f );
	if ( ImGui::BeginTable( "##keys", 7, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable, ImVec2( 0.0f, TableHeight ) ) )
	{
		ImGui::TableSetupScrollFreeze( 0, 1 );
		ImGui::TableSetupColumn( "#", ImGuiTableColumnFlags_WidthFixed, 28.0f );
		ImGui::TableSetupColumn( "Name", ImGuiTableColumnFlags_WidthStretch, 1.4f );
		ImGui::TableSetupColumn( "Type", ImGuiTableColumnFlags_WidthFixed, 100.0f );
		ImGui::TableSetupColumn( "Default", ImGuiTableColumnFlags_WidthStretch, 1.6f );
		ImGui::TableSetupColumn( "Options", ImGuiTableColumnFlags_WidthFixed, 110.0f );
		ImGui::TableSetupColumn( "Used", ImGuiTableColumnFlags_WidthFixed, 44.0f );
		ImGui::TableSetupColumn( "", ImGuiTableColumnFlags_WidthFixed, 96.0f );
		ImGui::TableHeadersRow();

		const int32 NumKeys = S.Blackboard.Get().Keys.Num();
		for ( int32 Index = 0; Index < NumKeys; ++Index )
		{
			const FBlackboardKeyDefinition& Key = S.Blackboard.Get().Keys[Index];
			ImGui::PushID( Index );
			ImGui::TableNextRow();

			ImGui::TableSetColumnIndex( 0 );
			ImGui::TextDisabled( "%d", Index );

			// 名前
			ImGui::TableSetColumnIndex( 1 );
			FString Text = S.NameBufferKey == Index ? S.NameBuffer : Key.Name;
			const FString OldName = Key.Name;
			ImGui::SetNextItemWidth( -FLT_MIN );
			if ( NDebugUI::InputString( "##name", Text ) )
			{
				S.NameBuffer = Text;
				S.NameBufferKey = Index;
				S.SelectedKey = Index;
			}
			if ( ImGui::IsItemDeactivatedAfterEdit() && S.NameBufferKey == Index )
			{
				const FString NewName = NBlackboardEdit::RenameKey( S.Blackboard, Index, S.NameBuffer );
				NAIEditor::PropagateKeyRename( S, OldName, NewName );
				S.NameBufferKey = INDEX_NONE;
			}

			// 型
			ImGui::TableSetColumnIndex( 2 );
			ImGui::SetNextItemWidth( -FLT_MIN );
			if ( ImGui::BeginCombo( "##type", NBlackboard::ToString( Key.Type ) ) )
			{
				for ( int32 TypeIndex = 0; TypeIndex < NumKeyTypes; ++TypeIndex )
				{
					if ( ImGui::Selectable( NBlackboard::ToString( KeyTypeAt( TypeIndex ) ), KeyTypeAt( TypeIndex ) == Key.Type ) )
					{
						NBlackboardEdit::SetKeyType( S.Blackboard, Index, KeyTypeAt( TypeIndex ) );
					}
				}
				ImGui::EndCombo();
			}

			// 既定値
			ImGui::TableSetColumnIndex( 3 );
			ImGui::SetNextItemWidth( -FLT_MIN );
			FBlackboardValue NewDefault;
			if ( DrawDefaultInput( S.Blackboard.Get().Keys[Index], NewDefault ) )
			{
				char CoalesceKey[32];
				NCString::Format( CoalesceKey, sizeof( CoalesceKey ), "default%d", Index );
				NBlackboardEdit::SetKeyDefault( S.Blackboard, Index, NewDefault, CoalesceKey );
			}

			// Trigger / Enum の選択肢
			ImGui::TableSetColumnIndex( 4 );
			const FBlackboardKeyDefinition& Current = S.Blackboard.Get().Keys[Index];
			if ( Current.Type == EBlackboardKeyType::Bool )
			{
				bool bTrigger = Current.bTrigger;
				if ( ImGui::Checkbox( "Trigger", &bTrigger ) )
				{
					NBlackboardEdit::SetKeyTrigger( S.Blackboard, Index, bTrigger );
				}
				if ( ImGui::IsItemHovered() )
				{
					ImGui::SetTooltip( "A trigger is true for one frame only (like a button press), then becomes false." );
				}
			}
			else if ( Current.Type == EBlackboardKeyType::Enum )
			{
				if ( ImGui::SmallButton( UI_TEXT( FString::Printf( "Values (%d)", Current.EnumValues.Num() ) ) ) )
				{
					EnumPopupKey = Index;
				}
			}

			// 使われている数（開いているステートマシン・ツリーのうち、このブラックボードを指すもの）
			ImGui::TableSetColumnIndex( 5 );
			const int32 Used = NAIEditor::CountKeyUsage( S, Current.Name );
			if ( Used > 0 )
			{
				ImGui::Text( "%d", Used );
			}
			else
			{
				ImGui::TextDisabled( "-" );
			}

			// 並べ替えと削除
			ImGui::TableSetColumnIndex( 6 );
			if ( ImGui::SmallButton( "^" ) && Index > 0 )
			{
				MoveFrom = Index;
				MoveTo = Index - 1;
			}
			ImGui::SameLine();
			if ( ImGui::SmallButton( "v" ) && Index + 1 < NumKeys )
			{
				MoveFrom = Index;
				MoveTo = Index + 1;
			}
			ImGui::SameLine();
			if ( ImGui::SmallButton( "x" ) )
			{
				RemoveIndex = Index;
			}
			ImGui::PopID();
		}
		ImGui::EndTable();
	}

	if ( EnumPopupKey != INDEX_NONE )
	{
		S.SelectedKey = EnumPopupKey;
		ImGui::OpenPopup( "EnumValues" );
	}
	DrawEnumPopup( S, S.SelectedKey );

	if ( MoveFrom != INDEX_NONE )
	{
		NBlackboardEdit::MoveKey( S.Blackboard, MoveFrom, MoveTo );
		S.SelectedKey = MoveTo;
	}
	if ( RemoveIndex != INDEX_NONE )
	{
		if ( NAIEditor::CountKeyUsage( S, S.Blackboard.Get().Keys[RemoveIndex].Name ) > 0 )
		{
			S.PendingRemoveKey = RemoveIndex;
			ImGui::OpenPopup( "Remove key?" );
		}
		else
		{
			NBlackboardEdit::RemoveKey( S.Blackboard, RemoveIndex );
		}
	}
	if ( ImGui::BeginPopupModal( "Remove key?", nullptr, ImGuiWindowFlags_AlwaysAutoResize ) )
	{
		if ( S.PendingRemoveKey >= 0 && S.PendingRemoveKey < S.Blackboard.Get().Keys.Num() )
		{
			const FString& Name = S.Blackboard.Get().Keys[S.PendingRemoveKey].Name;
			ImGui::TextUnformatted( UI_TEXT( FString::Printf( "'%s' is used by %d condition(s) in the open state machine / behavior tree.", *Name, NAIEditor::CountKeyUsage( S, Name ) ) ) );
			ImGui::TextUnformatted( "Those conditions will point to a key that no longer exists (shown in Issues)." );
		}
		if ( ImGui::Button( "Remove" ) )
		{
			NBlackboardEdit::RemoveKey( S.Blackboard, S.PendingRemoveKey );
			S.PendingRemoveKey = INDEX_NONE;
			ImGui::CloseCurrentPopup();
		}
		ImGui::SameLine();
		if ( ImGui::Button( "Cancel" ) )
		{
			S.PendingRemoveKey = INDEX_NONE;
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndPopup();
	}

	// ドラッグなどの連続した変更の区切り（どの項目も操作されていないとき）
	if ( !ImGui::IsAnyItemActive() )
	{
		S.Blackboard.EndEditSession();
	}

	ImGui::Separator();
	NAIEditor::DrawIssues( S );
}
