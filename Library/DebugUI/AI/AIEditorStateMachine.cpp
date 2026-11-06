// SPDX-License-Identifier: MIT
#include "AIEditorInternal.h"

#include "Library/Core/Types/CString.h"
#include "Library/DebugUI/AI/AIGraphLayout.h"
#include "Library/DebugUI/Common/EditorCommon.h"
#include "Library/Framework/AI/Registry/AIRegistry.h"
#include "Library/Math/MathUtils.h"

//=============================================================================
// ステートマシンのタブ
//   左: ステートのグラフ（自由に置ける。ドラッグで動かすと、座標が定義の Editor 欄に保存される。矢印をクリックで遷移を選ぶ。右クリックでメニュー）
//   右: 選んだステート・遷移の Inspector（名前・Machine・クリップ・Entry・処理（OnEnter / OnTick / OnExit）・アニメーションの出来事・遷移の条件）
//   下: Issues
//   グラフの描画と配置は、AI デバッガと同じ部品（NodeGraph.h・AIGraphLayout.h）。座標が無いステートは、自動で並ぶ。
//=============================================================================
namespace
{
	using FState = FAIEditorState;

	const ImU32 ColorText = IM_COL32( 235, 235, 240, 255 );
	const ImU32 ColorDimText = IM_COL32( 150, 152, 160, 255 );
	const ImU32 ColorBorder = IM_COL32( 95, 98, 108, 255 );
	const ImU32 ColorNodeFill = IM_COL32( 46, 48, 58, 255 );
	const ImU32 ColorContainerFill = IM_COL32( 36, 38, 48, 255 );
	const ImU32 ColorAccent = IM_COL32( 110, 190, 255, 255 );
	const ImU32 ColorArrow = IM_COL32( 130, 134, 148, 255 );
	const ImU32 ColorAnyArrow = IM_COL32( 170, 125, 80, 255 );
	const ImU32 ColorAnyFill = IM_COL32( 70, 52, 34, 255 );
	const ImU32 ColorOrange = IM_COL32( 255, 165, 80, 255 );
	const ImVec4 InspectorAccent( 0.55f, 0.78f, 1.0f, 1.0f );

	ImVec2 Add( const ImVec2& A, const ImVec2& B )
	{
		return ImVec2( A.x + B.x, A.y + B.y );
	}

	bool IsValidState( const FStateMachineDefinition& Definition, int32 State )
	{
		return State >= 0 && State < Definition.States.Num();
	}

	void Field( const char* Name )
	{
		ImGui::AlignTextToFramePadding();
		ImGui::TextUnformatted( Name );
		ImGui::SameLine( 110.0f );
		ImGui::SetNextItemWidth( -FLT_MIN );
	}

	const char* ConditionTypeName( EStateConditionType Type )
	{
		switch ( Type )
		{
		case EStateConditionType::BlackboardCompare:
			return "Blackboard compare";
		case EStateConditionType::Event:
			return "Event";
		case EStateConditionType::ClipFinished:
			return "Clip finished";
		case EStateConditionType::ElapsedTime:
			return "Elapsed time";
		default:
			return "Custom";
		}
	}

	FString DescribeCondition( const FStateConditionDef& Condition )
	{
		FString Text;
		switch ( Condition.Type )
		{
		case EStateConditionType::BlackboardCompare:
			Text = FString::Printf( "%s %s %s", *Condition.Key, NAI::ToString( Condition.Op ), *NJson::FromJsonString( Condition.Value.dump() ) );
			break;
		case EStateConditionType::Event:
			Text = "event '" + Condition.Event + "'";
			break;
		case EStateConditionType::ClipFinished:
			Text = "clip finished";
			break;
		case EStateConditionType::ElapsedTime:
			Text = FString::Printf( "time >= %g s", static_cast<float64>( Condition.Seconds ) );
			break;
		default:
			Text = "'" + Condition.Call.Do + "'";
			break;
		}
		return Condition.bNot ? "not " + Text : Text;
	}

	FString DescribeTransition( const FStateTransitionDef& Transition )
	{
		FString Text = "-> " + Transition.To;
		if ( !Transition.When.IsEmpty() )
		{
			Text += " when ";
			for ( int32 Index = 0; Index < Transition.When.Num(); ++Index )
			{
				Text += ( Index > 0 ? ( Transition.bAnyOf ? " or " : " and " ) : "" ) + DescribeCondition( Transition.When[Index] );
			}
		}
		return Text;
	}

	const TArray<FStateTransitionDef>& TransitionList( const FStateMachineDefinition& Definition, int32 Machine, bool bAny )
	{
		if ( Machine < 0 )
		{
			return bAny ? Definition.AnyTransitions : Definition.Transitions;
		}
		return bAny ? Definition.States[Machine].AnyTransitions : Definition.States[Machine].Transitions;
	}

	// 定義の遷移を、グラフの配置の入力（FStateTransitionInfo）にする。Refs[Id] が、その遷移の場所。先が見つからない遷移は含めない
	struct FTransitionList
	{
		TArray<FStateTransitionInfo> Infos;
		TArray<FAIEditorTransitionRef> Refs;
	};

