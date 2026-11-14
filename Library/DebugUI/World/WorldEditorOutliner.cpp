#include "Library/Core/Collection/Map.h"
#include "Library/Core/Types/Numeric.h"
#include "Library/DebugUI/Common/EditorCommon.h"
#include "Library/DebugUI/Asset/AssetBrowser.h"
#include "Library/DebugUI/Common/ImGuiTexture.h"
#include "Library/DebugUI/World/WorldEditorInternal.h"
#include "Library/DebugUI/World/WorldEditorModelMaterialPanel.h"
#include "Library/Framework/Asset/AssetSubsystem.h"
#include "Library/Framework/World/Editor/EditorOutliner.h"
#include "Library/Framework/World/Runtime/WorldCollision.h"
#include "Library/Framework/World/Definition/WorldHierarchy.h"

//=============================================================================
// ワールドエディタのアウトライナー（オブジェクトの一覧。選択・並べ替え・親子の付け替え）
//=============================================================================
namespace
{
	using FState = FWorldEditorState;
	using NWorldEditorUI::ObjectLabel;

	EWorldIssueSeverity WorstSeverity( const TArray<FWorldIssue>& Issues, int32 ObjectId, bool& bOutAny )
	{
		bOutAny = false;
		EWorldIssueSeverity Worst = EWorldIssueSeverity::Warning;
		for ( const FWorldIssue& Issue : Issues )
		{
			if ( Issue.ObjectId != ObjectId )
			{
				continue;
			}
			bOutAny = true;
			if ( Issue.Severity == EWorldIssueSeverity::Error )
			{
				Worst = EWorldIssueSeverity::Error;
			}
		}
		return Worst;
	}

	//=========================================================================
	// アウトライナー
	//=========================================================================
	CWorldEditorSession::ESelectMode GetSelectMode( const ImGuiIO& IO )
	{
		if ( IO.KeyCtrl )
		{
			return CWorldEditorSession::ESelectMode::Toggle;
		}
		return CWorldEditorSession::ESelectMode::Replace;
	}

	FString ComponentLetters( const FWorldObject& Object )
	{
		FString Letters;
		if ( Object.Model.IsSet() )
		{
			Letters += "M";
		}
		if ( Object.Mesh.IsSet() )
		{
			Letters += "G";
		}
		if ( Object.Prefab.IsSet() )
		{
			Letters += "P";
		}
		if ( Object.Billboard.IsSet() )
		{
			Letters += "B";
		}
		if ( Object.Sound.IsSet() )
		{
			Letters += "S";
		}
		if ( Object.Effect.IsSet() )
		{
			Letters += "E";
		}
		if ( Object.Collider.IsSet() )
		{
			Letters += "C";
		}
		if ( !Object.RawComponents.IsEmpty() )
		{
			Letters += "?";
		}
		return Letters;
	}

	void DrawRowContextMenu( FState& S, const FWorldObject& Object )
	{
		CWorldEditorSession& Session = S.Session;
		if ( !ImGui::BeginPopupContextItem( "##RowMenu" ) )
		{
			return;
		}
		if ( !Session.IsSelected( Object.Id ) )
		{
			Session.Select( Object.Id );
		}
		if ( ImGui::MenuItem( "Add empty child" ) )
		{
			Session.CreateEmpty( NWorld::ComputeWorldTransform( Session.Document.Get(), Object.Id ).Position, "Empty", Object.Id );
		}
		ImGui::Separator();
		if ( ImGui::MenuItem( "Focus", "F" ) )
		{
			Session.FocusSelection( S.View, S.AssetInfo.Get() );
		}
		if ( ImGui::MenuItem( "Duplicate", "Ctrl+D" ) )
		{
			Session.DuplicateSelection();
		}
		if ( ImGui::MenuItem( "Copy", "Ctrl+C" ) )
		{
			Session.CopySelection();
		}
		if ( ImGui::MenuItem( "Paste as child", "", false, Session.CanPaste() ) )
		{
			Session.Paste( FVector3::Zero, Object.Id );
		}
		ImGui::Separator();
		if ( ImGui::MenuItem( "Group", "Ctrl+G" ) )
		{
			Session.GroupSelection();
		}
		if ( ImGui::MenuItem( "Ungroup", "Ctrl+Shift+G" ) )
		{
			Session.UngroupSelection();
		}
		ImGui::Separator();
		if ( ImGui::MenuItem( "Delete", "Del" ) )
		{
			Session.DeleteSelection();
		}
		ImGui::EndPopup();
	}

