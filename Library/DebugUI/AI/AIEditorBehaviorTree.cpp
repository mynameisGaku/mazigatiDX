// SPDX-License-Identifier: MIT
#include "AIEditorInternal.h"

#include "Library/Core/Types/CString.h"
#include "Library/DebugUI/AI/AIGraphLayout.h"
#include "Library/DebugUI/AI/AIGraphViews.h"
#include "Library/DebugUI/Common/EditorCommon.h"
#include "Library/Framework/AI/Registry/AIRegistry.h"
#include "Library/Math/MathUtils.h"

//=============================================================================
// ビヘイビアツリーのタブ
//   左: ノードのグラフ（自動で並ぶ。クリックで選択、右クリックのメニューで追加・複製・削除など、ドラッグで動かす）
//   右: 選んだノードの Inspector（名前・種類・Task とそのパラメータ（スキーマから自動）・デコレーター・サービス）
//   下: Issues（部品の名前・ブラックボードのキーなどの問題）
//   ノードを動かすときは、ほかのノードの「左 1/4 = その前へ」「右 1/4 = その後ろへ」「真ん中 = その子の最後へ」にドロップする。
//   兄弟の左右の順が優先順位。座標は保存しない（自動配置）。
//=============================================================================
namespace
{
	using FState = FAIEditorState;

	const ImU32 ColorText = IM_COL32( 235, 235, 240, 255 );
	const ImU32 ColorDimText = IM_COL32( 150, 152, 160, 255 );
	const ImU32 ColorBorder = IM_COL32( 95, 98, 108, 255 );
	const ImU32 ColorAccent = IM_COL32( 110, 190, 255, 255 );
	const ImU32 ColorDrop = IM_COL32( 110, 255, 140, 255 );
	const ImU32 ColorBad = IM_COL32( 255, 120, 110, 255 );
	const ImU32 ColorLink = IM_COL32( 120, 124, 136, 255 );
	const ImVec4 InspectorAccent( 0.55f, 0.78f, 1.0f, 1.0f );

	FString Truncated( const FString& Text, int32 MaxChars )
	{
		return Text.Len() > MaxChars ? Text.Left( MaxChars - 1 ) + "~" : Text;
	}

	ImVec2 Add( const ImVec2& A, const ImVec2& B )
	{
		return ImVec2( A.x + B.x, A.y + B.y );
	}

	ImU32 TypeFill( EBTNodeType Type )
	{
		switch ( Type )
		{
		case EBTNodeType::Selector:
			return IM_COL32( 44, 54, 84, 255 );
		case EBTNodeType::Sequence:
			return IM_COL32( 40, 74, 66, 255 );
		case EBTNodeType::Parallel:
			return IM_COL32( 72, 54, 84, 255 );
		default:
			return IM_COL32( 56, 56, 62, 255 );
		}
	}

	bool IsValidNode( const FBehaviorTreeDefinition& Definition, int32 Node )
	{
		return Node >= 0 && Node < Definition.Nodes.Num();
	}

	FBTDecoratorDef MakeDecorator( EBTDecoratorType Type )
	{
		FBTDecoratorDef Decorator;
		Decorator.Type = Type;
		switch ( Type )
		{
		case EBTDecoratorType::Cooldown:
			Decorator.Seconds = 1.0f;
			break;
		case EBTDecoratorType::TimeLimit:
			Decorator.Seconds = 5.0f;
			break;
		case EBTDecoratorType::BlackboardCondition:
			Decorator.Value = true;
			break;
		default:
			break;
		}
		return Decorator;
	}

	// 選んでいるノードの下（選んでいるのが葉なら、その親の下）へ足す。足したノードを選ぶ
	void AddNodeFromMenu( FState& S, int32 Parent, EBTNodeType Type, const FString& TaskName )
	{
		const FBehaviorTreeDefinition& Definition = S.BehaviorTree.Get();
		if ( IsValidNode( Definition, Parent ) && Definition.Nodes[Parent].Type == EBTNodeType::Task )
		{
			Parent = Definition.Nodes[Parent].Parent;
		}
		const int32 Added = NBehaviorTreeEdit::AddNode( S.BehaviorTree, Parent, Type, TaskName );
		if ( Added != INDEX_NONE )
		{
			S.SelectedNode = Added;
		}
		else
		{
			NAIEditor::SetStatus( S, "Cannot add a node here (the tree already has a root, or the parent is a leaf).", true );
		}
	}