	FTransitionList BuildTransitionList( const FStateMachineDefinition& Definition )
	{
		FTransitionList Result;
		TArray<int32> Machines;
		Machines.Add( -1 );
		for ( int32 Index = 0; Index < Definition.States.Num(); ++Index )
		{
			if ( Definition.States[Index].bMachine )
			{
				Machines.Add( Index );
			}
		}
		for ( const int32 Machine : Machines )
		{
			for ( int32 Pass = 0; Pass < 2; ++Pass )
			{
				const TArray<FStateTransitionDef>& List = TransitionList( Definition, Machine, Pass == 1 );
				for ( int32 Index = 0; Index < List.Num(); ++Index )
				{
					const FStateTransitionDef& Transition = List[Index];
					const int32 Target = Definition.FindByPath( Machine, Transition.To );
					const bool bAny = Pass == 1 || Transition.From.IsEmpty();
					const int32 From = bAny ? -1 : Definition.FindChild( Machine, Transition.From );
					if ( Target < 0 || ( !bAny && From < 0 ) )
					{
						continue;
					}
					FStateTransitionInfo Info;
					Info.Id = Result.Infos.Num();
					Info.Machine = Machine;
					Info.FromChild = From;
					Info.Target = Target;
					Info.Priority = Transition.Priority;
					Info.bAnyTransition = bAny;
					Info.Description = DescribeTransition( Transition );
					Result.Infos.Add( Info );
					FAIEditorTransitionRef Ref;
					Ref.Machine = Machine;
					Ref.bAny = Pass == 1;
					Ref.Index = Index;
					Result.Refs.Add( Ref );
				}
			}
		}
		return Result;
	}

	bool SameRef( const FAIEditorTransitionRef& A, const FAIEditorTransitionRef& B )
	{
		return A.Machine == B.Machine && A.bAny == B.bAny && A.Index == B.Index;
	}

	// ステート State の、親のマシンの番号（最上位は -1）の中で、兄弟のステートの番号
	TArray<int32> Siblings( const FStateMachineDefinition& Definition, int32 State )
	{
		return Definition.GetChildren( Definition.States[State].Parent );
	}

	void SelectTransition( FState& S, int32 Machine, bool bAny, int32 Index )
	{
		S.SelectedState = INDEX_NONE;
		S.SelectedTransition.Machine = Machine;
		S.SelectedTransition.bAny = bAny;
		S.SelectedTransition.Index = Index;
	}

	void AddTransitionTo( FState& S, int32 State, int32 Target )
	{
		const FStateMachineDefinition& Definition = S.StateMachine.Get();
		const int32 Machine = Definition.States[State].Parent;
		const int32 Index = NStateMachineEdit::AddTransition( S.StateMachine, Machine, false, Definition.States[State].Name, Definition.States[Target].Name );
		if ( Index != INDEX_NONE )
		{
			SelectTransition( S, Machine, false, Index );
		}
	}

	void AddAnyTransitionTo( FState& S, int32 State )
	{
		const FStateMachineDefinition& Definition = S.StateMachine.Get();
		const int32 Machine = Definition.States[State].Parent;
		const int32 Index = NStateMachineEdit::AddTransition( S.StateMachine, Machine, true, FString(), Definition.States[State].Name );
		if ( Index != INDEX_NONE )
		{
			SelectTransition( S, Machine, true, Index );
		}
	}

	void DeleteState( FState& S, int32 State )
	{
		if ( IsValidState( S.StateMachine.Get(), State ) )
		{
			NStateMachineEdit::RemoveState( S.StateMachine, State );
			S.SelectedState = INDEX_NONE;
			S.SelectedTransition.Clear();
		}
	}

	void DeleteTransition( FState& S )
	{
		if ( S.SelectedTransition.IsValid() )
		{
			NStateMachineEdit::RemoveTransition( S.StateMachine, S.SelectedTransition.Machine, S.SelectedTransition.bAny, S.SelectedTransition.Index );
		}
		S.SelectedTransition.Clear();
	}

