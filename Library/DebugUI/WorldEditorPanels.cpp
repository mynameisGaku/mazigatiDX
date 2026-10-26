#include <algorithm>
#include <cmath>

#include "Library/Core/Collection/Map.h"
#include "Library/DebugUI/EditorCommon.h"
#include "Library/DebugUI/ImGuiTexture.h"
#include "Library/DebugUI/WorldEditorInternal.h"
#include "Library/DebugUI/WorldEditorModelMaterialPanel.h"
#include "Library/Framework/Asset/AssetManager.h"
#include "Library/Framework/World/Editor/EditorOutliner.h"
#include "Library/Framework/World/WorldCollision.h"
#include "Library/Framework/World/WorldHierarchy.h"

//=============================================================================
// ワールドエディタの各パネル（アウトライナー・インスペクター・アセット・問題と設定）
//   編集は NWorldEdit / CWorldEditorSession を呼ぶだけ。連続した変更（ドラッグ・文字入力）は、項目ごとに別のキーを渡して、1 回の編集にまとめる。
//=============================================================================
namespace
{
	using FState = FWorldEditorState;

	// パネルの窓を始める（最初の位置と大きさは、アプリの画面に対する割合）。閉じていれば false（End は呼ばなくてよい）
	bool BeginPanel( FState& S, const char* Title, bool* pOpen, float32 X, float32 Y, float32 Width, float32 Height )
	{
		NWorldEditorUI::SetNextWindowRect( X, Y, Width, Height );
		if ( !ImGui::Begin( Title, pOpen ) )
		{
			ImGui::End();
			return false;
		}
		if ( ImGui::IsWindowFocused( ImGuiFocusedFlags_RootAndChildWindows ) ) S.bAnyPanelFocused = true;
		return true;
	}

	EWorldIssueSeverity WorstSeverity( const TArray<FWorldIssue>& Issues, int32 ObjectId, bool& bOutAny )
	{
		bOutAny = false;
		EWorldIssueSeverity Worst = EWorldIssueSeverity::Warning;
		for ( const FWorldIssue& Issue : Issues )
		{
			if ( Issue.ObjectId != ObjectId ) continue;
			bOutAny = true;
			if ( Issue.Severity == EWorldIssueSeverity::Error ) Worst = EWorldIssueSeverity::Error;
		}
		return Worst;
	}

	FString ObjectLabel( const FWorldObject& Object )
	{
		return Object.Name.IsEmpty() ? FString::Printf( "(unnamed) #%d", Object.Id ) : FString::Printf( "%s  #%d", *Object.Name, Object.Id );
	}

	//=========================================================================
	// アウトライナー
	//=========================================================================
	CWorldEditorSession::ESelectMode GetSelectMode( const ImGuiIO& IO )
	{
		if ( IO.KeyCtrl ) return CWorldEditorSession::ESelectMode::Toggle;
		return CWorldEditorSession::ESelectMode::Replace;
	}

	FString ComponentLetters( const FWorldObject& Object )
	{
		FString Letters;
		if ( Object.Model.IsSet() ) Letters += "M";
		if ( Object.Mesh.IsSet() ) Letters += "G";
		if ( Object.Prefab.IsSet() ) Letters += "P";
		if ( Object.Billboard.IsSet() ) Letters += "B";
		if ( Object.Sound.IsSet() ) Letters += "S";
		if ( Object.Effect.IsSet() ) Letters += "E";
		if ( Object.Collider.IsSet() ) Letters += "C";
		if ( !Object.RawComponents.IsEmpty() ) Letters += "?";
		return Letters;
	}

	void DrawRowContextMenu( FState& S, const FWorldObject& Object )
	{
		CWorldEditorSession& Session = S.Session;
		if ( !ImGui::BeginPopupContextItem( "##RowMenu" ) ) return;
		if ( !Session.IsSelected( Object.Id ) ) Session.Select( Object.Id );
		if ( ImGui::MenuItem( "Add empty child" ) ) Session.CreateEmpty( NWorld::ComputeWorldTransform( Session.Document.Get(), Object.Id ).Position, "Empty", Object.Id );
		ImGui::Separator();
		if ( ImGui::MenuItem( "Focus", "F" ) ) Session.FocusSelection( S.View, S.AssetInfo.Get() );
		if ( ImGui::MenuItem( "Duplicate", "Ctrl+D" ) ) Session.DuplicateSelection();
		if ( ImGui::MenuItem( "Copy", "Ctrl+C" ) ) Session.CopySelection();
		if ( ImGui::MenuItem( "Paste as child", "", false, Session.CanPaste() ) ) Session.Paste( FVector3::Zero, Object.Id );
		ImGui::Separator();
		if ( ImGui::MenuItem( "Group", "Ctrl+G" ) ) Session.GroupSelection();
		if ( ImGui::MenuItem( "Ungroup", "Ctrl+Shift+G" ) ) Session.UngroupSelection();
		ImGui::Separator();
		if ( ImGui::MenuItem( "Delete", "Del" ) ) Session.DeleteSelection();
		ImGui::EndPopup();
	}