	// 「子を足す」のメニューの中身（Selector / Sequence / Parallel と、登録した Task）
	void DrawAddChildItems( FState& S, int32 Parent )
	{
		if ( ImGui::MenuItem( "Selector" ) )
		{
			AddNodeFromMenu( S, Parent, EBTNodeType::Selector, FString() );
		}
		if ( ImGui::MenuItem( "Sequence" ) )
		{
			AddNodeFromMenu( S, Parent, EBTNodeType::Sequence, FString() );
		}
		if ( ImGui::MenuItem( "Parallel" ) )
		{
			AddNodeFromMenu( S, Parent, EBTNodeType::Parallel, FString() );
		}
		ImGui::Separator();
		if ( ImGui::BeginMenu( "Task" ) )
		{
			const FString Name = NAIEditor::DrawPartMenuItems( NAIEditor::EPartKind::Task );
			if ( !Name.IsEmpty() )
			{
				AddNodeFromMenu( S, Parent, EBTNodeType::Task, Name );
				ImGui::CloseCurrentPopup();
			}
			ImGui::EndMenu();
		}
	}

	void DeleteSelected( FState& S )
	{
		if ( IsValidNode( S.BehaviorTree.Get(), S.SelectedNode ) )
		{
			NBehaviorTreeEdit::RemoveNode( S.BehaviorTree, S.SelectedNode );
			S.SelectedNode = INDEX_NONE;
		}
	}

	void CopySelected( FState& S )
	{
		if ( IsValidNode( S.BehaviorTree.Get(), S.SelectedNode ) )
		{
			S.Clipboard = NBehaviorTreeEdit::CopySubtree( S.BehaviorTree.Get(), S.SelectedNode );
		}
	}

	void PasteUnderSelected( FState& S )
	{
		if ( S.Clipboard.is_null() )
		{
			return;
		}
		const FBehaviorTreeDefinition& Definition = S.BehaviorTree.Get();
		int32 Parent = Definition.Nodes.IsEmpty() ? -1 : ( IsValidNode( Definition, S.SelectedNode ) ? S.SelectedNode : 0 );
		if ( Parent >= 0 && Definition.Nodes[Parent].Type == EBTNodeType::Task )
		{
			Parent = Definition.Nodes[Parent].Parent;
		}
		const int32 Pasted = NBehaviorTreeEdit::PasteSubtree( S.BehaviorTree, Parent, INDEX_NONE, S.Clipboard );
		if ( Pasted != INDEX_NONE )
		{
			S.SelectedNode = Pasted;
		}
	}

	void DuplicateSelected( FState& S )
	{
		const int32 Copy = NBehaviorTreeEdit::DuplicateNode( S.BehaviorTree, S.SelectedNode );
		if ( Copy != INDEX_NONE )
		{
			S.SelectedNode = Copy;
		}
	}

	// ノードの右クリックのメニュー
	void DrawNodeMenu( FState& S )
	{
		const FBehaviorTreeDefinition& Definition = S.BehaviorTree.Get();
		const int32 Node = S.SelectedNode;
		if ( !IsValidNode( Definition, Node ) )
		{
			ImGui::TextDisabled( "(no node selected)" );
			return;
		}
		const bool bIsTask = Definition.Nodes[Node].Type == EBTNodeType::Task;

		ImGui::BeginDisabled( bIsTask );
		if ( ImGui::BeginMenu( "Add child" ) )
		{
			DrawAddChildItems( S, Node );
			ImGui::EndMenu();
		}
		ImGui::EndDisabled();

		if ( ImGui::BeginMenu( "Add decorator" ) )
		{
			for ( uint8 Index = 0; Index <= static_cast<uint8>( EBTDecoratorType::TimeLimit ); ++Index )
			{
				const EBTDecoratorType Type = static_cast<EBTDecoratorType>( Index );
				if ( ImGui::MenuItem( NAI::ToString( Type ) ) )
				{
					NBehaviorTreeEdit::AddDecorator( S.BehaviorTree, Node, MakeDecorator( Type ) );
				}
			}
			ImGui::EndMenu();
		}
		if ( ImGui::BeginMenu( "Add service" ) )
		{
			const FString Name = NAIEditor::DrawPartMenuItems( NAIEditor::EPartKind::Service );
			if ( !Name.IsEmpty() )
			{
				FBTServiceDef Service;
				Service.Call.Do = Name;
				NBehaviorTreeEdit::AddService( S.BehaviorTree, Node, Service );
				ImGui::CloseCurrentPopup();
			}
			ImGui::EndMenu();
		}
		if ( ImGui::BeginMenu( "Wrap in" ) )
		{
			if ( ImGui::MenuItem( "Selector" ) )
			{
				S.SelectedNode = NBehaviorTreeEdit::WrapNode( S.BehaviorTree, Node, EBTNodeType::Selector );
			}
			if ( ImGui::MenuItem( "Sequence" ) )
			{
				S.SelectedNode = NBehaviorTreeEdit::WrapNode( S.BehaviorTree, Node, EBTNodeType::Sequence );
			}
			if ( ImGui::MenuItem( "Parallel" ) )
			{
				S.SelectedNode = NBehaviorTreeEdit::WrapNode( S.BehaviorTree, Node, EBTNodeType::Parallel );
			}
			ImGui::EndMenu();
		}
		if ( ImGui::BeginMenu( "Change type" ) )
		{
			const bool bHasChildren = !Definition.GetChildren( Node ).IsEmpty();
			const EBTNodeType Current = Definition.Nodes[Node].Type;
			if ( ImGui::MenuItem( "Selector", nullptr, Current == EBTNodeType::Selector ) )
			{
				NBehaviorTreeEdit::ChangeNodeType( S.BehaviorTree, Node, EBTNodeType::Selector );
			}
			if ( ImGui::MenuItem( "Sequence", nullptr, Current == EBTNodeType::Sequence ) )
			{
				NBehaviorTreeEdit::ChangeNodeType( S.BehaviorTree, Node, EBTNodeType::Sequence );
			}
			if ( ImGui::MenuItem( "Parallel", nullptr, Current == EBTNodeType::Parallel ) )
			{
				NBehaviorTreeEdit::ChangeNodeType( S.BehaviorTree, Node, EBTNodeType::Parallel );
			}
			if ( ImGui::MenuItem( "Task", nullptr, Current == EBTNodeType::Task, !bHasChildren ) )
			{
				NBehaviorTreeEdit::ChangeNodeType( S.BehaviorTree, Node, EBTNodeType::Task );
			}
			ImGui::EndMenu();
		}

		ImGui::Separator();
		if ( ImGui::MenuItem( "Copy", "Ctrl+C" ) )
		{
			CopySelected( S );
		}
		if ( ImGui::MenuItem( "Paste as child", "Ctrl+V", false, !S.Clipboard.is_null() && !bIsTask ) )
		{
			PasteUnderSelected( S );
		}
		if ( ImGui::MenuItem( "Duplicate", "Ctrl+D", false, Node > 0 ) )
		{
			DuplicateSelected( S );
		}
		if ( ImGui::MenuItem( "Delete", "Del" ) )
		{
			DeleteSelected( S );
		}
	}