	//=========================================================================
	// グラフ
	//=========================================================================
	void DrawGraph( FState& S )
	{
		const FStateMachineDefinition& Definition = S.StateMachine.Get();
		if ( Definition.States.IsEmpty() )
		{
			ImGui::TextDisabled( "The state machine has no states." );
			if ( ImGui::Button( "Add a state" ) )
			{
				S.SelectedState = NStateMachineEdit::AddState( S.StateMachine, -1, "Idle" );
			}
			return;
		}

		const FTransitionList Transitions = BuildTransitionList( Definition );
		const FStateMachineGraphLayout Layout = NAIGraph::BuildStateMachineLayout( Definition, Transitions.Infos );
		FNodeGraphCanvas Canvas;
		if ( !Canvas.Begin( "##smcanvas", S.StateMachineView, ImVec2( 0.0f, 0.0f ), Layout.Content ) )
		{
			return;
		}
		const ImGuiIO& IO = ImGui::GetIO();
		int32 HoveredState = INDEX_NONE;
		int32 HoveredRoute = INDEX_NONE;

		// 入れ子のマシンの枠（親が先）
		for ( int32 Index = 0; Index < Definition.States.Num(); ++Index )
		{
			const FStateDef& Def = Definition.States[Index];
			if ( !Def.bMachine )
			{
				continue;
			}
			const bool bSelected = Index == S.SelectedState;
			if ( Canvas.DrawBox( Layout.Pos[Index], Layout.Size[Index], ColorContainerFill, bSelected ? ColorAccent : ColorBorder, bSelected ? 2.5f : 1.0f, 8.0f ) )
			{
				HoveredState = Index;
			}
			Canvas.DrawText( Add( Layout.Pos[Index], ImVec2( 10.0f, 5.0f ) ), bSelected ? ColorAccent : ColorDimText, *Def.Name, 14.0f );
			if ( !Def.Entry.IsEmpty() )
			{
				const FString EntryText = FString::Printf( "entry %s", *Def.Entry );
				Canvas.DrawText( Add( Layout.Pos[Index], ImVec2( Layout.Size[Index].x - FNodeGraphCanvas::MeasureText( *EntryText, 11.0f ) - 10.0f, 7.0f ) ), ColorDimText, *EntryText, 11.0f );
			}
		}

		// 矢印とラベル（選んでいる遷移は、強調する）
		for ( const FStateGraphRoute& Route : Layout.Routes )
		{
			const bool bSelected = SameRef( Transitions.Refs[Route.Id], S.SelectedTransition );
			ImU32 Color = Route.bAny ? ColorAnyArrow : ColorArrow;
			float Thickness = 1.8f;
			if ( bSelected )
			{
				Color = ColorAccent;
				Thickness = 3.2f;
			}
			const bool bNear = Canvas.DrawElbowArrow( Route.Points.GetData(), Route.Points.Num(), Color, Thickness );
			if ( bNear && HoveredRoute == INDEX_NONE )
			{
				HoveredRoute = Route.Id;
			}
			const FString& Description = Transitions.Infos[Route.Id].Description;
			const int32 WhenAt = Description.Find( " when " );
			FString Label = WhenAt != INDEX_NONE ? Description.Mid( WhenAt + 6 ) : FString();
			if ( Label.Len() > 26 )
			{
				Label = Label.Left( 25 ) + "~";
			}
			// 条件の説明は、選んでいる矢印と、マウスが乗っている矢印だけに出す（全部出すと、重なって読めない）
			if ( !Label.IsEmpty() && ( bSelected || bNear ) )
			{
				Canvas.DrawText( ImVec2( Route.Label.x - FNodeGraphCanvas::MeasureText( *Label, 11.0f ) * 0.5f, Route.Label.y - 18.0f ), bSelected ? ColorAccent : ColorDimText, *Label, 11.0f );
			}
		}
		for ( int32 Parent = -1; Parent < Definition.States.Num(); ++Parent )
		{
			const FStateGraphMachine& MachineLayout = Layout.Machines[Parent + 1];
			if ( MachineLayout.EntryChild >= 0 )
			{
				const ImVec2 End( Layout.Pos[MachineLayout.EntryChild].x, Layout.Pos[MachineLayout.EntryChild].y + Layout.Size[MachineLayout.EntryChild].y * 0.5f );
				const ImVec2 EntryRoute[2] = { ImVec2( End.x - 32.0f, End.y ), End };
				Canvas.DrawDot( EntryRoute[0], ColorDimText );
				Canvas.DrawElbowArrow( EntryRoute, 2, ColorDimText, 1.6f );
				Canvas.DrawText( ImVec2( EntryRoute[0].x - 4.0f, End.y - 17.0f ), ColorDimText, "start", 10.0f );
			}
		}
		for ( const FStateGraphRelay& Relay : Layout.Relays )
		{
			Canvas.DrawRelay( Relay.Pos, ColorArrow );
		}

		// ステート（葉）と、ANY の箱
		for ( int32 Index = 0; Index < Definition.States.Num(); ++Index )
		{
			const FStateDef& Def = Definition.States[Index];
			if ( Def.bMachine )
			{
				continue;
			}
			const bool bSelected = Index == S.SelectedState;
			const ImVec2 Min = Layout.Pos[Index];
			if ( Canvas.DrawBox( Min, Layout.Size[Index], ColorNodeFill, bSelected ? ColorAccent : ColorBorder, bSelected ? 3.0f : 1.5f, 7.0f ) )
			{
				HoveredState = Index;
			}
			Canvas.DrawText( Add( Min, ImVec2( 10.0f, 7.0f ) ), bSelected ? ColorAccent : ColorText, *Def.Name, 14.0f, Min, Layout.Size[Index] );
			if ( !Def.Clip.IsEmpty() )
			{
				Canvas.DrawText( Add( Min, ImVec2( 10.0f, 27.0f ) ), ColorDimText, *FString::Printf( "clip %s", *Def.Clip ), 11.0f, Min, Layout.Size[Index] );
			}
		}
		for ( int32 Parent = -1; Parent < Definition.States.Num(); ++Parent )
		{
			const FStateGraphMachine& MachineLayout = Layout.Machines[Parent + 1];
			if ( MachineLayout.bHasAny )
			{
				Canvas.DrawBox( MachineLayout.AnyPos, ImVec2( NAIGraph::AnyWidth, NAIGraph::AnyHeight ), ColorAnyFill, ColorAnyArrow, 1.5f, 7.0f );
				Canvas.DrawText( Add( MachineLayout.AnyPos, ImVec2( 12.0f, 9.0f ) ), ColorOrange, "ANY", 14.0f );
			}
		}

		// 入力: クリックで選択、ドラッグでステートを動かす、右クリックでメニュー
		int32 MenuKind = 0;
		if ( Canvas.IsHovered() && ImGui::IsMouseClicked( ImGuiMouseButton_Left ) )
		{
			if ( HoveredState != INDEX_NONE )
			{
				S.SelectedState = HoveredState;
				S.SelectedTransition.Clear();
				S.DragState = HoveredState;
				S.DragStartWorld = Canvas.ToWorld( IO.MousePos );
				const int32 Parent = Definition.States[HoveredState].Parent;
				S.DragStartRelative = ImVec2( Layout.Pos[HoveredState].x - Layout.Machines[Parent + 1].Origin.x, Layout.Pos[HoveredState].y - Layout.Machines[Parent + 1].Origin.y );
			}
			else if ( HoveredRoute != INDEX_NONE )
			{
				const FAIEditorTransitionRef& Ref = Transitions.Refs[HoveredRoute];
				SelectTransition( S, Ref.Machine, Ref.bAny, Ref.Index );
			}
			else
			{
				S.SelectedState = INDEX_NONE;
				S.SelectedTransition.Clear();
			}
		}
		if ( Canvas.IsHovered() && ImGui::IsMouseClicked( ImGuiMouseButton_Right ) )
		{
			S.MenuWorld = Canvas.ToWorld( IO.MousePos );
			if ( HoveredState != INDEX_NONE )
			{
				S.SelectedState = HoveredState;
				S.SelectedTransition.Clear();
				MenuKind = 1;
			}
			else if ( HoveredRoute != INDEX_NONE )
			{
				const FAIEditorTransitionRef& Ref = Transitions.Refs[HoveredRoute];
				SelectTransition( S, Ref.Machine, Ref.bAny, Ref.Index );
				MenuKind = 2;
			}
			else
			{
				MenuKind = 3;
			}
		}
		const bool bDragging = S.DragState != INDEX_NONE && ImGui::IsMouseDown( ImGuiMouseButton_Left );
		if ( S.DragState != INDEX_NONE )
		{
			if ( bDragging && IsValidState( Definition, S.DragState ) )
			{
				const ImVec2 World = Canvas.ToWorld( IO.MousePos );
				const float Dx = World.x - S.DragStartWorld.x;
				const float Dy = World.y - S.DragStartWorld.y;
				if ( Dx * Dx + Dy * Dy > 4.0f )
				{
					NStateMachineEdit::SetStatePosition( S.StateMachine, S.DragState, NMath::Max( S.DragStartRelative.x + Dx, 0.0f ), NMath::Max( S.DragStartRelative.y + Dy, 0.0f ), "statedrag" );
				}
			}
			else
			{
				S.DragState = INDEX_NONE;
				S.StateMachine.EndEditSession();
			}
		}
		if ( ImGui::IsWindowFocused( ImGuiFocusedFlags_RootAndChildWindows ) && !IO.WantTextInput && ImGui::IsKeyPressed( ImGuiKey_Delete ) )
		{
			if ( S.SelectedTransition.IsValid() )
			{
				DeleteTransition( S );
			}
			else if ( IsValidState( Definition, S.SelectedState ) )
			{
				DeleteState( S, S.SelectedState );
			}
		}

		S.StateMachineView.bPanBlocked = HoveredState != INDEX_NONE || HoveredRoute != INDEX_NONE || S.DragState != INDEX_NONE;
		Canvas.End();

		if ( MenuKind == 1 )
		{
			ImGui::OpenPopup( "##smstatemenu" );
		}
		else if ( MenuKind == 2 )
		{
			ImGui::OpenPopup( "##smtransitionmenu" );
		}
		else if ( MenuKind == 3 )
		{
			ImGui::OpenPopup( "##smemptymenu" );
		}
		if ( ImGui::BeginPopup( "##smstatemenu" ) )
		{
			const int32 State = S.SelectedState;
			if ( IsValidState( S.StateMachine.Get(), State ) )
			{
				if ( ImGui::MenuItem( "Add child state" ) )
				{
					S.SelectedState = NStateMachineEdit::AddState( S.StateMachine, State, "State" );
				}
				if ( ImGui::BeginMenu( "Add transition to" ) )
				{
					for ( const int32 Sibling : Siblings( S.StateMachine.Get(), State ) )
					{
						if ( ImGui::MenuItem( UI_TEXT( S.StateMachine.Get().States[Sibling].Name ) ) )
						{
							AddTransitionTo( S, State, Sibling );
						}
					}
					ImGui::EndMenu();
				}
				if ( ImGui::MenuItem( "Add Any State transition to this" ) )
				{
					AddAnyTransitionTo( S, State );
				}
				if ( ImGui::MenuItem( "Set as entry" ) )
				{
					NStateMachineEdit::SetEntry( S.StateMachine, S.StateMachine.Get().States[State].Parent, S.StateMachine.Get().States[State].Name );
				}
				ImGui::Separator();
				if ( ImGui::MenuItem( "Delete state", "Del" ) )
				{
					DeleteState( S, State );
				}
			}
			ImGui::EndPopup();
		}
		if ( ImGui::BeginPopup( "##smtransitionmenu" ) )
		{
			if ( ImGui::MenuItem( "Delete transition", "Del" ) )
			{
				DeleteTransition( S );
			}
			ImGui::EndPopup();
		}
		if ( ImGui::BeginPopup( "##smemptymenu" ) )
		{
			if ( ImGui::MenuItem( "Add state here" ) )
			{
				const int32 Added = NStateMachineEdit::AddState( S.StateMachine, -1, "State" );
				if ( Added != INDEX_NONE )
				{
					NStateMachineEdit::SetStatePosition( S.StateMachine, Added, NMath::Max( S.MenuWorld.x, 0.0f ), NMath::Max( S.MenuWorld.y, 0.0f ) );
					S.SelectedState = Added;
				}
			}
			ImGui::EndPopup();
		}
	}