	// 行へのドロップ: オブジェクトの付け替え（手前 / 中 / 後ろ）と、アセットを子として置く
	void HandleRowDrop( FState& S, const FWorldObject& Object, const ImVec2& RowMin, const ImVec2& RowMax )
	{
		CWorldEditorSession& Session = S.Session;
		if ( !ImGui::BeginDragDropTarget() )
		{
			return;
		}
		ImDrawList* DL = ImGui::GetWindowDrawList();
		const float32 RowHeight = RowMax.y - RowMin.y;
		const float32 MouseY = ImGui::GetIO().MousePos.y;
		NWorldEdit::EWorldDropPosition Position = NWorldEdit::EWorldDropPosition::Inside;
		if ( MouseY < RowMin.y + RowHeight * 0.25f )
		{
			Position = NWorldEdit::EWorldDropPosition::Before;
		}
		else if ( MouseY > RowMax.y - RowHeight * 0.25f )
		{
			Position = NWorldEdit::EWorldDropPosition::After;
		}

		if ( const ImGuiPayload* Payload = ImGui::AcceptDragDropPayload( NWorldEditorUI::ObjectPayloadType, ImGuiDragDropFlags_AcceptBeforeDelivery | ImGuiDragDropFlags_AcceptNoDrawDefaultRect ) )
		{
			const bool bAllowed = NWorldEdit::CanMoveObjects( Session.Document.Get(), Session.GetSelection(), Object.Id, Position );
			const ImU32 Color = bAllowed ? IM_COL32( 255, 215, 60, 255 ) : IM_COL32( 230, 70, 60, 255 );
			if ( Position == NWorldEdit::EWorldDropPosition::Inside )
			{
				DL->AddRect( RowMin, RowMax, Color, 0.0f, 0, 2.0f );
			}
			else
			{
				const float32 Y = Position == NWorldEdit::EWorldDropPosition::Before ? RowMin.y : RowMax.y;
				DL->AddLine( ImVec2( RowMin.x, Y ), ImVec2( RowMax.x, Y ), Color, 2.0f );
			}
			if ( Payload->IsDelivery() && bAllowed )
			{
				Session.MoveSelection( Object.Id, Position );
			}
		}
		else if ( const ImGuiPayload* AssetPayload = ImGui::AcceptDragDropPayload( NWorldEditorUI::AssetPayloadType, ImGuiDragDropFlags_AcceptBeforeDelivery | ImGuiDragDropFlags_AcceptNoDrawDefaultRect ) )
		{
			DL->AddRect( RowMin, RowMax, IM_COL32( 255, 215, 60, 255 ), 0.0f, 0, 2.0f );
			if ( AssetPayload->IsDelivery() )
			{
				const FString Path( static_cast<const char*>( AssetPayload->Data ) );
				Session.CreateAsset( Path, NWorld::ComputeWorldTransform( Session.Document.Get(), Object.Id ).Position, S.AssetInfo.Get(), Object.Id );
			}
		}
		ImGui::EndDragDropTarget();
	}
}