	//=========================================================================
	// グラフ
	//=========================================================================
	struct FDropTarget
	{
		int32 Node = INDEX_NONE;
		// 0 = その前、1 = その子の最後、2 = その後ろ
		int32 Zone = 1;
	};

	bool IsDescendantOf( const FBehaviorTreeDefinition& Definition, int32 Node, int32 Ancestor )
	{
		for ( int32 Current = Node; Current >= 0; Current = Definition.Nodes[Current].Parent )
		{
			if ( Current == Ancestor )
			{
				return true;
			}
		}
		return false;
	}

	void DrawGraph( FState& S )
	{
		const FBehaviorTreeDefinition& Definition = S.BehaviorTree.Get();
		if ( Definition.Nodes.IsEmpty() )
		{
			ImGui::TextDisabled( "The tree is empty." );
			if ( ImGui::Button( "Add the root node..." ) )
			{
				ImGui::OpenPopup( "##btaddroot" );
			}
			if ( ImGui::BeginPopup( "##btaddroot" ) )
			{
				DrawAddChildItems( S, -1 );
				ImGui::EndPopup();
			}
			return;
		}

		const FBehaviorTreeGraphLayout Layout = NAIGraph::BuildBehaviorTreeLayout( Definition );
		FNodeGraphCanvas Canvas;
		if ( !Canvas.Begin( "##btcanvas", S.BehaviorTreeView, ImVec2( 0.0f, 0.0f ), Layout.Content ) )
		{
			return;
		}
		const int32 NumNodes = Definition.Nodes.Num();

		// 親から子への線（選んでいるノードの線は、強調する）
		for ( int32 Node = 0; Node < NumNodes; ++Node )
		{
			if ( !Layout.Links[Node].IsEmpty() )
			{
				const bool bSelected = Node == S.SelectedNode;
				Canvas.DrawElbowArrow( Layout.Links[Node].GetData(), Layout.Links[Node].Num(), bSelected ? ColorAccent : ColorLink, bSelected ? 3.0f : 1.8f );
			}
		}
		for ( const FBehaviorTreeGraphRelay& Relay : Layout.Relays )
		{
			Canvas.DrawRelay( Relay.Pos, ColorBorder );
		}

		// ノード
		int32 Hovered = INDEX_NONE;
		const CAIRegistry& Registry = NAI::GetRegistry();
		for ( int32 Node = 0; Node < NumNodes; ++Node )
		{
			const FBTNodeDef& Def = Definition.Nodes[Node];
			const ImVec2 Min = Layout.Pos[Node];
			const ImVec2 Size = Layout.Size[Node];
			const bool bUnregistered = Def.Type == EBTNodeType::Task && Registry.FindTask( Def.Task.Do ) == nullptr;
			const bool bHovered = Canvas.DrawBox( Min, Size, TypeFill( Def.Type ), bUnregistered ? ColorBad : ColorBorder, 1.4f, 7.0f );
			if ( bHovered )
			{
				Hovered = Node;
			}

			if ( Def.Parent >= 0 )
			{
				const TArray<int32> Siblings = Definition.GetChildren( Def.Parent );
				Canvas.DrawText( Add( Min, ImVec2( Size.x - 22.0f, 9.0f ) ), ColorDimText, *FString::Printf( "#%d", Siblings.Find( Node ) + 1 ), 11.0f );
			}
			Canvas.DrawText( Add( Min, ImVec2( 8.0f, 8.0f ) ), ColorText, *Truncated( Definition.GetNodeLabel( Node ), 24 ), 14.0f, Min, ImVec2( Size.x - 26.0f, Size.y ) );
			FString Sub = Def.Type == EBTNodeType::Task ? FString::Printf( "<%s>", *Def.Task.Do ) : FString( NAI::ToString( Def.Type ) );
			if ( Def.Type == EBTNodeType::Parallel )
			{
				Sub += FString::Printf( " %s", NAI::ToString( Def.ParallelMode ) );
			}
			Canvas.DrawText( Add( Min, ImVec2( 8.0f, 25.0f ) ), bUnregistered ? ColorBad : ColorDimText, *Sub, 11.0f, Min, Size );

			float32 LineY = NAIGraph::TreeHeaderHeight + 2.0f;
			for ( const FBTDecoratorDef& Decorator : Def.Decorators )
			{
				Canvas.DrawText( Add( Min, ImVec2( 8.0f, LineY ) ), ColorText, *NDebugUI::DescribeBehaviorDecorator( Decorator ), 11.0f, Min, Size );
				LineY += NAIGraph::TreeLineHeight;
			}
			for ( const FBTServiceDef& Service : Def.Services )
			{
				Canvas.DrawText( Add( Min, ImVec2( 8.0f, LineY ) ), ColorDimText, *FString::Printf( "service %s / %g s", *Service.Call.Do, static_cast<float64>( Service.Interval ) ), 11.0f, Min, Size );
				LineY += NAIGraph::TreeLineHeight;
			}
		}

		// 選択の枠
		if ( IsValidNode( Definition, S.SelectedNode ) )
		{
			Canvas.DrawOutline( Add( Layout.Pos[S.SelectedNode], ImVec2( -3.0f, -3.0f ) ), Add( Layout.Size[S.SelectedNode], ImVec2( 6.0f, 6.0f ) ), ColorAccent, 2.5f, 9.0f );
		}

		// 入力: クリックで選択、ドラッグで動かす、右クリックでメニュー
		const ImGuiIO& IO = ImGui::GetIO();
		bool bOpenMenu = false;
		if ( Canvas.IsHovered() && Hovered != INDEX_NONE && ImGui::IsMouseClicked( ImGuiMouseButton_Left ) )
		{
			S.SelectedNode = Hovered;
			S.DragNode = Hovered;
			S.DragStart = IO.MousePos;
			S.bDragging = false;
		}
		if ( Canvas.IsHovered() && Hovered != INDEX_NONE && ImGui::IsMouseClicked( ImGuiMouseButton_Right ) )
		{
			S.SelectedNode = Hovered;
			bOpenMenu = true;
		}
		if ( Canvas.IsHovered() && Hovered == INDEX_NONE && ImGui::IsMouseClicked( ImGuiMouseButton_Left ) )
		{
			S.SelectedNode = INDEX_NONE;
		}
		if ( S.DragNode != INDEX_NONE && !IsValidNode( Definition, S.DragNode ) )
		{
			S.DragNode = INDEX_NONE;
			S.bDragging = false;
		}
		// 放した瞬間（動かしていたノードを、このフレームで置く）
		bool bDropNow = false;
		int32 DropNode = INDEX_NONE;
		if ( S.DragNode != INDEX_NONE )
		{
			if ( ImGui::IsMouseDown( ImGuiMouseButton_Left ) )
			{
				const float Dx = IO.MousePos.x - S.DragStart.x;
				const float Dy = IO.MousePos.y - S.DragStart.y;
				if ( !S.bDragging && S.DragNode > 0 && Dx * Dx + Dy * Dy > 36.0f )
				{
					S.bDragging = true;
				}
			}
			else
			{
				bDropNow = S.bDragging;
				DropNode = S.DragNode;
				S.DragNode = INDEX_NONE;
				S.bDragging = false;
			}
		}
		const int32 ActiveDrag = S.bDragging ? S.DragNode : ( bDropNow ? DropNode : INDEX_NONE );

		// ドロップ先（動かしているノードの子孫・自分自身は、先にできない）
		FDropTarget Target;
		if ( ActiveDrag != INDEX_NONE )
		{
			for ( int32 Node = 0; Node < NumNodes; ++Node )
			{
				if ( IsDescendantOf( Definition, Node, ActiveDrag ) )
				{
					continue;
				}
				const ImVec2 ScreenMin = Canvas.ToScreen( Layout.Pos[Node] );
				const ImVec2 ScreenMax = Canvas.ToScreen( Add( Layout.Pos[Node], Layout.Size[Node] ) );
				if ( !ImGui::IsMouseHoveringRect( ScreenMin, ScreenMax ) )
				{
					continue;
				}
				const float Fraction = ( IO.MousePos.x - ScreenMin.x ) / NMath::Max( ScreenMax.x - ScreenMin.x, 1.0f );
				Target.Node = Node;
				if ( Fraction < 0.25f && Node > 0 )
				{
					Target.Zone = 0;
				}
				else if ( Fraction > 0.75f && Node > 0 )
				{
					Target.Zone = 2;
				}
				else
				{
					Target.Zone = 1;
					// 葉の子にはできない
					if ( Definition.Nodes[Node].Type == EBTNodeType::Task )
					{
						Target.Node = INDEX_NONE;
					}
				}
				break;
			}
			// 印（動かしている間だけ）
			if ( S.bDragging && Target.Node != INDEX_NONE )
			{
				const ImVec2 Min = Layout.Pos[Target.Node];
				const ImVec2 Size = Layout.Size[Target.Node];
				if ( Target.Zone == 1 )
				{
					Canvas.DrawOutline( Min, Size, ColorDrop, 3.0f, 7.0f );
				}
				else
				{
					const float X = Target.Zone == 0 ? Min.x - 6.0f : Min.x + Size.x + 6.0f;
					Canvas.DrawLine( ImVec2( X, Min.y ), ImVec2( X, Min.y + Size.y ), ColorDrop, 4.0f );
				}
			}
			if ( S.bDragging )
			{
				Canvas.DrawOutline( Layout.Pos[ActiveDrag], Layout.Size[ActiveDrag], ColorAccent, 2.0f, 7.0f );
			}
		}

		// 放した: 動かす
		if ( bDropNow && Target.Node != INDEX_NONE )
		{
			int32 NewParent = Target.Node;
			int32 At = INDEX_NONE;
			if ( Target.Zone != 1 )
			{
				NewParent = Definition.Nodes[Target.Node].Parent;
				const TArray<int32> Siblings = Definition.GetChildren( NewParent );
				At = Siblings.Find( Target.Node );
				if ( Definition.Nodes[DropNode].Parent == NewParent && Siblings.Find( DropNode ) < At )
				{
					--At;
				}
				At += Target.Zone == 2 ? 1 : 0;
			}
			int32 NewIndex = INDEX_NONE;
			if ( NBehaviorTreeEdit::MoveNode( S.BehaviorTree, DropNode, NewParent, At, &NewIndex ) )
			{
				S.SelectedNode = NewIndex;
			}
		}

		// キー操作（文字を入力していないとき）
		if ( ImGui::IsWindowFocused( ImGuiFocusedFlags_RootAndChildWindows ) && !IO.WantTextInput )
		{
			if ( ImGui::IsKeyPressed( ImGuiKey_Delete ) )
			{
				DeleteSelected( S );
			}
			if ( IO.KeyCtrl && ImGui::IsKeyPressed( ImGuiKey_C ) )
			{
				CopySelected( S );
			}
			if ( IO.KeyCtrl && ImGui::IsKeyPressed( ImGuiKey_V ) )
			{
				PasteUnderSelected( S );
			}
			if ( IO.KeyCtrl && ImGui::IsKeyPressed( ImGuiKey_D ) )
			{
				DuplicateSelected( S );
			}
		}

		// ノードの上にいるあいだ・動かしているあいだは、ドラッグでパンしない（次のフレームから）
		S.BehaviorTreeView.bPanBlocked = Hovered != INDEX_NONE || S.bDragging;
		Canvas.End();

		if ( bOpenMenu )
		{
			ImGui::OpenPopup( "##btnodemenu" );
		}
		if ( ImGui::BeginPopup( "##btnodemenu" ) )
		{
			DrawNodeMenu( S );
			ImGui::EndPopup();
		}
	}