	// 行へのドロップ: オブジェクトの付け替え（手前 / 中 / 後ろ）と、アセットを子として置く
	void HandleRowDrop( FState& S, const FWorldObject& Object, const ImVec2& RowMin, const ImVec2& RowMax )
	{
		CWorldEditorSession& Session = S.Session;
		if ( !ImGui::BeginDragDropTarget() ) return;
		ImDrawList* DL = ImGui::GetWindowDrawList();
		const float32 RowHeight = RowMax.y - RowMin.y;
		const float32 MouseY = ImGui::GetIO().MousePos.y;
		NWorldEdit::EWorldDropPosition Position = NWorldEdit::EWorldDropPosition::Inside;
		if ( MouseY < RowMin.y + RowHeight * 0.25f ) Position = NWorldEdit::EWorldDropPosition::Before;
		else if ( MouseY > RowMax.y - RowHeight * 0.25f ) Position = NWorldEdit::EWorldDropPosition::After;

		if ( const ImGuiPayload* Payload = ImGui::AcceptDragDropPayload( NWorldEditorUI::ObjectPayloadType, ImGuiDragDropFlags_AcceptBeforeDelivery | ImGuiDragDropFlags_AcceptNoDrawDefaultRect ) )
		{
			const bool bAllowed = NWorldEdit::CanMoveObjects( Session.Document.Get(), Session.GetSelection(), Object.Id, Position );
			const ImU32 Color = bAllowed ? IM_COL32( 255, 215, 60, 255 ) : IM_COL32( 230, 70, 60, 255 );
			if ( Position == NWorldEdit::EWorldDropPosition::Inside ) DL->AddRect( RowMin, RowMax, Color, 0.0f, 0, 2.0f );
			else
			{
				const float32 Y = Position == NWorldEdit::EWorldDropPosition::Before ? RowMin.y : RowMax.y;
				DL->AddLine( ImVec2( RowMin.x, Y ), ImVec2( RowMax.x, Y ), Color, 2.0f );
			}
			if ( Payload->IsDelivery() && bAllowed ) Session.MoveSelection( Object.Id, Position );
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
	if ( !BeginPanel( S, "World Outliner", &S.bShowOutliner, 0.60f, 0.04f, 0.39f, 0.33f ) ) return;
	CWorldEditorSession& Session = S.Session;
	const FWorldDefinition& World = Session.Document.Get();
	const ImGuiIO& IO = ImGui::GetIO();

	ImGui::SetNextItemWidth( -ImGui::GetFrameHeight() * 2.0f - ImGui::GetStyle().ItemSpacing.x * 2.0f );
	NDebugUI::InputStringWithHint( "##Filter", "filter: name  tag:Enemy  comp:Model  #12", S.OutlinerFilter );
	ImGui::SameLine();
	if ( ImGui::Button( "x##ClearFilter" ) ) S.OutlinerFilter = FString();
	ImGui::SameLine();
	if ( ImGui::Button( "+##AddEmpty" ) ) CreateEmptyAtView( S );
	if ( ImGui::IsItemHovered() ) ImGui::SetTooltip( "Add an empty object in front of the camera" );

	const TArray<FEditorOutlinerRow> Rows = NWorldEditor::BuildOutlinerRows( World, S.Collapsed, S.OutlinerFilter );
	ImGui::BeginChild( "##Rows", ImVec2( 0.0f, 0.0f ), ImGuiChildFlags_Borders );
	if ( Rows.IsEmpty() ) ImGui::TextDisabled( World.Objects.IsEmpty() ? "(empty world: drag a model, image or sound from World Assets)" : "(no object matches the filter)" );

	const float32 RowHeight = ImGui::GetFrameHeight();
	const float32 Indent = 14.0f;
	ImDrawList* DL = ImGui::GetWindowDrawList();
	for ( int32 RowIndex = 0; RowIndex < Rows.Num(); ++RowIndex )
	{
		const FEditorOutlinerRow& Row = Rows[RowIndex];
		const FWorldObject* Object = World.FindObject( Row.ObjectId );
		if ( Object == nullptr ) continue;
		ImGui::PushID( Row.ObjectId );

		const ImVec2 RowMin = ImGui::GetCursorScreenPos();
		const float32 RowWidth = ImGui::GetContentRegionAvail().x;
		const ImVec2 RowMax( RowMin.x + RowWidth, RowMin.y + RowHeight );
		const bool bSelected = Session.IsSelected( Row.ObjectId );
		if ( bSelected ) DL->AddRectFilled( RowMin, RowMax, Row.ObjectId == Session.GetPrimary() ? IM_COL32( 90, 120, 190, 150 ) : IM_COL32( 80, 100, 150, 100 ) );

		// 畳む / 開く
		const float32 ArrowX = RowMin.x + static_cast<float32>( Row.Depth ) * Indent;
		if ( Row.bHasChildren )
		{
			ImGui::SetCursorScreenPos( ImVec2( ArrowX, RowMin.y ) );
			if ( ImGui::InvisibleButton( "##Arrow", ImVec2( Indent, RowHeight ) ) )
			{
				if ( S.Collapsed.Contains( Row.ObjectId ) ) S.Collapsed.Remove( Row.ObjectId );
				else S.Collapsed.Add( Row.ObjectId );
			}
			const float32 CenterY = RowMin.y + RowHeight * 0.5f;
			if ( Row.bExpanded ) DL->AddTriangleFilled( ImVec2( ArrowX + 3.0f, CenterY - 3.0f ), ImVec2( ArrowX + 11.0f, CenterY - 3.0f ), ImVec2( ArrowX + 7.0f, CenterY + 4.0f ), IM_COL32( 210, 210, 220, 255 ) );
			else DL->AddTriangleFilled( ImVec2( ArrowX + 4.0f, CenterY - 5.0f ), ImVec2( ArrowX + 4.0f, CenterY + 5.0f ), ImVec2( ArrowX + 11.0f, CenterY ), IM_COL32( 210, 210, 220, 255 ) );
		}

		// 有効 / 無効（右端）
		const float32 CheckSize = ImGui::GetFrameHeight();
		ImGui::SetCursorScreenPos( ImVec2( RowMax.x - CheckSize, RowMin.y ) );
		bool bEnabled = Object->bEnabled;
		if ( ImGui::Checkbox( "##Enabled", &bEnabled ) ) NWorldEdit::EditObject( Session.Document, Row.ObjectId, "Outliner.Enabled" )->bEnabled = bEnabled;
		if ( ImGui::IsItemHovered() ) ImGui::SetTooltip( "Enabled (a disabled object and its children are not placed in the game)" );

		// 名前の行
		const float32 NameX = ArrowX + Indent;
		ImGui::SetCursorScreenPos( ImVec2( NameX, RowMin.y ) );
		bool bAny = false;
		const EWorldIssueSeverity Severity = WorstSeverity( S.Issues, Row.ObjectId, bAny );
		const bool bDim = !Object->bEnabled || !Row.bMatch;
		if ( bDim ) ImGui::PushStyleColor( ImGuiCol_Text, ImGui::GetStyleColorVec4( ImGuiCol_TextDisabled ) );
		const FString Label = ObjectLabel( *Object );
		const bool bClicked = ImGui::Selectable( UI_TEXT( Label ), false, ImGuiSelectableFlags_AllowDoubleClick, ImVec2( RowMax.x - CheckSize - NameX - 4.0f, RowHeight ) );
		if ( bDim ) ImGui::PopStyleColor();
		const bool bItemHovered = ImGui::IsItemHovered();
		if ( bClicked )
		{
			if ( IO.KeyShift && S.LastClickedId != WorldNoId )
			{
				// 見えている行の、最後にクリックしたものから、ここまで
				int32 From = INDEX_NONE;
				for ( int32 i = 0; i < Rows.Num(); ++i )
				{
					if ( Rows[i].ObjectId == S.LastClickedId ) From = i;
				}
				if ( From != INDEX_NONE )
				{
					TArray<int32> Range;
					for ( int32 i = std::min( From, RowIndex ); i <= std::max( From, RowIndex ); ++i ) Range.Add( Rows[i].ObjectId );
					Session.SetSelection( Range );
				}
			}
			else
			{
				Session.Select( Row.ObjectId, GetSelectMode( IO ) );
				S.LastClickedId = Row.ObjectId;
			}
			if ( ImGui::IsMouseDoubleClicked( ImGuiMouseButton_Left ) ) Session.FocusSelection( S.View, S.AssetInfo.Get() );
		}

		// 検証の印・部品の頭文字（名前の右）
		if ( bAny ) DL->AddCircleFilled( ImVec2( RowMax.x - CheckSize - 10.0f, RowMin.y + RowHeight * 0.5f ), 4.0f, Severity == EWorldIssueSeverity::Error ? IM_COL32( 235, 80, 70, 255 ) : IM_COL32( 235, 190, 60, 255 ) );
		const FString Letters = ComponentLetters( *Object );
		if ( !Letters.IsEmpty() )
		{
			const ImVec2 TextSize = ImGui::CalcTextSize( UI_TEXT( Letters ) );
			DL->AddText( ImVec2( RowMax.x - CheckSize - 22.0f - TextSize.x, RowMin.y + ( RowHeight - TextSize.y ) * 0.5f ), IM_COL32( 140, 170, 220, 220 ), UI_TEXT( Letters ) );
		}

		// ドラッグ（選択を動かす）
		if ( ImGui::BeginDragDropSource( ImGuiDragDropFlags_None ) )
		{
			if ( !Session.IsSelected( Row.ObjectId ) ) Session.Select( Row.ObjectId );
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
		ImGui::InvisibleButton( "##Rest", ImVec2( std::max( Rest.x, 1.0f ), std::max( Rest.y, 1.0f ) ) );
		if ( ImGui::IsItemClicked( ImGuiMouseButton_Left ) ) Session.ClearSelection();
		if ( ImGui::BeginDragDropTarget() )
		{
			if ( const ImGuiPayload* Payload = ImGui::AcceptDragDropPayload( NWorldEditorUI::ObjectPayloadType ) )
			{
				(void)Payload;
				TWorldDocument::FBatch Batch( Session.Document );
				for ( const int32 Root : Session.GetSelectedRoots() ) NWorldEdit::Reparent( Session.Document, Root, WorldNoId, WorldNoId, true );
			}
			ImGui::EndDragDropTarget();
		}
	}
	ImGui::EndChild();
	ImGui::End();
}

//=============================================================================
// インスペクター
//=============================================================================
namespace
{
	const char* const BusNames[] = { "Master", "Music", "Sfx", "Ambient", "Voice", "Ui" };
	const char* const PropTypeNames[] = { "Bool", "Int", "Float", "String", "Vector3", "Asset", "Object" };
	const char* const BillboardModeNames[] = { "Camera (always faces the screen)", "CameraY (turns around the Y axis only)", "World (fixed to the object)" };
	const char* const ShapeNames[] = { "Box", "Sphere", "Capsule", "Mesh" };
	const char* const CollisionModeNames[] = { "Auto (world tag rules)", "On", "Off" };

	void SectionTitle( const char* Text )
	{
		ImGui::Spacing();
		ImGui::TextColored( ImVec4( 0.65f, 0.78f, 1.0f, 1.0f ), "%s", Text );
		ImGui::Separator();
	}

	// 項目を 1 つ、「リセット」の小さなボタンつきで出す
	bool ResetButton( const char* Id )
	{
		ImGui::SameLine();
		const bool bPressed = ImGui::SmallButton( Id );
		if ( ImGui::IsItemHovered() ) ImGui::SetTooltip( "Reset" );
		return bPressed;
	}

	// 部品の見出し: 右端に削除ボタン。開いているときに true
	bool ComponentHeader( const char* Title, bool& bOutRemove )
	{
		bOutRemove = false;
		const bool bOpen = ImGui::CollapsingHeader( Title, ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap );
		ImGui::SameLine( ImGui::GetWindowWidth() - 36.0f );
		ImGui::PushID( Title );
		bOutRemove = ImGui::SmallButton( "x" );
		if ( ImGui::IsItemHovered() ) ImGui::SetTooltip( "Remove this component" );
		ImGui::PopID();
		return bOpen;
	}

	void DrawAssetPathField( FState& S, const char* Label, FString& Path, const TArray<FString>& Files, bool& bOutChanged )
	{
		ImGui::TextUnformatted( Label );
		ImGui::SetNextItemWidth( -FLT_MIN );
		ImGui::PushID( Label );
		bOutChanged = NDebugUI::FileCombo( "##Path", Path, Files, "(none)" );
		ImGui::PopID();
		if ( !Path.IsEmpty() && S.AssetInfo.IsValid() && !S.AssetInfo->Exists( Path ) ) ImGui::TextColored( ImVec4( 1.0f, 0.5f, 0.4f, 1.0f ), "'%s' was not found", UI_TEXT( Path ) );
	}

	void DrawTransformSection( FState& S, const FWorldObject& Object )
	{
		CWorldEditorSession& Session = S.Session;
		const int32 Id = Object.Id;
		SectionTitle( "Transform  (relative to the parent)" );

		FVector3 Position = Object.Transform.Position;
		if ( ImGui::DragFloat3( "Position", &Position.x, 0.05f, 0.0f, 0.0f, "%.3f" ) ) NWorldEdit::EditObject( Session.Document, Id, "Inspector.Position" )->Transform.Position = Position;
		if ( ResetButton( "R##Position" ) ) NWorldEdit::EditObject( Session.Document, Id )->Transform.Position = FVector3::Zero;

		FVector3 Rotation = Object.Transform.RotationDegrees;
		if ( ImGui::DragFloat3( "Rotation", &Rotation.x, 0.5f, 0.0f, 0.0f, "%.1f" ) ) NWorldEdit::EditObject( Session.Document, Id, "Inspector.Rotation" )->Transform.RotationDegrees = Rotation;
		if ( ResetButton( "R##Rotation" ) ) NWorldEdit::EditObject( Session.Document, Id )->Transform.RotationDegrees = FVector3::Zero;

		FVector3 Scale = Object.Transform.Scale;
		if ( ImGui::DragFloat3( "Scale", &Scale.x, 0.01f, 0.0f, 0.0f, "%.3f" ) ) NWorldEdit::EditObject( Session.Document, Id, "Inspector.Scale" )->Transform.Scale = Scale;
		if ( ResetButton( "R##Scale" ) ) NWorldEdit::EditObject( Session.Document, Id )->Transform.Scale = FVector3::One;

		const FTransform WorldTransform = NWorld::ComputeWorldTransform( Session.Document.Get(), Id );
		if ( Object.Parent != WorldNoId ) ImGui::TextDisabled( "World position  %.3f  %.3f  %.3f", WorldTransform.Position.x, WorldTransform.Position.y, WorldTransform.Position.z );
	}

	void DrawTagsSection( FState& S, const FWorldObject& Object )
	{
		CWorldEditorSession& Session = S.Session;
		SectionTitle( "Tags  (the code finds and binds objects by tag)" );

		const TArray<FString> Tags = Object.Tags;
		for ( int32 i = 0; i < Tags.Num(); ++i )
		{
			ImGui::PushID( i );
			if ( i > 0 && ImGui::GetContentRegionAvail().x > ImGui::CalcTextSize( UI_TEXT( Tags[i] ) ).x + 60.0f ) ImGui::SameLine();
			ImGui::PushStyleColor( ImGuiCol_Button, ImVec4( 0.25f, 0.38f, 0.58f, 1.0f ) );
			const FString Chip = Tags[i] + "  x";
			if ( ImGui::SmallButton( UI_TEXT( Chip ) ) ) NWorldEdit::RemoveTag( Session.Document, Object.Id, Tags[i] );
			ImGui::PopStyleColor();
			ImGui::PopID();
		}
		if ( Tags.IsEmpty() ) ImGui::TextDisabled( "(no tags)" );

		ImGui::SetNextItemWidth( 140.0f );
		const bool bEnter = NDebugUI::InputStringWithHint( "##NewTag", "new tag", S.NewTag, ImGuiInputTextFlags_EnterReturnsTrue );
		ImGui::SameLine();
		if ( ( ImGui::Button( "Add tag" ) || bEnter ) && !S.NewTag.TrimStartAndEnd().IsEmpty() )
		{
			NWorldEdit::AddTag( Session.Document, Object.Id, S.NewTag );
			S.NewTag = FString();
		}

		// すでにワールドにあるタグから選ぶ
		TArray<FString> Known;
		for ( const FWorldObject& Other : Session.Document.Get().Objects )
		{
			for ( const FString& Tag : Other.Tags )
			{
				if ( !Object.HasTag( Tag ) ) Known.AddUnique( Tag );
			}
		}
		if ( !Known.IsEmpty() )
		{
			Known.Sort();
			ImGui::SameLine();
			ImGui::SetNextItemWidth( 120.0f );
			if ( ImGui::BeginCombo( "##KnownTags", "existing...", ImGuiComboFlags_HeightLarge ) )
			{
				for ( const FString& Tag : Known )
				{
					if ( ImGui::Selectable( UI_TEXT( Tag ) ) ) NWorldEdit::AddTag( Session.Document, Object.Id, Tag );
				}
				ImGui::EndCombo();
			}
		}
	}

	void DrawPropEditor( FState& S, const FWorldObject& Object, const FWorldProp& Prop )
	{
		CWorldEditorSession& Session = S.Session;
		FWorldProp Edited = Prop;
		const FString Key = "Inspector.Prop." + Prop.Name;
		bool bChanged = false;
		ImGui::SetNextItemWidth( -FLT_MIN );
		switch ( Prop.Type )
		{
		case EWorldPropType::Bool: bChanged = ImGui::Checkbox( "##V", &Edited.bValue ); break;
		case EWorldPropType::Int: bChanged = ImGui::InputScalar( "##V", ImGuiDataType_S64, &Edited.IntValue ); break;
		case EWorldPropType::Float: bChanged = ImGui::DragScalar( "##V", ImGuiDataType_Double, &Edited.FloatValue, 0.01f, nullptr, nullptr, "%.4f" ); break;
		case EWorldPropType::String: bChanged = NDebugUI::InputString( "##V", Edited.StringValue ); break;
		case EWorldPropType::Vector3: bChanged = ImGui::DragFloat3( "##V", &Edited.VectorValue.x, 0.05f ); break;
		case EWorldPropType::Asset:
		{
			// 画像・モデル・音のどれのファイルでも選べるように、全部をまとめた一覧から
			TArray<FString> All = S.ModelFiles;
			All.Append( S.TextureFiles );
			All.Append( S.SoundFiles );
			bChanged = NDebugUI::FileCombo( "##V", Edited.StringValue, All, "(none)" );
			break;
		}
		case EWorldPropType::Object:
		{
			const FWorldObject* Target = Session.Document.Get().FindObject( Edited.ObjectId );
			const FString Preview = Edited.ObjectId == WorldNoId ? FString( "(none)" ) : Target != nullptr ? ObjectLabel( *Target ) : FString::Printf( "(missing #%d)", Edited.ObjectId );
			if ( Edited.ObjectId != WorldNoId && Target == nullptr ) ImGui::PushStyleColor( ImGuiCol_Text, ImVec4( 1.0f, 0.5f, 0.4f, 1.0f ) );
			const bool bOpen = ImGui::BeginCombo( "##V", UI_TEXT( Preview ) );
			if ( Edited.ObjectId != WorldNoId && Target == nullptr ) ImGui::PopStyleColor();
			if ( bOpen )
			{
				if ( ImGui::Selectable( "(none)", Edited.ObjectId == WorldNoId ) )
				{
					Edited.ObjectId = WorldNoId;
					bChanged = true;
				}
				for ( const FWorldObject& Other : Session.Document.Get().Objects )
				{
					if ( Other.Id == Object.Id ) continue;
					if ( ImGui::Selectable( UI_TEXT( ObjectLabel( Other ) ), Other.Id == Edited.ObjectId ) )
					{
						Edited.ObjectId = Other.Id;
						bChanged = true;
					}
				}
				ImGui::EndCombo();
			}
			break;
		}
		}
		if ( bChanged ) NWorldEdit::SetProp( Session.Document, Object.Id, Edited, *Key );
	}

	void DrawPropsSection( FState& S, const FWorldObject& Object )
	{
		CWorldEditorSession& Session = S.Session;
		SectionTitle( "Properties  (values the code reads from this object)" );
		const TArray<FWorldProp> Props = Object.Props;
		for ( int32 i = 0; i < Props.Num(); ++i )
		{
			ImGui::PushID( i );
			if ( ImGui::SmallButton( "x" ) )
			{
				NWorldEdit::RemoveProp( Session.Document, Object.Id, Props[i].Name );
				ImGui::PopID();
				break;
			}
			ImGui::SameLine();
			ImGui::Text( "%s", UI_TEXT( Props[i].Name ) );
			ImGui::SameLine();
			ImGui::TextDisabled( "(%s)", ToString( Props[i].Type ) );
			DrawPropEditor( S, Object, Props[i] );
			ImGui::PopID();
		}
		if ( Props.IsEmpty() ) ImGui::TextDisabled( "(no properties)" );

		ImGui::SetNextItemWidth( 110.0f );
		NDebugUI::InputStringWithHint( "##NewPropName", "name", S.NewPropName );
		ImGui::SameLine();
		ImGui::SetNextItemWidth( 90.0f );
		ImGui::Combo( "##NewPropType", &S.NewPropType, "Bool\0Int\0Float\0String\0Vector3\0Asset\0Object\0" );
		ImGui::SameLine();
		const FString Name = S.NewPropName.TrimStartAndEnd();
		ImGui::BeginDisabled( Name.IsEmpty() || Object.FindProp( Name ) != nullptr );
		if ( ImGui::Button( "Add" ) )
		{
			FWorldProp Prop;
			switch ( static_cast<EWorldPropType>( S.NewPropType ) )
			{
			case EWorldPropType::Bool: Prop = FWorldProp::MakeBool( Name, false ); break;
			case EWorldPropType::Int: Prop = FWorldProp::MakeInt( Name, 0 ); break;
			case EWorldPropType::Float: Prop = FWorldProp::MakeFloat( Name, 0.0 ); break;
			case EWorldPropType::String: Prop = FWorldProp::MakeString( Name, FString() ); break;
			case EWorldPropType::Vector3: Prop = FWorldProp::MakeVector3( Name, FVector3::Zero ); break;
			case EWorldPropType::Asset: Prop = FWorldProp::MakeAsset( Name, FString() ); break;
			case EWorldPropType::Object: Prop = FWorldProp::MakeObject( Name, WorldNoId ); break;
			}
			NWorldEdit::SetProp( Session.Document, Object.Id, Prop );
			S.NewPropName = FString();
		}
		ImGui::EndDisabled();
		if ( Object.FindProp( Name ) != nullptr ) ImGui::TextDisabled( "A property with that name already exists." );
	}

	void DrawComponents( FState& S, const FWorldObject& Object )
	{
		CWorldEditorSession& Session = S.Session;
		const int32 Id = Object.Id;
		SectionTitle( "Components" );
		bool bRemove = false;

		if ( Object.Model.IsSet() )
		{
			ImGui::PushID( "Model" );
			if ( ComponentHeader( "Model", bRemove ) && !bRemove )
			{
				FWorldModel Model = Object.Model.GetValue();
				bool bChanged = false;
				DrawAssetPathField( S, "File", Model.Path, S.ModelFiles, bChanged );
				if ( bChanged ) NWorldEdit::EditObject( Session.Document, Id )->Model.GetValue().Path = Model.Path;
				DrawAssetPathField( S, "Animation set (optional)", Model.Animations, S.AnimationFiles, bChanged );
				if ( bChanged ) NWorldEdit::EditObject( Session.Document, Id )->Model.GetValue().Animations = Model.Animations;
				if ( NDebugUI::InputStringWithHint( "##Clip", "clip to play (optional)", Model.Clip ) ) NWorldEdit::EditObject( Session.Document, Id, "Inspector.Model.Clip" )->Model.GetValue().Clip = Model.Clip;
				if ( ImGui::DragFloat( "Speed", &Model.Speed, 0.01f, 0.0f, 10.0f ) ) NWorldEdit::EditObject( Session.Document, Id, "Inspector.Model.Speed" )->Model.GetValue().Speed = Model.Speed;
				ImGui::BeginDisabled( !S.AssetInfo.IsValid() || Object.Mesh.IsSet() );
				if ( ImGui::Button( "Convert to editable mesh" ) && S.AssetInfo.IsValid() )
				{
					TWorldDocument::FBatch Batch( Session.Document );
					if ( !NWorldEditor::ConvertModelToMesh( Session.Document, Id, *S.AssetInfo ) ) NWorldEditorUI::SetStatus( S, "The model's triangles cannot be read (not a mesh model, or not loaded)", true );
				}
				ImGui::EndDisabled();
				if ( ImGui::IsItemHovered( ImGuiHoveredFlags_AllowWhenDisabled ) ) ImGui::SetTooltip( "Replace the model by a Mesh component with the same triangles, so that it can be edited (images and colours are not carried over)" );
				NWorldEditorUI::DrawModelMaterialEffectPanel( S, Id );
			}
			if ( bRemove ) NWorldEdit::EditObject( Session.Document, Id )->Model.Reset();
			ImGui::PopID();
		}

		if ( Object.Mesh.IsSet() )
		{
			ImGui::PushID( "Mesh" );
			if ( ComponentHeader( "Mesh", bRemove ) && !bRemove ) NWorldEditorUI::DrawMeshComponentBody( S, Object );
			if ( bRemove )
			{
				if ( Session.GetMeshObjectId() == Id ) Session.EndMeshEdit();
				NWorldEdit::EditObject( Session.Document, Id )->Mesh.Reset();
			}
			ImGui::PopID();
		}

		if ( Object.Prefab.IsSet() )
		{
			ImGui::PushID( "Prefab" );
			if ( ComponentHeader( "Prefab  (a nested world)", bRemove ) && !bRemove )
			{
				FString Path = Object.Prefab.GetValue().Path;
				bool bChanged = false;
				DrawAssetPathField( S, "World file", Path, S.WorldFiles, bChanged );
				if ( bChanged ) NWorldEdit::EditObject( Session.Document, Id )->Prefab.GetValue().Path = Path;
				const FWorldDefinition* Contents = S.AssetInfo.IsValid() && !Path.IsEmpty() ? S.AssetInfo->GetWorld( Path ) : nullptr;
				if ( Contents != nullptr ) ImGui::TextDisabled( "%d object(s) inside", Contents->Objects.Num() );
				else if ( !Path.IsEmpty() ) ImGui::TextDisabled( "(the contents cannot be read)" );
				if ( !Path.IsEmpty() && NWorld::NormalizeAssetPath( Path ) == NWorld::NormalizeAssetPath( Session.FilePath ) ) ImGui::TextColored( ImVec4( 1.0f, 0.5f, 0.4f, 1.0f ), "This world places itself" );
				ImGui::BeginDisabled( Path.IsEmpty() );
				if ( ImGui::Button( "Open" ) ) NWorldEditorUI::RequestOpenWorld( S, Path );
				ImGui::EndDisabled();
				if ( ImGui::IsItemHovered( ImGuiHoveredFlags_AllowWhenDisabled ) ) ImGui::SetTooltip( "Edit the nested world itself (asks to save this one first)" );
				ImGui::SameLine();
				ImGui::BeginDisabled( Contents == nullptr || !S.AssetInfo.IsValid() );
				if ( ImGui::Button( "Unpack" ) && S.AssetInfo.IsValid() ) Session.UnpackPrefabs( TArray<int32>{ Id }, *S.AssetInfo );
				ImGui::EndDisabled();
				if ( ImGui::IsItemHovered( ImGuiHoveredFlags_AllowWhenDisabled ) ) ImGui::SetTooltip( "Copy the contents into this world as children of this object (the Prefab component is removed)" );
			}
			if ( bRemove ) NWorldEdit::EditObject( Session.Document, Id )->Prefab.Reset();
			ImGui::PopID();
		}

		if ( Object.Billboard.IsSet() )
		{
			ImGui::PushID( "Billboard" );
			if ( ComponentHeader( "Billboard", bRemove ) && !bRemove )
			{
				FWorldBillboard Board = Object.Billboard.GetValue();
				bool bChanged = false;
				DrawAssetPathField( S, "Image", Board.Texture, S.TextureFiles, bChanged );
				if ( bChanged ) NWorldEdit::EditObject( Session.Document, Id )->Billboard.GetValue().Texture = Board.Texture;
				int32 Mode = static_cast<int32>( Board.Mode );
				ImGui::SetNextItemWidth( -FLT_MIN );
				if ( ImGui::Combo( "##Mode", &Mode, BillboardModeNames, 3 ) ) NWorldEdit::EditObject( Session.Document, Id )->Billboard.GetValue().Mode = static_cast<EWorldBillboardMode>( Mode );
				if ( ImGui::DragFloat2( "Size", &Board.Size.x, 0.01f, 0.0f, 0.0f, "%.3f" ) ) NWorldEdit::EditObject( Session.Document, Id, "Inspector.Billboard.Size" )->Billboard.GetValue().Size = Board.Size;
				if ( ImGui::DragFloat2( "Pivot", &Board.Pivot.x, 0.005f, 0.0f, 1.0f, "%.3f" ) ) NWorldEdit::EditObject( Session.Document, Id, "Inspector.Billboard.Pivot" )->Billboard.GetValue().Pivot = Board.Pivot;
				if ( ImGui::IsItemHovered() ) ImGui::SetTooltip( "The point of the image that sits at the object's position (0,0 = top-left, 0.5,1 = bottom-centre)" );
				float32 Tint[4] = { Board.Tint.r / 255.0f, Board.Tint.g / 255.0f, Board.Tint.b / 255.0f, Board.Tint.a / 255.0f };
				if ( ImGui::ColorEdit4( "Tint", Tint ) )
				{
					NWorldEdit::EditObject( Session.Document, Id, "Inspector.Billboard.Tint" )->Billboard.GetValue().Tint = FColor32( static_cast<uint8>( Tint[0] * 255.0f + 0.5f ), static_cast<uint8>( Tint[1] * 255.0f + 0.5f ), static_cast<uint8>( Tint[2] * 255.0f + 0.5f ), static_cast<uint8>( Tint[3] * 255.0f + 0.5f ) );
				}
				if ( ImGui::Checkbox( "Flip X", &Board.bFlipX ) ) NWorldEdit::EditObject( Session.Document, Id )->Billboard.GetValue().bFlipX = Board.bFlipX;
				ImGui::SameLine();
				if ( ImGui::Checkbox( "Flip Y", &Board.bFlipY ) ) NWorldEdit::EditObject( Session.Document, Id )->Billboard.GetValue().bFlipY = Board.bFlipY;
				ImGui::SameLine();
				if ( ImGui::Checkbox( "Use part of the image", &Board.bUseRect ) ) NWorldEdit::EditObject( Session.Document, Id )->Billboard.GetValue().bUseRect = Board.bUseRect;
				if ( Board.bUseRect )
				{
					int32 Rect[4] = { Board.RectX, Board.RectY, Board.RectWidth, Board.RectHeight };
					if ( ImGui::DragInt4( "x y w h", Rect, 1.0f, 0, 16384 ) )
					{
						FWorldBillboard& Target = NWorldEdit::EditObject( Session.Document, Id, "Inspector.Billboard.Rect" )->Billboard.GetValue();
						Target.RectX = Rect[0];
						Target.RectY = Rect[1];
						Target.RectWidth = Rect[2];
						Target.RectHeight = Rect[3];
					}
				}
			}
			if ( bRemove ) NWorldEdit::EditObject( Session.Document, Id )->Billboard.Reset();
			ImGui::PopID();
		}

		if ( Object.Sound.IsSet() )
		{
			ImGui::PushID( "Sound" );
			if ( ComponentHeader( "Sound", bRemove ) && !bRemove )
			{
				FWorldSound Sound = Object.Sound.GetValue();
				bool bChanged = false;
				DrawAssetPathField( S, "File", Sound.Path, S.SoundFiles, bChanged );
				if ( bChanged ) NWorldEdit::EditObject( Session.Document, Id )->Sound.GetValue().Path = Sound.Path;
				int32 Bus = 3;
				for ( int32 i = 0; i < 6; ++i )
				{
					if ( Sound.Bus.Equals( BusNames[i], ESearchCase::IgnoreCase ) ) Bus = i;
				}
				ImGui::SetNextItemWidth( 140.0f );
				if ( ImGui::Combo( "Bus", &Bus, BusNames, 6 ) ) NWorldEdit::EditObject( Session.Document, Id )->Sound.GetValue().Bus = BusNames[Bus];
				if ( ImGui::SliderFloat( "Volume", &Sound.Volume, 0.0f, 2.0f ) ) NWorldEdit::EditObject( Session.Document, Id, "Inspector.Sound.Volume" )->Sound.GetValue().Volume = Sound.Volume;
				if ( ImGui::SliderFloat( "Pitch", &Sound.Pitch, 0.25f, 4.0f ) ) NWorldEdit::EditObject( Session.Document, Id, "Inspector.Sound.Pitch" )->Sound.GetValue().Pitch = Sound.Pitch;
				if ( ImGui::Checkbox( "Loop", &Sound.bLoop ) ) NWorldEdit::EditObject( Session.Document, Id )->Sound.GetValue().bLoop = Sound.bLoop;
				ImGui::SameLine();
				if ( ImGui::Checkbox( "Play when the scene starts", &Sound.bAutoPlay ) ) NWorldEdit::EditObject( Session.Document, Id )->Sound.GetValue().bAutoPlay = Sound.bAutoPlay;
				float32 Range[2] = { Sound.MinDistance, Sound.MaxDistance };
				if ( ImGui::DragFloat2( "Near / far distance", Range, 0.1f, 0.0f, 10000.0f ) )
				{
					FWorldSound& Target = NWorldEdit::EditObject( Session.Document, Id, "Inspector.Sound.Range" )->Sound.GetValue();
					Target.MinDistance = Range[0];
					Target.MaxDistance = Range[1];
				}
			}
			if ( bRemove ) NWorldEdit::EditObject( Session.Document, Id )->Sound.Reset();
			ImGui::PopID();
		}

		if ( Object.Effect.IsSet() )
		{
			ImGui::PushID( "Effect" );
			if ( ComponentHeader( "Effect", bRemove ) && !bRemove )
			{
				FWorldEffect Effect = Object.Effect.GetValue();
				bool bChanged = false;
				DrawAssetPathField( S, "File", Effect.Path, S.EffectFiles, bChanged );
				if ( bChanged ) NWorldEdit::EditObject( Session.Document, Id )->Effect.GetValue().Path = Effect.Path;
				if ( !Effect.Path.IsEmpty() && FEffectBackendRegistry::Global().Find( Effect.Path ) == nullptr ) ImGui::TextColored( ImVec4( 1.0f, 0.85f, 0.4f, 1.0f ), "No effect backend handles this kind of file" );
				if ( ImGui::Checkbox( "Play when the scene starts", &Effect.bAutoPlay ) ) NWorldEdit::EditObject( Session.Document, Id )->Effect.GetValue().bAutoPlay = Effect.bAutoPlay;
				if ( ImGui::DragFloat( "Scale", &Effect.Scale, 0.01f, 0.01f, 100.0f, "%.2f" ) ) NWorldEdit::EditObject( Session.Document, Id, "Inspector.Effect.Scale" )->Effect.GetValue().Scale = Effect.Scale;
				if ( ImGui::DragFloat( "Speed", &Effect.Speed, 0.01f, 0.0f, 10.0f, "%.2f" ) ) NWorldEdit::EditObject( Session.Document, Id, "Inspector.Effect.Speed" )->Effect.GetValue().Speed = Effect.Speed;
				if ( ImGui::InputInt( "Seed (0 = the effect's)", &Effect.Seed ) ) NWorldEdit::EditObject( Session.Document, Id, "Inspector.Effect.Seed" )->Effect.GetValue().Seed = std::max( Effect.Seed, 0 );
				const char* TargetPreview = Effect.Target == WorldNoId ? "(none)" : "(missing object)";
				if ( const FWorldObject* TargetObject = Session.Document.Get().FindObject( Effect.Target ); TargetObject != nullptr ) TargetPreview = UI_TEXT( TargetObject->Name );
				if ( ImGui::BeginCombo( "Target object", TargetPreview ) )
				{
					if ( ImGui::Selectable( "(none)", Effect.Target == WorldNoId ) ) NWorldEdit::EditObject( Session.Document, Id )->Effect.GetValue().Target = WorldNoId;
					for ( const FWorldObject& Candidate : Session.Document.Get().Objects )
					{
						if ( Candidate.Id == Id ) continue;
						ImGui::PushID( Candidate.Id );
						if ( ImGui::Selectable( UI_TEXT( Candidate.Name ), Effect.Target == Candidate.Id ) ) NWorldEdit::EditObject( Session.Document, Id )->Effect.GetValue().Target = Candidate.Id;
						ImGui::PopID();
					}
					ImGui::EndCombo();
				}
				FVector3 TargetOffset = std::isfinite( Effect.TargetOffset.x ) && std::isfinite( Effect.TargetOffset.y ) && std::isfinite( Effect.TargetOffset.z ) ? Effect.TargetOffset : FVector3::Zero;
				if ( ImGui::DragFloat3( "Target offset", &TargetOffset.x, 0.05f, 0.0f, 0.0f, "%.3f" ) ) NWorldEdit::EditObject( Session.Document, Id, "Inspector.Effect.TargetOffset" )->Effect.GetValue().TargetOffset = TargetOffset;
				if ( ResetButton( "R##EffectTargetOffset" ) ) NWorldEdit::EditObject( Session.Document, Id )->Effect.GetValue().TargetOffset = FVector3::Zero;
				int32 Loop = Effect.Loop + 1;   // 0: エフェクトの指定のまま / 1: 繰り返さない / 2: 繰り返す
				ImGui::SetNextItemWidth( 180.0f );
				if ( ImGui::Combo( "Loop", &Loop, "As the effect says\0Once\0Loop\0" ) ) NWorldEdit::EditObject( Session.Document, Id )->Effect.GetValue().Loop = Loop - 1;
				if ( CAssetManager* Assets = NWorldEditorUI::GetAssets(); Assets != nullptr && !Effect.Path.IsEmpty() )
				{
					if ( const TSharedPtr<CEffectAsset> Asset = Assets->LoadEffect( Effect.Path ); Asset.IsValid() )
					{
						const FString Summary = Asset->Describe();
						ImGui::TextDisabled( "%s", UI_TEXT( Summary ) );
					}
				}
				if ( ImGui::Button( "Restart preview" ) ) ++S.EffectRestartSerial;
			}
			if ( bRemove ) NWorldEdit::EditObject( Session.Document, Id )->Effect.Reset();
			ImGui::PopID();
		}

		if ( Object.Collider.IsSet() )
		{
			ImGui::PushID( "Collider" );
			if ( ComponentHeader( "Collider", bRemove ) && !bRemove )
			{
				FWorldCollider Collider = Object.Collider.GetValue();
				int32 Shape = static_cast<int32>( Collider.Shape );
				ImGui::SetNextItemWidth( 140.0f );
				if ( ImGui::Combo( "Shape", &Shape, ShapeNames, 4 ) ) NWorldEdit::EditObject( Session.Document, Id )->Collider.GetValue().Shape = static_cast<EWorldColliderShape>( Shape );

				// モデルの大きさに合わせる（選んでいるすべてのオブジェクトに。Model を持つものだけ）
				ImGui::BeginDisabled( !S.AssetInfo.IsValid() );
				ImGui::TextDisabled( "Fit to the model" );
				for ( const EWorldColliderShape Fit : { EWorldColliderShape::Box, EWorldColliderShape::Sphere, EWorldColliderShape::Capsule } )
				{
					ImGui::SameLine();
					if ( ImGui::SmallButton( ToString( Fit ) ) && S.AssetInfo.IsValid() ) Session.FitSelectionColliders( Fit, *S.AssetInfo );
				}
				ImGui::EndDisabled();
				if ( ImGui::IsItemHovered( ImGuiHoveredFlags_AllowWhenDisabled ) ) ImGui::SetTooltip( "Sets the shape and size from the Model's bounds, for every selected object that has a Model" );

				if ( Collider.Shape == EWorldColliderShape::Mesh )
				{
					bool bChanged = false;
					FString MeshFile = Collider.Mesh;
					DrawAssetPathField( S, "Triangles from (empty = this object's Mesh or Model)", MeshFile, S.ModelFiles, bChanged );
					if ( bChanged ) NWorldEdit::EditObject( Session.Document, Id )->Collider.GetValue().Mesh = MeshFile;
					const FString Source = NWorld::GetColliderMeshPath( Object );
					if ( NWorld::UsesEditMeshCollider( Object ) ) ImGui::TextDisabled( "Using this object's Mesh component (%d triangles)", Object.Mesh.GetValue().GetNumTriangles() );
					else if ( Source.IsEmpty() ) ImGui::TextColored( ImVec4( 1.0f, 0.5f, 0.4f, 1.0f ), "No triangles: add a Mesh or Model component, or choose a file above" );
					else if ( const FEditorCollisionMesh* Mesh = S.AssetInfo.IsValid() ? S.AssetInfo->GetCollisionMesh( Source ) : nullptr )
					{
						constexpr int32 HeavyTriangles = 30000;
						ImGui::TextDisabled( "%d triangles from '%s'", Mesh->GetNumTriangles(), UI_TEXT( Source ) );
						if ( Mesh->GetNumTriangles() > HeavyTriangles ) ImGui::TextColored( ImVec4( 1.0f, 0.75f, 0.4f, 1.0f ), "Many triangles: slow to build and to simulate. A simpler model for the collision is better." );
					}
					else ImGui::TextDisabled( "Triangles of '%s' are not available (not a mesh model, or not loaded)", UI_TEXT( Source ) );
					ImGui::TextWrapped( "Static only (does not move with the object). Uses the model's own coordinates; the object's scale is applied, Offset is not used. One-sided: bodies pass through from behind." );
				}
				else if ( Collider.Shape == EWorldColliderShape::Box )
				{
					if ( ImGui::DragFloat3( "Size", &Collider.Size.x, 0.02f, 0.001f, 10000.0f ) ) NWorldEdit::EditObject( Session.Document, Id, "Inspector.Collider.Size" )->Collider.GetValue().Size = Collider.Size;
				}
				else
				{
					if ( ImGui::DragFloat( "Radius", &Collider.Radius, 0.01f, 0.001f, 10000.0f ) ) NWorldEdit::EditObject( Session.Document, Id, "Inspector.Collider.Radius" )->Collider.GetValue().Radius = Collider.Radius;
					if ( Collider.Shape == EWorldColliderShape::Capsule && ImGui::DragFloat( "Height", &Collider.Height, 0.02f, 0.001f, 10000.0f ) ) NWorldEdit::EditObject( Session.Document, Id, "Inspector.Collider.Height" )->Collider.GetValue().Height = Collider.Height;
				}
				if ( Collider.Shape != EWorldColliderShape::Mesh && ImGui::DragFloat3( "Offset", &Collider.Offset.x, 0.02f ) ) NWorldEdit::EditObject( Session.Document, Id, "Inspector.Collider.Offset" )->Collider.GetValue().Offset = Collider.Offset;
				if ( ImGui::Checkbox( "Trigger (reports overlaps, does not block)", &Collider.bTrigger ) ) NWorldEdit::EditObject( Session.Document, Id )->Collider.GetValue().bTrigger = Collider.bTrigger;
			}
			if ( bRemove ) NWorldEdit::EditObject( Session.Document, Id )->Collider.Reset();
			ImGui::PopID();
		}

		for ( const FWorldRawComponent& Raw : Object.RawComponents )
		{
			ImGui::TextDisabled( "%s  (not supported by this editor yet; kept as it is)", UI_TEXT( Raw.Type ) );
		}

		// 足す
		if ( ImGui::Button( "Add component..." ) ) ImGui::OpenPopup( "##AddComponent" );
		if ( ImGui::BeginPopup( "##AddComponent" ) )
		{
			if ( ImGui::MenuItem( "Model", nullptr, false, !Object.Model.IsSet() ) ) NWorldEdit::EditObject( Session.Document, Id )->Model = FWorldModel();
			if ( ImGui::MenuItem( "Mesh (editable)", nullptr, false, !Object.Mesh.IsSet() ) ) NWorldEdit::EditObject( Session.Document, Id )->Mesh = NEditMesh::MakePrimitive( EEditMeshPrimitive::Box );
			if ( ImGui::MenuItem( "Prefab (nested world)", nullptr, false, !Object.Prefab.IsSet() ) ) NWorldEdit::EditObject( Session.Document, Id )->Prefab = FWorldPrefab();
			if ( ImGui::MenuItem( "Billboard", nullptr, false, !Object.Billboard.IsSet() ) ) NWorldEdit::EditObject( Session.Document, Id )->Billboard = FWorldBillboard();
			if ( ImGui::MenuItem( "Sound", nullptr, false, !Object.Sound.IsSet() ) ) NWorldEdit::EditObject( Session.Document, Id )->Sound = FWorldSound();
			if ( ImGui::MenuItem( "Effect (particles)", nullptr, false, !Object.Effect.IsSet() ) ) NWorldEdit::EditObject( Session.Document, Id )->Effect = FWorldEffect();
			if ( ImGui::MenuItem( "Collider", nullptr, false, !Object.Collider.IsSet() ) ) NWorldEdit::EditObject( Session.Document, Id )->Collider = FWorldCollider();
			ImGui::EndPopup();
		}
	}

	void DrawCollisionSection( FState& S, const FWorldObject& Object )
	{
		CWorldEditorSession& Session = S.Session;
		SectionTitle( "Collision" );
		int32 Mode = static_cast<int32>( Object.Collision );
		ImGui::SetNextItemWidth( -FLT_MIN );
		if ( ImGui::Combo( "##CollisionMode", &Mode, CollisionModeNames, 3 ) ) NWorldEdit::EditObject( Session.Document, Object.Id )->Collision = static_cast<EWorldCollisionMode>( Mode );

		const FWorldCollisionDecision Decision = NWorld::ResolveCollision( Session.Document.Get().Settings, Object );
		const FString Text = Decision.ToDisplayString();
		ImGui::TextColored( Decision.bEnabled ? ImVec4( 0.5f, 0.9f, 0.55f, 1.0f ) : ImVec4( 0.85f, 0.6f, 0.5f, 1.0f ), "%s", UI_TEXT( Text ) );
		if ( !Object.Collider.IsSet() ) ImGui::TextDisabled( "Add a Collider component to give this object a collision shape." );
	}
}

void NWorldEditorUI::DrawInspector( FWorldEditorState& S )
{
	if ( !BeginPanel( S, "World Inspector", &S.bShowInspector, 0.60f, 0.38f, 0.39f, 0.60f ) ) return;
	CWorldEditorSession& Session = S.Session;
	const FWorldObject* Object = Session.Document.Get().FindObject( Session.GetPrimary() );
	if ( Object == nullptr )
	{
		ImGui::TextDisabled( "Nothing selected." );
		ImGui::TextWrapped( "Click an object in the view or the outliner. Drag a model, image or sound from World Assets into the view to place it." );
		ImGui::End();
		return;
	}
	const int32 Id = Object->Id;
	if ( Session.GetSelection().Num() > 1 ) ImGui::TextColored( ImVec4( 1.0f, 0.8f, 0.4f, 1.0f ), "%d objects selected (editing the last one)", Session.GetSelection().Num() );

	// 名前・有効
	ImGui::TextDisabled( "Id %d", Id );
	ImGui::SameLine();
	bool bEnabled = Object->bEnabled;
	if ( ImGui::Checkbox( "Enabled", &bEnabled ) ) NWorldEdit::EditObject( Session.Document, Id )->bEnabled = bEnabled;
	FString Name = Object->Name;
	ImGui::SetNextItemWidth( -FLT_MIN );
	if ( NDebugUI::InputStringWithHint( "##Name", "name", Name ) ) NWorldEdit::EditObject( Session.Document, Id, "Inspector.Name" )->Name = Name;

	// 親
	if ( Object->Parent != WorldNoId )
	{
		const FWorldObject* Parent = Session.Document.Get().FindObject( Object->Parent );
		ImGui::TextDisabled( "Parent  %s", Parent != nullptr ? UI_TEXT( ObjectLabel( *Parent ) ) : "(missing)" );
	}

	DrawTransformSection( S, *Object );
	Object = Session.Document.Get().FindObject( Id );
	DrawTagsSection( S, *Object );
	Object = Session.Document.Get().FindObject( Id );
	DrawPropsSection( S, *Object );
	Object = Session.Document.Get().FindObject( Id );
	DrawComponents( S, *Object );
	Object = Session.Document.Get().FindObject( Id );
	DrawCollisionSection( S, *Object );

	// この物の検証の問題
	bool bShownHeader = false;
	for ( const FWorldIssue& Issue : S.Issues )
	{
		if ( Issue.ObjectId != Id ) continue;
		if ( !bShownHeader )
		{
			SectionTitle( "Problems" );
			bShownHeader = true;
		}
		ImGui::TextColored( Issue.Severity == EWorldIssueSeverity::Error ? ImVec4( 1.0f, 0.5f, 0.4f, 1.0f ) : ImVec4( 1.0f, 0.85f, 0.4f, 1.0f ), "%s", UI_TEXT( Issue.Message ) );
	}
	if ( Object != nullptr && !Object->Extra.is_null() && !Object->Extra.empty() ) ImGui::TextDisabled( "Also holds %d unknown field(s) from another editor / version (kept as they are).", static_cast<int32>( Object->Extra.size() ) );
	ImGui::End();
}

//=============================================================================
// アセット
//=============================================================================
namespace
{
	struct FAssetRow
	{
		int32 Kind = 0;   // 1 モデル / 2 画像 / 3 音
		const FString* Path = nullptr;
	};
}

void NWorldEditorUI::DrawAssetBrowser( FWorldEditorState& S )
{
	if ( !BeginPanel( S, "World Assets", &S.bShowAssets, 0.01f, 0.67f, 0.29f, 0.31f ) ) return;

	ImGui::SetNextItemWidth( 200.0f );
	NDebugUI::InputStringWithHint( "##AssetFilter", "filter", S.AssetFilter );
	ImGui::SameLine();
	ImGui::RadioButton( "All", &S.AssetKind, 0 );
	ImGui::SameLine();
	ImGui::RadioButton( "Models", &S.AssetKind, 1 );
	ImGui::SameLine();
	ImGui::RadioButton( "Images", &S.AssetKind, 2 );
	ImGui::SameLine();
	ImGui::RadioButton( "Sounds", &S.AssetKind, 3 );
	ImGui::SameLine();
	ImGui::RadioButton( "Worlds", &S.AssetKind, 4 );
	ImGui::SameLine();
	ImGui::RadioButton( "Effects", &S.AssetKind, 5 );
	ImGui::SameLine();
	if ( ImGui::Button( "Rescan" ) )
	{
		RescanFiles( S, true );
		S.ModelPreviews.Empty(); // asset cacheを消す前に専用model instanceを解放する
		if ( S.AssetInfo.IsValid() ) S.AssetInfo->Clear();
		S.IssuesRevision = ~0u;
	}
	ImGui::TextDisabled( "Drag into the view to place.  Double-click to place in front of the camera." );

	TArray<FAssetRow> Rows;
	const auto AddAll = [&]( const TArray<FString>& Files, int32 Kind )
	{
		if ( S.AssetKind != 0 && S.AssetKind != Kind ) return;
		for ( const FString& File : Files )
		{
			if ( !S.AssetFilter.IsEmpty() && !File.Contains( S.AssetFilter, ESearchCase::IgnoreCase ) ) continue;
			if ( Kind == 4 && NWorld::NormalizeAssetPath( File ) == NWorld::NormalizeAssetPath( S.Session.FilePath ) ) continue;   // 開いているワールド自身は、置けない（循環）
			FAssetRow Row;
			Row.Kind = Kind;
			Row.Path = &File;
			Rows.Add( Row );
		}
	};
	AddAll( S.ModelFiles, 1 );
	AddAll( S.TextureFiles, 2 );
	AddAll( S.SoundFiles, 3 );
	AddAll( S.WorldFiles, 4 );
	AddAll( S.EffectFiles, 5 );

	ImGui::BeginChild( "##AssetList", ImVec2( 0.0f, 0.0f ), ImGuiChildFlags_Borders );
	if ( Rows.IsEmpty() ) ImGui::TextDisabled( "(no files)" );
	ImGuiListClipper Clipper;
	Clipper.Begin( Rows.Num(), 22.0f );
	while ( Clipper.Step() )
	{
		for ( int32 i = Clipper.DisplayStart; i < Clipper.DisplayEnd; ++i )
		{
			const FAssetRow& Row = Rows[i];
			ImGui::PushID( i );
			static const char* const KindLetters[] = { "[?]", "[M]", "[I]", "[S]", "[W]", "[E]" };
			ImGui::TextDisabled( "%s", KindLetters[std::clamp( Row.Kind, 0, 5 )] );
			ImGui::SameLine();
			if ( Row.Kind == 2 && S.AssetInfo.IsValid() )
			{
				if ( const TSharedPtr<CTexture> Texture = S.AssetInfo->GetTexture( *Row.Path ); Texture.IsValid() && Texture->GetHandle() >= 0 )
				{
					ImGui::Image( NDebugUI::GetImGuiTexture( Texture->GetHandle() ), ImVec2( 18.0f, 18.0f ) );
					ImGui::SameLine();
				}
			}
			ImGui::Selectable( UI_TEXT( *Row.Path ), false, ImGuiSelectableFlags_AllowDoubleClick );
			if ( ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked( ImGuiMouseButton_Left ) ) CreateAssetAtView( S, *Row.Path );
			if ( ImGui::BeginDragDropSource( ImGuiDragDropFlags_None ) )
			{
				ImGui::SetDragDropPayload( NWorldEditorUI::AssetPayloadType, **Row.Path, static_cast<size_t>( Row.Path->Len() ) + 1 );
				ImGui::Text( "%s", UI_TEXT( NWorldEditor::GetAssetDisplayName( *Row.Path ) ) );
				ImGui::EndDragDropSource();
			}
			ImGui::PopID();
		}
	}
	ImGui::EndChild();
	ImGui::End();
}

//=============================================================================
// 問題と設定
//=============================================================================
namespace
{
	void DrawIssuesTab( FState& S )
	{
		CWorldEditorSession& Session = S.Session;
		int32 NumErrors = 0, NumWarnings = 0;
		for ( const FWorldIssue& Issue : S.Issues ) ( Issue.Severity == EWorldIssueSeverity::Error ? NumErrors : NumWarnings )++;
		ImGui::TextColored( NumErrors > 0 ? ImVec4( 1.0f, 0.5f, 0.4f, 1.0f ) : ImVec4( 0.6f, 0.9f, 0.6f, 1.0f ), "%d error(s)", NumErrors );
		ImGui::SameLine();
		ImGui::TextColored( NumWarnings > 0 ? ImVec4( 1.0f, 0.85f, 0.4f, 1.0f ) : ImVec4( 0.6f, 0.9f, 0.6f, 1.0f ), "%d warning(s)", NumWarnings );
		ImGui::SameLine();
		ImGui::Checkbox( "Show warnings", &S.bIssuesShowWarnings );

		ImGui::BeginChild( "##Issues", ImVec2( 0.0f, 0.0f ), ImGuiChildFlags_Borders );
		if ( S.Issues.IsEmpty() ) ImGui::TextDisabled( "No problems found." );
		for ( int32 i = 0; i < S.Issues.Num(); ++i )
		{
			const FWorldIssue& Issue = S.Issues[i];
			if ( Issue.Severity == EWorldIssueSeverity::Warning && !S.bIssuesShowWarnings ) continue;
			ImGui::PushID( i );
			ImGui::PushStyleColor( ImGuiCol_Text, Issue.Severity == EWorldIssueSeverity::Error ? ImVec4( 1.0f, 0.55f, 0.45f, 1.0f ) : ImVec4( 1.0f, 0.85f, 0.4f, 1.0f ) );
			if ( ImGui::Selectable( UI_TEXT( Issue.ToDisplayString() ) ) && Issue.ObjectId != WorldNoId )
			{
				Session.Select( Issue.ObjectId );
				Session.FocusSelection( S.View, S.AssetInfo.Get() );
			}
			ImGui::PopStyleColor();
			ImGui::PopID();
		}
		ImGui::EndChild();
	}

	void DrawSettingsTab( FState& S )
	{
		CWorldEditorSession& Session = S.Session;
		const FWorldDefinition& World = Session.Document.Get();

		FString Name = World.Name;
		ImGui::SetNextItemWidth( 220.0f );
		if ( NDebugUI::InputStringWithHint( "##WorldName", "world name", Name ) ) Session.Document.BeginEdit( "World.Name" ).Name = Name;
		ImGui::SameLine();
		ImGui::TextDisabled( "format v%d  next id %d", World.Version, World.NextId );

		// 集計: 何が剛体になるか、なぜか
		SectionTitle( "Collision summary" );
		const FWorldCollisionSummary Summary = NWorld::SummarizeCollision( World );
		ImGui::Text( "%d object(s) placed, %d with a collider shape", Summary.NumPlaced, Summary.NumWithCollider );
		ImGui::Text( "collides: %d  (triggers %d, meshes %d)     off: %d", Summary.NumEnabled, Summary.NumTriggers, Summary.NumMeshes, Summary.NumDisabled );
		ImGui::TextDisabled( "decided by: the object itself %d, a tag rule %d, the default %d", Summary.NumByOverride, Summary.NumByRule, Summary.NumByDefault );

		SectionTitle( "Collision rules by tag" );
		ImGui::TextWrapped( "Whether an object with a Collider gets a physics body: the object's own On / Off first, then the first rule below whose tag the object has, then the default (on)." );
		const TArray<FWorldCollisionRule> Rules = World.Settings.CollisionRules;
		for ( int32 i = 0; i < Rules.Num(); ++i )
		{
			ImGui::PushID( i );
			int32 NumTagged = 0;
			for ( const FWorldObject& Object : World.Objects ) NumTagged += Object.HasTag( Rules[i].Tag ) ? 1 : 0;

			ImGui::BeginDisabled( i == 0 );
			if ( ImGui::ArrowButton( "##Up", ImGuiDir_Up ) ) NWorldEdit::MoveCollisionRule( Session.Document, i, i - 1 );
			ImGui::EndDisabled();
			ImGui::SameLine();
			ImGui::BeginDisabled( i == Rules.Num() - 1 );
			if ( ImGui::ArrowButton( "##Down", ImGuiDir_Down ) ) NWorldEdit::MoveCollisionRule( Session.Document, i, i + 1 );
			ImGui::EndDisabled();
			ImGui::SameLine();
			FString Tag = Rules[i].Tag;
			ImGui::SetNextItemWidth( 130.0f );
			if ( NDebugUI::InputString( "##Tag", Tag ) ) Session.Document.BeginEdit( *FString::Printf( "Rule.Tag.%d", i ) ).Settings.CollisionRules[i].Tag = Tag;
			ImGui::SameLine();
			int32 Collide = Rules[i].bCollide ? 1 : 0;
			ImGui::SetNextItemWidth( 120.0f );
			if ( ImGui::Combo( "##Collide", &Collide, "never collide\0collide\0" ) ) Session.Document.BeginEdit().Settings.CollisionRules[i].bCollide = Collide != 0;
			ImGui::SameLine();
			if ( ImGui::SmallButton( "x" ) )
			{
				NWorldEdit::RemoveCollisionRule( Session.Document, i );
				ImGui::PopID();
				break;
			}
			ImGui::SameLine();
			ImGui::TextDisabled( "%d tagged, decides %d", NumTagged, Summary.NumDecidedByRule.IsValidIndex( i ) ? Summary.NumDecidedByRule[i] : 0 );
			ImGui::SameLine();
			ImGui::BeginDisabled( NumTagged == 0 );
			if ( ImGui::SmallButton( "Select" ) ) Session.SelectByTag( Rules[i].Tag );
			ImGui::EndDisabled();
			ImGui::PopID();
		}
		if ( Rules.IsEmpty() ) ImGui::TextDisabled( "(no rules)" );

		ImGui::SetNextItemWidth( 130.0f );
		const bool bEnter = NDebugUI::InputStringWithHint( "##NewRule", "tag", S.NewRuleTag, ImGuiInputTextFlags_EnterReturnsTrue );
		ImGui::SameLine();
		if ( ( ImGui::Button( "Add rule" ) || bEnter ) && !S.NewRuleTag.TrimStartAndEnd().IsEmpty() )
		{
			NWorldEdit::SetCollisionRule( Session.Document, S.NewRuleTag, false );
			S.NewRuleTag = FString();
		}
	}
}

void NWorldEditorUI::DrawIssuesPanel( FWorldEditorState& S )
{
	if ( !BeginPanel( S, "World Issues", &S.bShowIssues, 0.31f, 0.67f, 0.28f, 0.31f ) ) return;
	if ( ImGui::BeginTabBar( "##IssuesTabs" ) )
	{
		if ( ImGui::BeginTabItem( "Issues" ) )
		{
			DrawIssuesTab( S );
			ImGui::EndTabItem();
		}
		if ( ImGui::BeginTabItem( "World settings" ) )
		{
			DrawSettingsTab( S );
			ImGui::EndTabItem();
		}
		ImGui::EndTabBar();
	}
	ImGui::End();
}