void NWorldEditorUI::DrawOutliner( FWorldEditorState& S )
{
	if ( !BeginPanel( S, "World Outliner", &S.bShowOutliner, 0.60f, 0.04f, 0.39f, 0.33f ) )
	{
		return;
	}
	CWorldEditorSession& Session = S.Session;
	const FWorldDefinition& World = Session.Document.Get();
	const ImGuiIO& IO = ImGui::GetIO();

	ImGui::SetNextItemWidth( -ImGui::GetFrameHeight() * 2.0f - ImGui::GetStyle().ItemSpacing.x * 2.0f );
	NDebugUI::InputStringWithHint( "##Filter", "filter: name  tag:Enemy  comp:Model  #12", S.OutlinerFilter );
	ImGui::SameLine();
	if ( ImGui::Button( "x##ClearFilter" ) )
	{
		S.OutlinerFilter = FString();
	}
	ImGui::SameLine();
	if ( ImGui::Button( "+##AddEmpty" ) )
	{
		CreateEmptyAtView( S );
	}
	if ( ImGui::IsItemHovered() )
	{
		ImGui::SetTooltip( "Add an empty object in front of the camera" );
	}

	const TArray<FEditorOutlinerRow> Rows = NWorldEditor::BuildOutlinerRows( World, S.Collapsed, S.OutlinerFilter );
	ImGui::BeginChild( "##Rows", ImVec2( 0.0f, 0.0f ), ImGuiChildFlags_Borders );
	if ( Rows.IsEmpty() )
	{
		ImGui::TextDisabled( World.Objects.IsEmpty() ? "(empty world: drag a model, image or sound from World Assets)" : "(no object matches the filter)" );
	}

	const float32 RowHeight = ImGui::GetFrameHeight();
	const float32 Indent = 14.0f;
	ImDrawList* DL = ImGui::GetWindowDrawList();
	for ( int32 RowIndex = 0; RowIndex < Rows.Num(); ++RowIndex )
	{
		const FEditorOutlinerRow& Row = Rows[RowIndex];
		const FWorldObject* Object = World.FindObject( Row.ObjectId );
		if ( Object == nullptr )
		{
			continue;
		}
		ImGui::PushID( Row.ObjectId );

		const ImVec2 RowMin = ImGui::GetCursorScreenPos();
		const float32 RowWidth = ImGui::GetContentRegionAvail().x;
		const ImVec2 RowMax( RowMin.x + RowWidth, RowMin.y + RowHeight );
		const bool bSelected = Session.IsSelected( Row.ObjectId );
		if ( bSelected )
		{
			DL->AddRectFilled( RowMin, RowMax, Row.ObjectId == Session.GetPrimary() ? IM_COL32( 90, 120, 190, 150 ) : IM_COL32( 80, 100, 150, 100 ) );
		}

		// 畳む / 開く
		const float32 ArrowX = RowMin.x + static_cast<float32>( Row.Depth ) * Indent;
		if ( Row.bHasChildren )
		{
			ImGui::SetCursorScreenPos( ImVec2( ArrowX, RowMin.y ) );
			if ( ImGui::InvisibleButton( "##Arrow", ImVec2( Indent, RowHeight ) ) )
			{
				if ( S.Collapsed.Contains( Row.ObjectId ) )
				{
					S.Collapsed.Remove( Row.ObjectId );
				}
				else
				{
					S.Collapsed.Add( Row.ObjectId );
				}
			}
			const float32 CenterY = RowMin.y + RowHeight * 0.5f;
			if ( Row.bExpanded )
			{
				DL->AddTriangleFilled( ImVec2( ArrowX + 3.0f, CenterY - 3.0f ), ImVec2( ArrowX + 11.0f, CenterY - 3.0f ), ImVec2( ArrowX + 7.0f, CenterY + 4.0f ), IM_COL32( 210, 210, 220, 255 ) );
			}
			else
			{
				DL->AddTriangleFilled( ImVec2( ArrowX + 4.0f, CenterY - 5.0f ), ImVec2( ArrowX + 4.0f, CenterY + 5.0f ), ImVec2( ArrowX + 11.0f, CenterY ), IM_COL32( 210, 210, 220, 255 ) );
			}
		}

		// 有効 / 無効（右端）
		const float32 CheckSize = ImGui::GetFrameHeight();
		ImGui::SetCursorScreenPos( ImVec2( RowMax.x - CheckSize, RowMin.y ) );
		bool bEnabled = Object->bEnabled;
		if ( ImGui::Checkbox( "##Enabled", &bEnabled ) )
		{
			NWorldEdit::EditObject( Session.Document, Row.ObjectId, "Outliner.Enabled" )->bEnabled = bEnabled;
		}
		if ( ImGui::IsItemHovered() )
		{
			ImGui::SetTooltip( "Enabled (a disabled object and its children are not placed in the game)" );
		}

		// 名前の行
		const float32 NameX = ArrowX + Indent;
		ImGui::SetCursorScreenPos( ImVec2( NameX, RowMin.y ) );
		bool bAny = false;
		const EWorldIssueSeverity Severity = WorstSeverity( S.Issues, Row.ObjectId, bAny );
		const bool bDim = !Object->bEnabled || !Row.bMatch;
		if ( bDim )
		{
			ImGui::PushStyleColor( ImGuiCol_Text, ImGui::GetStyleColorVec4( ImGuiCol_TextDisabled ) );
		}
		const FString Label = ObjectLabel( *Object );
		const bool bClicked = ImGui::Selectable( UI_TEXT( Label ), false, ImGuiSelectableFlags_AllowDoubleClick, ImVec2( RowMax.x - CheckSize - NameX - 4.0f, RowHeight ) );
		if ( bDim )
		{
			ImGui::PopStyleColor();
		}
		const bool bItemHovered = ImGui::IsItemHovered();
		if ( bClicked )
		{
			if ( IO.KeyShift && S.LastClickedId != WorldNoId )
			{
				// 見えている行の、最後にクリックしたものから、ここまで
				int32 From = INDEX_NONE;
				for ( int32 i = 0; i < Rows.Num(); ++i )
				{
					if ( Rows[i].ObjectId == S.LastClickedId )
					{
						From = i;
					}
				}
				if ( From != INDEX_NONE )
				{
					TArray<int32> Range;
					for ( int32 i = NMath::Min( From, RowIndex ); i <= NMath::Max( From, RowIndex ); ++i )
					{
						Range.Add( Rows[i].ObjectId );
					}
					Session.SetSelection( Range );
				}
			}
			else
			{
				Session.Select( Row.ObjectId, GetSelectMode( IO ) );
				S.LastClickedId = Row.ObjectId;
			}
			if ( ImGui::IsMouseDoubleClicked( ImGuiMouseButton_Left ) )
			{
				Session.FocusSelection( S.View, S.AssetInfo.Get() );
			}
		}

		// 検証の印・部品の頭文字（名前の右）
		if ( bAny )
		{
			DL->AddCircleFilled( ImVec2( RowMax.x - CheckSize - 10.0f, RowMin.y + RowHeight * 0.5f ), 4.0f, Severity == EWorldIssueSeverity::Error ? IM_COL32( 235, 80, 70, 255 ) : IM_COL32( 235, 190, 60, 255 ) );
		}
		const FString Letters = ComponentLetters( *Object );
		if ( !Letters.IsEmpty() )
		{
			const ImVec2 TextSize = ImGui::CalcTextSize( UI_TEXT( Letters ) );
			DL->AddText( ImVec2( RowMax.x - CheckSize - 22.0f - TextSize.x, RowMin.y + ( RowHeight - TextSize.y ) * 0.5f ), IM_COL32( 140, 170, 220, 220 ), UI_TEXT( Letters ) );
		}

		// ドラッグ（選択を動かす）
		if ( ImGui::BeginDragDropSource( ImGuiDragDropFlags_None ) )
		{
			if ( !Session.IsSelected( Row.ObjectId ) )
			{
				Session.Select( Row.ObjectId );
			}
			int32 Dummy = Row.ObjectId;
			ImGui::SetDragDropPayload( NWorldEditorUI::ObjectPayloadType, &Dummy, sizeof( Dummy ) );
			ImGui::Text( "%d object(s)", Session.GetSelection().Num() );
			ImGui::EndDragDropSource();
		}
		HandleRowDrop( S, *Object, RowMin, RowMax );
		DrawRowContextMenu( S, *Object );
		(void)bItemHovered;

		ImGui::SetCursorScreenPos( ImVec2( RowMin.x, RowMax.y + ImGui::GetStyle().ItemSpacing.y * 0.0f ) );
		ImGui::Dummy( ImVec2( RowWidth, 0.0f ) );
		ImGui::PopID();
	}

	// 空きの場所: クリックで選択を外す。ここへオブジェクトをドロップすると最上位になる
	const ImVec2 Rest = ImGui::GetContentRegionAvail();
	if ( Rest.y > 4.0f )
	{
		ImGui::InvisibleButton( "##Rest", ImVec2( NMath::Max( Rest.x, 1.0f ), NMath::Max( Rest.y, 1.0f ) ) );
		if ( ImGui::IsItemClicked( ImGuiMouseButton_Left ) )
		{
			Session.ClearSelection();
		}
		if ( ImGui::BeginDragDropTarget() )
		{
			if ( const ImGuiPayload* Payload = ImGui::AcceptDragDropPayload( NWorldEditorUI::ObjectPayloadType ) )
			{
				(void)Payload;
				TWorldDocument::FBatch Batch( Session.Document );
				for ( const int32 Root : Session.GetSelectedRoots() )
				{
					NWorldEdit::Reparent( Session.Document, Root, WorldNoId, WorldNoId, true );
				}
			}
			ImGui::EndDragDropTarget();
		}
	}
	ImGui::EndChild();
	ImGui::End();
}
