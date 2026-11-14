// SPDX-License-Identifier: MIT
#include "AIEditorStateMachineInternal.h"

namespace NAIEditorStateMachineUI
{
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
		// Issues からのジャンプ: 選んだステート・遷移を真ん中へ
		if ( S.bFocusRequested )
		{
			if ( IsValidState( Definition, S.SelectedState ) )
			{
				S.StateMachineView.bCenterRequested = true;
				S.StateMachineView.CenterWorld = Add( Layout.Pos[S.SelectedState], ImVec2( Layout.Size[S.SelectedState].x * 0.5f, Layout.Size[S.SelectedState].y * 0.5f ) );
			}
			else if ( S.SelectedTransition.IsValid() )
			{
				for ( const FStateGraphRoute& Route : Layout.Routes )
				{
					if ( SameRef( Transitions.Refs[Route.Id], S.SelectedTransition ) )
					{
						S.StateMachineView.bCenterRequested = true;
						S.StateMachineView.CenterWorld = Route.Label;
					}
				}
			}
		}
		S.bFocusRequested = false;
		FNodeGraphCanvas Canvas;
		if ( !Canvas.Begin( "##smcanvas", S.StateMachineView, ImVec2( 0.0f, 0.0f ), Layout.Content ) )
		{
			return;
		}
		const ImGuiIO& IO = ImGui::GetIO();
		int32 HoveredState = INDEX_NONE;
		int32 HoveredRoute = INDEX_NONE;
		// 矢印のラベルは、箱より手前に描くため、あとでまとめて描く
		struct FPendingLabel
		{
			ImVec2 Pos;
			ImU32 Color;
			FString Text;
		};
		TArray<FPendingLabel> PendingLabels;

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
		for ( int32 RouteIndex = 0; RouteIndex < Layout.Routes.Num(); ++RouteIndex )
		{
			const FStateGraphRoute& Route = Layout.Routes[RouteIndex];
			// まとめた矢印は、組の中に、選んでいる遷移があれば、それを代表にする
			int32 Shown = Route.Id;
			bool bSelected = false;
			for ( const int32 Id : Route.Ids )
			{
				if ( SameRef( Transitions.Refs[Id], S.SelectedTransition ) )
				{
					Shown = Id;
					bSelected = true;
				}
			}
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
				HoveredRoute = RouteIndex;
			}
			const FString& Description = Transitions.Infos[Shown].Description;
			const int32 WhenAt = Description.Find( " when " );
			FString Label = WhenAt != INDEX_NONE ? Description.Mid( WhenAt + 6 ) : FString();
			if ( Label.Len() > 26 )
			{
				Label = Label.Left( 25 ) + "~";
			}
			const bool bGroup = Route.Ids.Num() > 1;
			if ( bGroup )
			{
				Label += FString::Printf( "%sx%d", Label.IsEmpty() ? "" : "  ", Route.Ids.Num() );
			}
			// 条件の説明は、選んでいる矢印と、マウスが乗っている矢印だけに出す（全部出すと、重なって読めない）。まとめた矢印の本数は、いつも出す
			if ( !Label.IsEmpty() && ( bSelected || bNear || bGroup ) )
			{
				FPendingLabel Pending;
				Pending.Pos = ImVec2( Route.Label.x - FNodeGraphCanvas::MeasureText( *Label, 11.0f ) * 0.5f, Route.Label.y - 18.0f );
				Pending.Color = bSelected ? ColorAccent : ColorDimText;
				Pending.Text = Label;
				PendingLabels.Add( Pending );
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

		for ( const FPendingLabel& Label : PendingLabels )
		{
			Canvas.DrawText( Label.Pos, Label.Color, *Label.Text, 11.0f );
		}

		// 遷移を作るためのポート（各ステートの右端と ANY の箱の右端の丸）。ここから別のステートへドラッグすると、遷移ができる
		const auto PortOfState = [&]( int32 State )
		{
			return ImVec2( Layout.Pos[State].x + Layout.Size[State].x, Layout.Pos[State].y + Layout.Size[State].y * 0.5f );
		};
		const auto PortOfAny = [&]( int32 Machine )
		{
			const FStateGraphMachine& MachineLayout = Layout.Machines[Machine + 1];
			return ImVec2( MachineLayout.AnyPos.x + NAIGraph::AnyWidth, MachineLayout.AnyPos.y + NAIGraph::AnyHeight * 0.5f );
		};
		const auto IsOverPort = [&]( const ImVec2& World )
		{
			const ImVec2 Center = Canvas.ToScreen( World );
			const float Dx = IO.MousePos.x - Center.x;
			const float Dy = IO.MousePos.y - Center.y;
			return Canvas.IsHovered() && Dx * Dx + Dy * Dy < 100.0f;
		};
		int32 HoveredPort = INDEX_NONE;
		int32 HoveredAnyPort = -2;
		for ( int32 Index = 0; Index < Definition.States.Num(); ++Index )
		{
			const bool bOver = IsOverPort( PortOfState( Index ) );
			if ( bOver )
			{
				HoveredPort = Index;
			}
			Canvas.DrawDot( PortOfState( Index ), bOver || Index == S.SelectedState ? ColorAccent : ColorBorder, bOver ? 6.0f : 4.5f );
		}
		for ( int32 Parent = -1; Parent < Definition.States.Num(); ++Parent )
		{
			if ( Layout.Machines[Parent + 1].bHasAny )
			{
				const bool bOver = IsOverPort( PortOfAny( Parent ) );
				if ( bOver )
				{
					HoveredAnyPort = Parent;
				}
				Canvas.DrawDot( PortOfAny( Parent ), bOver ? ColorAccent : ColorAnyArrow, bOver ? 6.0f : 4.5f );
			}
		}
		if ( HoveredPort != INDEX_NONE || HoveredAnyPort != -2 )
		{
			ImGui::SetTooltip( "Drag to another state to create a transition." );
		}

		// 入力: クリックで選択、ドラッグでステートを動かす、右クリックでメニュー
		int32 MenuKind = 0;
		if ( Canvas.IsHovered() && ImGui::IsMouseClicked( ImGuiMouseButton_Left ) )
		{
			if ( HoveredPort != INDEX_NONE )
			{
				// ポートから、遷移を作るドラッグを始める
				S.LinkFromState = HoveredPort;
				S.LinkFromAnyMachine = -2;
				S.SelectedState = HoveredPort;
				S.SelectedTransition.Clear();
			}
			else if ( HoveredAnyPort != -2 )
			{
				S.LinkFromAnyMachine = HoveredAnyPort;
				S.LinkFromState = INDEX_NONE;
			}
			else if ( HoveredState != INDEX_NONE )
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
				S.SelectedState = INDEX_NONE;
				SelectRoute( S, Transitions, Layout.Routes[HoveredRoute], true );
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
				S.SelectedState = INDEX_NONE;
				SelectRoute( S, Transitions, Layout.Routes[HoveredRoute], false );
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
		// 遷移を作るドラッグ: 元から、マウスの位置まで線を引く。ステートの上で放すと、そこへの遷移ができる（同じマシンの中だけ）
		const bool bLinking = S.LinkFromState != INDEX_NONE || S.LinkFromAnyMachine != -2;
		if ( bLinking )
		{
			const bool bFromState = S.LinkFromState != INDEX_NONE;
			const int32 LinkMachine = bFromState ? ( IsValidState( Definition, S.LinkFromState ) ? Definition.States[S.LinkFromState].Parent : -2 ) : S.LinkFromAnyMachine;
			const bool bValidTarget = LinkMachine >= -1 && HoveredState != INDEX_NONE && Definition.States[HoveredState].Parent == LinkMachine;
			if ( LinkMachine < -1 )
			{
				S.LinkFromState = INDEX_NONE;
				S.LinkFromAnyMachine = -2;
			}
			else if ( ImGui::IsMouseDown( ImGuiMouseButton_Left ) )
			{
				const ImVec2 From = bFromState ? PortOfState( S.LinkFromState ) : PortOfAny( LinkMachine );
				Canvas.DrawLine( From, Canvas.ToWorld( IO.MousePos ), bValidTarget ? ColorDrop : ColorAccent, 3.0f );
				if ( HoveredState != INDEX_NONE )
				{
					Canvas.DrawOutline( Layout.Pos[HoveredState], Layout.Size[HoveredState], bValidTarget ? ColorDrop : ColorBad, 3.0f, 7.0f );
				}
			}
			else
			{
				if ( bValidTarget )
				{
					if ( bFromState )
					{
						AddTransitionTo( S, S.LinkFromState, HoveredState );
					}
					else
					{
						AddAnyTransitionTo( S, HoveredState );
					}
				}
				else if ( HoveredState != INDEX_NONE )
				{
					NAIEditor::SetStatus( S, "A transition can only connect states of the same machine.", true );
				}
				S.LinkFromState = INDEX_NONE;
				S.LinkFromAnyMachine = -2;
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

		S.StateMachineView.bPanBlocked = HoveredState != INDEX_NONE || HoveredRoute != INDEX_NONE || S.DragState != INDEX_NONE || HoveredPort != INDEX_NONE || HoveredAnyPort != -2 || bLinking;
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
}