	//=========================================================================
	// Inspector
	//=========================================================================
	// 左に名前、右に入力欄を出す準備
	void Field( const char* Name )
	{
		ImGui::AlignTextToFramePadding();
		ImGui::TextUnformatted( Name );
		ImGui::SameLine( 110.0f );
		ImGui::SetNextItemWidth( -FLT_MIN );
	}

	void DrawDecoratorEditor( FState& S, int32 Node, int32 Index, int32& RemoveIndex, int32& MoveFrom, int32& MoveTo )
	{
		const FBehaviorTreeDefinition& Definition = S.BehaviorTree.Get();
		const int32 NumDecorators = Definition.Nodes[Node].Decorators.Num();
		FBTDecoratorDef Edited = Definition.Nodes[Node].Decorators[Index];
		bool bChanged = false;
		const FAIInspectContext Context = NAIEditor::MakeInspectContext( S, Definition.Blackboard );

		ImGui::PushID( 1000 + Index );
		ImGui::TextColored( InspectorAccent, "%s", NAI::ToString( Edited.Type ) );
		ImGui::SameLine();
		if ( ImGui::SmallButton( "^" ) && Index > 0 )
		{
			MoveFrom = Index;
			MoveTo = Index - 1;
		}
		ImGui::SameLine();
		if ( ImGui::SmallButton( "v" ) && Index + 1 < NumDecorators )
		{
			MoveFrom = Index;
			MoveTo = Index + 1;
		}
		ImGui::SameLine();
		if ( ImGui::SmallButton( "x" ) )
		{
			RemoveIndex = Index;
		}

		const bool bIsCondition = Edited.Type == EBTDecoratorType::BlackboardCondition || Edited.Type == EBTDecoratorType::Condition;
		switch ( Edited.Type )
		{
		case EBTDecoratorType::BlackboardCondition:
		{
			Field( "Key" );
			bChanged |= NDebugUI::DrawBlackboardKeyCombo( "##key", Edited.Key, Context );
			const int32 KeyIndex = Context.Blackboard != nullptr ? Context.Blackboard->FindKeyIndex( Edited.Key ) : -1;
			const FBlackboardKeyDefinition* KeyDefinition = KeyIndex >= 0 ? &Context.Blackboard->Keys[KeyIndex] : nullptr;
			Field( "Operator" );
			bChanged |= NDebugUI::DrawCompareOpCombo( "##op", Edited.Op, KeyDefinition );
			Field( "Value" );
			bChanged |= NDebugUI::DrawBlackboardValueInput( "##value", KeyDefinition, Edited.Value );
			break;
		}
		case EBTDecoratorType::Condition:
			bChanged |= NAIEditor::DrawCallEditor( S, "Condition", Edited.Call, NAIEditor::EPartKind::Condition, Definition.Blackboard );
			break;
		case EBTDecoratorType::Cooldown:
		case EBTDecoratorType::TimeLimit:
			Field( "Seconds" );
			bChanged |= ImGui::DragFloat( "##seconds", &Edited.Seconds, 0.05f, 0.0f, 3600.0f );
			break;
		case EBTDecoratorType::Repeat:
			Field( "Count" );
			bChanged |= ImGui::InputInt( "##count", &Edited.Count );
			if ( Edited.Count < 0 )
			{
				Edited.Count = 0;
			}
			if ( ImGui::IsItemHovered() )
			{
				ImGui::SetTooltip( "0 = repeat forever." );
			}
			Field( "Until success" );
			bChanged |= ImGui::Checkbox( "##untilsuccess", &Edited.bUntilSuccess );
			break;
		default:
			break;
		}
		if ( bIsCondition )
		{
			Field( "Not" );
			bChanged |= ImGui::Checkbox( "##not", &Edited.bNot );
			Field( "Abort" );
			if ( ImGui::BeginCombo( "##abort", NAI::ToString( Edited.Abort ) ) )
			{
				for ( uint8 Mode = 0; Mode <= static_cast<uint8>( EBTAbortMode::Both ); ++Mode )
				{
					const EBTAbortMode Candidate = static_cast<EBTAbortMode>( Mode );
					if ( ImGui::Selectable( NAI::ToString( Candidate ), Candidate == Edited.Abort ) )
					{
						Edited.Abort = Candidate;
						bChanged = true;
					}
				}
				ImGui::EndCombo();
			}
			if ( ImGui::IsItemHovered() )
			{
				ImGui::SetTooltip( "What happens when the condition changes while running:\nNone: only checked on entry\nSelf: abort this branch when it turns false\nLowerPriority: abort lower priority siblings when it turns true\nBoth: both" );
			}
		}
		if ( bChanged )
		{
			char CoalesceKey[40];
			NCString::Format( CoalesceKey, sizeof( CoalesceKey ), "decorator%d_%d", Node, Index );
			NBehaviorTreeEdit::SetDecorator( S.BehaviorTree, Node, Index, Edited, CoalesceKey );
		}
		ImGui::Separator();
		ImGui::PopID();
	}