	//=========================================================================
	// Inspector
	//=========================================================================
	const TArray<FAICall>& ActionsOf( const FStateDef& State, EStateActionList List )
	{
		switch ( List )
		{
		case EStateActionList::OnEnter:
			return State.OnEnter;
		case EStateActionList::OnTick:
			return State.OnTick;
		default:
			return State.OnExit;
		}
	}

	void DrawActionList( FState& S, int32 StateIndex, EStateActionList List, const char* Title )
	{
		const FStateMachineDefinition& Definition = S.StateMachine.Get();
		ImGui::SeparatorText( Title );
		int32 RemoveIndex = INDEX_NONE;
		int32 MoveFrom = INDEX_NONE;
		int32 MoveTo = INDEX_NONE;
		const TArray<FAICall>& Actions = ActionsOf( Definition.States[StateIndex], List );
		for ( int32 Index = 0; Index < Actions.Num(); ++Index )
		{
			ImGui::PushID( Index + static_cast<int32>( List ) * 100 );
			ImGui::TextColored( InspectorAccent, "%s %d", Title, Index + 1 );
			ImGui::SameLine();
			if ( ImGui::SmallButton( "^" ) && Index > 0 )
			{
				MoveFrom = Index;
				MoveTo = Index - 1;
			}
			ImGui::SameLine();
			if ( ImGui::SmallButton( "v" ) && Index + 1 < Actions.Num() )
			{
				MoveFrom = Index;
				MoveTo = Index + 1;
			}
			ImGui::SameLine();
			if ( ImGui::SmallButton( "x" ) )
			{
				RemoveIndex = Index;
			}
			FAICall Edited = Actions[Index];
			if ( NAIEditor::DrawCallEditor( S, "Action", Edited, NAIEditor::EPartKind::StateAction, Definition.Blackboard ) )
			{
				char CoalesceKey[48];
				NCString::Format( CoalesceKey, sizeof( CoalesceKey ), "action%d_%d_%d", StateIndex, static_cast<int32>( List ), Index );
				NStateMachineEdit::SetAction( S.StateMachine, StateIndex, List, Index, Edited, CoalesceKey );
			}
			ImGui::PopID();
		}
		if ( RemoveIndex != INDEX_NONE )
		{
			NStateMachineEdit::RemoveAction( S.StateMachine, StateIndex, List, RemoveIndex );
		}
		if ( MoveFrom != INDEX_NONE )
		{
			NStateMachineEdit::MoveAction( S.StateMachine, StateIndex, List, MoveFrom, MoveTo );
		}
		const FString Popup = FString::Printf( "##addaction%d", static_cast<int32>( List ) );
		if ( ImGui::Button( UI_TEXT( FString::Printf( "+ %s", Title ) ) ) )
		{
			ImGui::OpenPopup( *Popup );
		}
		if ( ImGui::BeginPopup( *Popup ) )
		{
			const FString Name = NAIEditor::DrawPartMenuItems( NAIEditor::EPartKind::StateAction );
			if ( !Name.IsEmpty() )
			{
				FAICall Call;
				Call.Do = Name;
				NStateMachineEdit::AddAction( S.StateMachine, StateIndex, List, Call );
				ImGui::CloseCurrentPopup();
			}
			ImGui::EndPopup();
		}
	}