	void DrawServiceEditor( FState& S, int32 Node, int32 Index, int32& RemoveIndex )
	{
		const FBehaviorTreeDefinition& Definition = S.BehaviorTree.Get();
		FBTServiceDef Edited = Definition.Nodes[Node].Services[Index];
		bool bChanged = false;
		ImGui::PushID( 2000 + Index );
		ImGui::TextColored( InspectorAccent, "Service" );
		ImGui::SameLine();
		if ( ImGui::SmallButton( "x" ) )
		{
			RemoveIndex = Index;
		}
		bChanged |= NAIEditor::DrawCallEditor( S, "Service", Edited.Call, NAIEditor::EPartKind::Service, Definition.Blackboard );
		Field( "Interval (s)" );
		bChanged |= ImGui::DragFloat( "##interval", &Edited.Interval, 0.01f, 0.0f, 600.0f );
		if ( ImGui::IsItemHovered() )
		{
			ImGui::SetTooltip( "0 or less = every frame." );
		}
		if ( bChanged )
		{
			char CoalesceKey[40];
			NCString::Format( CoalesceKey, sizeof( CoalesceKey ), "service%d_%d", Node, Index );
			NBehaviorTreeEdit::SetService( S.BehaviorTree, Node, Index, Edited, CoalesceKey );
		}
		ImGui::Separator();
		ImGui::PopID();
	}

	void DrawInspector( FState& S )
	{
		const FBehaviorTreeDefinition& Definition = S.BehaviorTree.Get();
		const int32 Node = S.SelectedNode;
		if ( !IsValidNode( Definition, Node ) )
		{
			ImGui::TextWrapped( "Click a node to edit it. Right-click for the menu." );
			ImGui::TextWrapped( "Drag a node onto another one to move it: left quarter = before it, right quarter = after it, middle = its last child. The order of the children is the priority." );
			return;
		}
		const FBTNodeDef& Def = Definition.Nodes[Node];
		ImGui::TextColored( InspectorAccent, "Node %d: %s", Node, UI_TEXT( Definition.GetNodeLabel( Node ) ) );
		ImGui::Separator();

		Field( "Name" );
		FString Name = Def.Name;
		if ( NDebugUI::InputString( "##name", Name ) )
		{
			NBehaviorTreeEdit::SetNodeName( S.BehaviorTree, Node, Name, "nodename" );
		}

		Field( "Type" );
		if ( ImGui::BeginCombo( "##type", NAI::ToString( Def.Type ) ) )
		{
			const bool bHasChildren = !Definition.GetChildren( Node ).IsEmpty();
			for ( uint8 Index = 0; Index <= static_cast<uint8>( EBTNodeType::Task ); ++Index )
			{
				const EBTNodeType Candidate = static_cast<EBTNodeType>( Index );
				ImGui::BeginDisabled( Candidate == EBTNodeType::Task && bHasChildren );
				if ( ImGui::Selectable( NAI::ToString( Candidate ), Candidate == Def.Type ) )
				{
					NBehaviorTreeEdit::ChangeNodeType( S.BehaviorTree, Node, Candidate );
				}
				ImGui::EndDisabled();
			}
			ImGui::EndCombo();
		}

		if ( Def.Type == EBTNodeType::Task )
		{
			FAICall Call = Def.Task;
			if ( NAIEditor::DrawCallEditor( S, "Task", Call, NAIEditor::EPartKind::Task, Definition.Blackboard ) )
			{
				char CoalesceKey[32];
				NCString::Format( CoalesceKey, sizeof( CoalesceKey ), "task%d", Node );
				NBehaviorTreeEdit::SetNodeTask( S.BehaviorTree, Node, Call, CoalesceKey );
			}
		}
		else if ( Def.Type == EBTNodeType::Parallel )
		{
			Field( "Mode" );
			if ( ImGui::BeginCombo( "##mode", NAI::ToString( Def.ParallelMode ) ) )
			{
				for ( uint8 Index = 0; Index <= static_cast<uint8>( EBTParallelMode::Primary ); ++Index )
				{
					const EBTParallelMode Candidate = static_cast<EBTParallelMode>( Index );
					if ( ImGui::Selectable( NAI::ToString( Candidate ), Candidate == Def.ParallelMode ) )
					{
						NBehaviorTreeEdit::SetParallelMode( S.BehaviorTree, Node, Candidate );
					}
				}
				ImGui::EndCombo();
			}
		}

		// デコレーター
		ImGui::SeparatorText( "Decorators" );
		int32 RemoveDecorator = INDEX_NONE;
		int32 MoveFrom = INDEX_NONE;
		int32 MoveTo = INDEX_NONE;
		for ( int32 Index = 0; Index < Def.Decorators.Num(); ++Index )
		{
			DrawDecoratorEditor( S, Node, Index, RemoveDecorator, MoveFrom, MoveTo );
		}
		if ( RemoveDecorator != INDEX_NONE )
		{
			NBehaviorTreeEdit::RemoveDecorator( S.BehaviorTree, Node, RemoveDecorator );
		}
		if ( MoveFrom != INDEX_NONE )
		{
			NBehaviorTreeEdit::MoveDecorator( S.BehaviorTree, Node, MoveFrom, MoveTo );
		}
		if ( ImGui::Button( "+ Decorator" ) )
		{
			ImGui::OpenPopup( "##adddecorator" );
		}
		if ( ImGui::BeginPopup( "##adddecorator" ) )
		{
			for ( uint8 Index = 0; Index <= static_cast<uint8>( EBTDecoratorType::TimeLimit ); ++Index )
			{
				const EBTDecoratorType Type = static_cast<EBTDecoratorType>( Index );
				if ( ImGui::MenuItem( NAI::ToString( Type ) ) )
				{
					NBehaviorTreeEdit::AddDecorator( S.BehaviorTree, Node, MakeDecorator( Type ) );
				}
			}
			ImGui::EndPopup();
		}

		// サービス
		ImGui::SeparatorText( "Services" );
		int32 RemoveService = INDEX_NONE;
		for ( int32 Index = 0; Index < Def.Services.Num(); ++Index )
		{
			DrawServiceEditor( S, Node, Index, RemoveService );
		}
		if ( RemoveService != INDEX_NONE )
		{
			NBehaviorTreeEdit::RemoveService( S.BehaviorTree, Node, RemoveService );
		}
		if ( ImGui::Button( "+ Service" ) )
		{
			ImGui::OpenPopup( "##addservice" );
		}
		if ( ImGui::BeginPopup( "##addservice" ) )
		{
			const FString ServiceName = NAIEditor::DrawPartMenuItems( NAIEditor::EPartKind::Service );
			if ( !ServiceName.IsEmpty() )
			{
				FBTServiceDef Service;
				Service.Call.Do = ServiceName;
				NBehaviorTreeEdit::AddService( S.BehaviorTree, Node, Service );
				ImGui::CloseCurrentPopup();
			}
			ImGui::EndPopup();
		}
	}
}