	void DrawStateInspector( FState& S )
	{
		const FStateMachineDefinition& Definition = S.StateMachine.Get();
		const int32 State = S.SelectedState;
		const FStateDef& Def = Definition.States[State];
		ImGui::TextColored( InspectorAccent, "State: %s", UI_TEXT( Definition.GetPath( State ) ) );
		ImGui::Separator();

		// 名前（入力が終わったときに改名する）
		Field( "Name" );
		const int32 BufferKey = 2000000 + State;
		FString Text = S.NameBufferKey == BufferKey ? S.NameBuffer : Def.Name;
		if ( NDebugUI::InputString( "##statename", Text ) )
		{
			S.NameBuffer = Text;
			S.NameBufferKey = BufferKey;
		}
		if ( ImGui::IsItemDeactivatedAfterEdit() && S.NameBufferKey == BufferKey )
		{
			NStateMachineEdit::RenameState( S.StateMachine, State, S.NameBuffer );
			S.NameBufferKey = INDEX_NONE;
		}

		Field( "Machine" );
		bool bMachine = Def.bMachine;
		if ( ImGui::Checkbox( "##machine", &bMachine ) )
		{
			NStateMachineEdit::SetStateIsMachine( S.StateMachine, State, bMachine );
		}
		if ( ImGui::IsItemHovered() )
		{
			ImGui::SetTooltip( "A machine has child states (and transitions between them). A machine with children cannot become a leaf." );
		}

		Field( "Clip" );
		FString Clip = Def.Clip;
		if ( NDebugUI::DrawNameCombo( "##clip", Clip, S.ClipNames, "(none)" ) )
		{
			NStateMachineEdit::SetStateClip( S.StateMachine, State, Clip );
		}

		if ( Def.bMachine )
		{
			Field( "Entry" );
			TArray<FString> ChildNames;
			for ( const int32 Child : Definition.GetChildren( State ) )
			{
				ChildNames.Add( Definition.States[Child].Name );
			}
			FString Entry = Def.Entry;
			if ( NDebugUI::DrawNameCombo( "##entry", Entry, ChildNames, "(first child)" ) )
			{
				NStateMachineEdit::SetEntry( S.StateMachine, State, Entry );
			}
			if ( ImGui::Button( "+ Child state" ) )
			{
				S.SelectedState = NStateMachineEdit::AddState( S.StateMachine, State, "State" );
				return;
			}
		}

		DrawActionList( S, State, EStateActionList::OnEnter, "OnEnter" );
		DrawActionList( S, State, EStateActionList::OnTick, "OnTick" );
		DrawActionList( S, State, EStateActionList::OnExit, "OnExit" );

		// アニメーションの出来事
		ImGui::SeparatorText( "OnClipEvent" );
		int32 RemoveEvent = INDEX_NONE;
		for ( int32 Index = 0; Index < Def.OnClipEvent.Num(); ++Index )
		{
			ImGui::PushID( 5000 + Index );
			FStateClipEventDef Edited = Def.OnClipEvent[Index];
			bool bChanged = false;
			ImGui::TextColored( InspectorAccent, "Clip event %d", Index + 1 );
			ImGui::SameLine();
			if ( ImGui::SmallButton( "x" ) )
			{
				RemoveEvent = Index;
			}
			Field( "Event" );
			bChanged |= NDebugUI::InputString( "##event", Edited.Event );
			bChanged |= NAIEditor::DrawCallEditor( S, "Action", Edited.Call, NAIEditor::EPartKind::StateAction, Definition.Blackboard );
			if ( bChanged )
			{
				char CoalesceKey[40];
				NCString::Format( CoalesceKey, sizeof( CoalesceKey ), "clipevent%d_%d", State, Index );
				NStateMachineEdit::SetClipEvent( S.StateMachine, State, Index, Edited, CoalesceKey );
			}
			ImGui::PopID();
		}
		if ( RemoveEvent != INDEX_NONE )
		{
			NStateMachineEdit::RemoveClipEvent( S.StateMachine, State, RemoveEvent );
		}
		if ( ImGui::Button( "+ Clip event" ) )
		{
			FStateClipEventDef Event;
			Event.Event = "Event";
			NStateMachineEdit::AddClipEvent( S.StateMachine, State, Event );
		}

		// このステートから出る遷移
		ImGui::SeparatorText( "Transitions" );
		const int32 Machine = Def.Parent;
		const TArray<FStateTransitionDef>& List = TransitionList( Definition, Machine, false );
		bool bAny = false;
		for ( int32 Index = 0; Index < List.Num(); ++Index )
		{
			if ( List[Index].From == Def.Name )
			{
				bAny = true;
				if ( ImGui::Selectable( UI_TEXT( DescribeTransition( List[Index] ) ), S.SelectedTransition.Machine == Machine && !S.SelectedTransition.bAny && S.SelectedTransition.Index == Index ) )
				{
					SelectTransition( S, Machine, false, Index );
					return;
				}
			}
		}
		if ( !bAny )
		{
			ImGui::TextDisabled( "(none)" );
		}
		ImGui::SetNextItemWidth( 180.0f );
		if ( ImGui::BeginCombo( "##addtransition", "+ Transition to..." ) )
		{
			for ( const int32 Sibling : Siblings( Definition, State ) )
			{
				if ( ImGui::Selectable( UI_TEXT( Definition.States[Sibling].Name ) ) )
				{
					AddTransitionTo( S, State, Sibling );
					ImGui::EndCombo();
					return;
				}
			}
			ImGui::EndCombo();
		}
		if ( ImGui::Button( "+ Any State transition to this" ) )
		{
			AddAnyTransitionTo( S, State );
			return;
		}

		ImGui::Separator();
		if ( ImGui::Button( "Delete state" ) )
		{
			DeleteState( S, State );
		}
	}

	void DrawTransitionInspector( FState& S )
	{
		const FStateMachineDefinition& Definition = S.StateMachine.Get();
		const FAIEditorTransitionRef Ref = S.SelectedTransition;
		if ( !( Ref.Machine == -1 || IsValidState( Definition, Ref.Machine ) ) || Ref.Index >= TransitionList( Definition, Ref.Machine, Ref.bAny ).Num() )
		{
			S.SelectedTransition.Clear();
			return;
		}
		FStateTransitionDef Edited = TransitionList( Definition, Ref.Machine, Ref.bAny )[Ref.Index];
		bool bChanged = false;
		ImGui::TextColored( InspectorAccent, "%s transition", Ref.bAny ? "Any State" : "Transition" );
		ImGui::TextDisabled( "in %s", Ref.Machine < 0 ? "the top level" : UI_TEXT( Definition.GetPath( Ref.Machine ) ) );
		ImGui::Separator();

		// 元と先（同じマシンの子）
		TArray<FString> ChildNames;
		for ( const int32 Child : Definition.GetChildren( Ref.Machine ) )
		{
			ChildNames.Add( Definition.States[Child].Name );
		}
		if ( !Ref.bAny )
		{
			Field( "From" );
			bChanged |= NDebugUI::DrawNameCombo( "##from", Edited.From, ChildNames, "(any child)" );
		}
		Field( "To" );
		bChanged |= NDebugUI::DrawNameCombo( "##to", Edited.To, ChildNames, "(choose)" );
		Field( "Priority" );
		bChanged |= ImGui::InputInt( "##priority", &Edited.Priority );
		if ( ImGui::IsItemHovered() )
		{
			ImGui::SetTooltip( "Higher first. The same priority is evaluated in the written order." );
		}
		Field( "Min time (s)" );
		bChanged |= ImGui::DragFloat( "##mintime", &Edited.MinTime, 0.01f, 0.0f, 600.0f );
		Field( "Any of (OR)" );
		bChanged |= ImGui::Checkbox( "##anyof", &Edited.bAnyOf );
		if ( ImGui::IsItemHovered() )
		{
			ImGui::SetTooltip( "Off: all conditions must be true (AND). On: any one is enough (OR)." );
		}

		ImGui::SeparatorText( "Conditions (When)" );
		int32 RemoveCondition = INDEX_NONE;
		const FAIInspectContext Context = NAIEditor::MakeInspectContext( S, Definition.Blackboard );
		for ( int32 Index = 0; Index < Edited.When.Num(); ++Index )
		{
			FStateConditionDef& Condition = Edited.When[Index];
			ImGui::PushID( 7000 + Index );
			ImGui::TextColored( InspectorAccent, "[%d]", Index + 1 );
			ImGui::SameLine();
			ImGui::SetNextItemWidth( 170.0f );
			if ( ImGui::BeginCombo( "##type", ConditionTypeName( Condition.Type ) ) )
			{
				for ( uint8 Type = 0; Type <= static_cast<uint8>( EStateConditionType::Custom ); ++Type )
				{
					const EStateConditionType Candidate = static_cast<EStateConditionType>( Type );
					if ( ImGui::Selectable( ConditionTypeName( Candidate ), Candidate == Condition.Type ) )
					{
						Condition = FStateConditionDef();
						Condition.Type = Candidate;
						bChanged = true;
					}
				}
				ImGui::EndCombo();
			}
			ImGui::SameLine();
			if ( ImGui::SmallButton( "x" ) )
			{
				RemoveCondition = Index;
			}
			switch ( Condition.Type )
			{
			case EStateConditionType::BlackboardCompare:
			{
				Field( "Key" );
				bChanged |= NDebugUI::DrawBlackboardKeyCombo( "##key", Condition.Key, Context );
				const int32 KeyIndex = Context.Blackboard != nullptr ? Context.Blackboard->FindKeyIndex( Condition.Key ) : -1;
				const FBlackboardKeyDefinition* KeyDefinition = KeyIndex >= 0 ? &Context.Blackboard->Keys[KeyIndex] : nullptr;
				Field( "Operator" );
				bChanged |= NDebugUI::DrawCompareOpCombo( "##op", Condition.Op, KeyDefinition );
				Field( "Value" );
				bChanged |= NDebugUI::DrawBlackboardValueInput( "##value", KeyDefinition, Condition.Value );
				break;
			}
			case EStateConditionType::Event:
				Field( "Event" );
				bChanged |= NDebugUI::InputString( "##event", Condition.Event );
				break;
			case EStateConditionType::ElapsedTime:
				Field( "Seconds" );
				bChanged |= ImGui::DragFloat( "##seconds", &Condition.Seconds, 0.01f, 0.0f, 600.0f );
				break;
			case EStateConditionType::Custom:
				bChanged |= NAIEditor::DrawCallEditor( S, "Condition", Condition.Call, NAIEditor::EPartKind::Condition, Definition.Blackboard );
				break;
			default:
				break;
			}
			Field( "Not" );
			bChanged |= ImGui::Checkbox( "##not", &Condition.bNot );
			ImGui::Separator();
			ImGui::PopID();
		}
		if ( RemoveCondition != INDEX_NONE )
		{
			Edited.When.RemoveAt( RemoveCondition );
			bChanged = true;
		}
		if ( ImGui::Button( "+ Condition" ) )
		{
			FStateConditionDef Condition;
			Condition.Value = true;
			Edited.When.Add( Condition );
			bChanged = true;
		}
		if ( bChanged )
		{
			char CoalesceKey[48];
			NCString::Format( CoalesceKey, sizeof( CoalesceKey ), "transition%d_%d_%d", Ref.Machine, Ref.bAny ? 1 : 0, Ref.Index );
			NStateMachineEdit::SetTransition( S.StateMachine, Ref.Machine, Ref.bAny, Ref.Index, Edited, CoalesceKey );
		}
		ImGui::Separator();
		if ( ImGui::Button( "Delete transition" ) )
		{
			DeleteTransition( S );
		}
	}