void NAIEditor::DrawBehaviorTreeTab( FAIEditorState& S )
{
	if ( !IsValidNode( S.BehaviorTree.Get(), S.SelectedNode ) )
	{
		S.SelectedNode = INDEX_NONE;
	}

	// ツールバー
	if ( ImGui::Button( "Fit" ) )
	{
		S.BehaviorTreeView.bFitted = false;
	}
	ImGui::SameLine();
	if ( ImGui::Button( "Add node..." ) )
	{
		ImGui::OpenPopup( "##btaddmenu" );
	}
	if ( ImGui::BeginPopup( "##btaddmenu" ) )
	{
		const FBehaviorTreeDefinition& Definition = S.BehaviorTree.Get();
		DrawAddChildItems( S, Definition.Nodes.IsEmpty() ? -1 : ( IsValidNode( Definition, S.SelectedNode ) ? S.SelectedNode : 0 ) );
		ImGui::EndPopup();
	}
	ImGui::SameLine();
	ImGui::BeginDisabled( !IsValidNode( S.BehaviorTree.Get(), S.SelectedNode ) );
	if ( ImGui::Button( "Delete" ) )
	{
		DeleteSelected( S );
	}
	ImGui::EndDisabled();
	ImGui::SameLine();
	ImGui::TextUnformatted( "Blackboard:" );
	ImGui::SameLine();
	ImGui::SetNextItemWidth( 260.0f );
	FString Blackboard = S.BehaviorTree.Get().Blackboard;
	if ( NDebugUI::DrawNameCombo( "##btblackboard", Blackboard, S.BlackboardFiles, "(none)" ) )
	{
		NBehaviorTreeEdit::SetBlackboard( S.BehaviorTree, Blackboard );
	}
	ImGui::SameLine();
	ImGui::BeginDisabled( S.BehaviorTree.Get().Blackboard.IsEmpty() );
	if ( ImGui::SmallButton( "Clear##btbb" ) )
	{
		NBehaviorTreeEdit::SetBlackboard( S.BehaviorTree, FString() );
	}
	ImGui::EndDisabled();

	// グラフと Inspector
	constexpr float InspectorWidth = 380.0f;
	const float Height = NMath::Max( ImGui::GetContentRegionAvail().y - 170.0f, 220.0f );
	const float GraphWidth = NMath::Max( ImGui::GetContentRegionAvail().x - InspectorWidth - ImGui::GetStyle().ItemSpacing.x, 200.0f );
	if ( ImGui::BeginChild( "##btgraph", ImVec2( GraphWidth, Height ), ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse ) )
	{
		DrawGraph( S );
	}
	ImGui::EndChild();
	ImGui::SameLine();
	if ( ImGui::BeginChild( "##btinspector", ImVec2( InspectorWidth, Height ), ImGuiChildFlags_Borders ) )
	{
		DrawInspector( S );
	}
	ImGui::EndChild();

	// ドラッグなどの連続した変更の区切り（どの項目も操作されていないとき）
	if ( !ImGui::IsAnyItemActive() )
	{
		S.BehaviorTree.EndEditSession();
	}
	NAIEditor::DrawIssues( S );
}