	void DrawInspector( FState& S )
	{
		const FStateMachineDefinition& Definition = S.StateMachine.Get();
		if ( IsValidState( Definition, S.SelectedState ) )
		{
			DrawStateInspector( S );
			return;
		}
		if ( S.SelectedTransition.IsValid() )
		{
			DrawTransitionInspector( S );
			return;
		}
		ImGui::TextWrapped( "Click a state or an arrow to edit it. Drag a state to move it (its position is saved in the file). Right-click for the menu." );
		ImGui::Separator();
		Field( "Entry" );
		TArray<FString> TopNames;
		for ( const int32 Child : Definition.GetChildren( -1 ) )
		{
			TopNames.Add( Definition.States[Child].Name );
		}
		FString Entry = Definition.Entry;
		if ( NDebugUI::DrawNameCombo( "##topentry", Entry, TopNames, "(first state)" ) )
		{
			NStateMachineEdit::SetEntry( S.StateMachine, -1, Entry );
		}
		if ( ImGui::Button( "+ State" ) )
		{
			S.SelectedState = NStateMachineEdit::AddState( S.StateMachine, -1, "State" );
		}
	}
}

void NAIEditor::DrawStateMachineTab( FAIEditorState& S )
{
	if ( !IsValidState( S.StateMachine.Get(), S.SelectedState ) )
	{
		S.SelectedState = INDEX_NONE;
	}

	// ツールバー
	if ( ImGui::Button( "Fit" ) )
	{
		S.StateMachineView.bFitted = false;
	}
	ImGui::SameLine();
	if ( ImGui::Button( "Reset layout" ) )
	{
		// 動かした座標を捨てて、自動の配置に戻す（元に戻せる）
		FStateMachineDefinition Cleared = S.StateMachine.Get();
		bool bAny = false;
		for ( const FStateDef& State : Cleared.States )
		{
			bAny = bAny || ( State.Editor.is_object() && ( State.Editor.contains( "X" ) || State.Editor.contains( "Y" ) ) );
		}
		if ( bAny )
		{
			FStateMachineDefinition& Definition = S.StateMachine.BeginEdit();
			for ( FStateDef& State : Definition.States )
			{
				if ( State.Editor.is_object() )
				{
					State.Editor.erase( "X" );
					State.Editor.erase( "Y" );
					if ( State.Editor.empty() )
					{
						State.Editor = NlohmannJson();
					}
				}
			}
		}
		S.StateMachineView.bFitted = false;
	}
	ImGui::SameLine();
	ImGui::TextUnformatted( "Blackboard:" );
	ImGui::SameLine();
	ImGui::SetNextItemWidth( 260.0f );
	FString Blackboard = S.StateMachine.Get().Blackboard;
	if ( NDebugUI::DrawNameCombo( "##smblackboard", Blackboard, S.BlackboardFiles, "(none)" ) )
	{
		NStateMachineEdit::SetBlackboard( S.StateMachine, Blackboard );
	}
	ImGui::SameLine();
	ImGui::BeginDisabled( S.StateMachine.Get().Blackboard.IsEmpty() );
	if ( ImGui::SmallButton( "Clear##smbb" ) )
	{
		NStateMachineEdit::SetBlackboard( S.StateMachine, FString() );
	}
	ImGui::EndDisabled();

	// グラフと Inspector
	constexpr float InspectorWidth = 400.0f;
	const float Height = NMath::Max( ImGui::GetContentRegionAvail().y - 170.0f, 220.0f );
	const float GraphWidth = NMath::Max( ImGui::GetContentRegionAvail().x - InspectorWidth - ImGui::GetStyle().ItemSpacing.x, 200.0f );
	if ( ImGui::BeginChild( "##smgraph", ImVec2( GraphWidth, Height ), ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse ) )
	{
		DrawGraph( S );
	}
	ImGui::EndChild();
	ImGui::SameLine();
	if ( ImGui::BeginChild( "##sminspector", ImVec2( InspectorWidth, Height ), ImGuiChildFlags_Borders ) )
	{
		DrawInspector( S );
	}
	ImGui::EndChild();

	if ( !ImGui::IsAnyItemActive() )
	{
		S.StateMachine.EndEditSession();
	}
	NAIEditor::DrawIssues( S );
}
