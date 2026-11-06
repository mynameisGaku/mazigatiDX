// SPDX-License-Identifier: MIT
#include "AIGraphViews.h"

#include "Library/Core/Collection/Array.h"
#include "Library/DebugUI/AI/AIGraphLayout.h"
#include "Library/DebugUI/Common/EditorCommon.h"
#include "Library/Framework/AI/BehaviorTreeComponent.h"
#include "Library/Framework/AI/StateMachineComponent.h"
#include "Library/Json/JsonObject.h"
#include "Library/Math/MathUtils.h"

namespace
{
	//=========================================================================
	// 色
	//=========================================================================
	const ImU32 ColorText = IM_COL32( 235, 235, 240, 255 );
	const ImU32 ColorDimText = IM_COL32( 150, 152, 160, 255 );
	const ImU32 ColorGreen = IM_COL32( 110, 255, 140, 255 );
	const ImU32 ColorYellow = IM_COL32( 240, 210, 80, 255 );
	const ImU32 ColorRed = IM_COL32( 255, 120, 110, 255 );
	const ImU32 ColorOrange = IM_COL32( 255, 165, 80, 255 );
	const ImU32 ColorBorder = IM_COL32( 95, 98, 108, 255 );
	const ImU32 ColorNodeFill = IM_COL32( 46, 48, 58, 255 );
	const ImU32 ColorActiveFill = IM_COL32( 38, 82, 52, 255 );
	const ImU32 ColorContainerFill = IM_COL32( 36, 38, 48, 255 );
	const ImU32 ColorArrowIdle = IM_COL32( 92, 94, 104, 255 );
	const ImU32 ColorArrowPossible = IM_COL32( 170, 180, 215, 255 );
	const ImU32 ColorAnyArrowIdle = IM_COL32( 130, 96, 62, 255 );
	const ImU32 ColorAnyArrowPossible = IM_COL32( 215, 160, 95, 255 );

	// 遷移した直後の強調が消えるまでの秒数
	constexpr float FiredFadeSeconds = 2.5f;
	// 結果の強調が消えるまでの秒数（ビヘイビアツリー）
	constexpr float ResultFadeSeconds = 2.0f;

	ImVec2 Add( const ImVec2& A, const ImVec2& B )
	{
		return ImVec2( A.x + B.x, A.y + B.y );
	}

	FString Truncated( const FString& Text, int32 MaxChars )
	{
		return Text.Len() > MaxChars ? Text.Left( MaxChars - 1 ) + "~" : Text;
	}

	// 矢印 1 本の見た目
	struct FEdgeStyle
	{
		ImU32 Color = ColorArrowIdle;
		float32 Thickness = 1.6f;
		// 最後に遷移してからの秒数（遷移したことがなければ、とても大きい値）
		float32 FiredAge = 1.0e9f;
		bool bReady = false;
		// 強調の順位（中継ノードの色を決める。大きいほど目立つ）
		int32 Rank = 0;
	};
}

FString NDebugUI::DescribeBehaviorDecorator( const FBTDecoratorDef& Decorator )
{
	FString Text = NAI::ToString( Decorator.Type );
	switch ( Decorator.Type )
	{
	case EBTDecoratorType::BlackboardCondition:
		Text += FString::Printf( " %s%s %s %s", Decorator.bNot ? "not " : "", *Decorator.Key, NAI::ToString( Decorator.Op ), *NJson::FromJsonString( Decorator.Value.dump() ) );
		break;
	case EBTDecoratorType::Condition:
		Text += FString::Printf( " %s'%s'", Decorator.bNot ? "not " : "", *Decorator.Call.Do );
		break;
	case EBTDecoratorType::Cooldown:
	case EBTDecoratorType::TimeLimit:
		Text += FString::Printf( " %g s", static_cast<float64>( Decorator.Seconds ) );
		break;
	case EBTDecoratorType::Repeat:
		Text += FString::Printf( " %s%s", Decorator.Count > 0 ? *FString::Printf( "x%d", Decorator.Count ) : "forever", Decorator.bUntilSuccess ? " until success" : "" );
		break;
	default:
		break;
	}
	if ( Decorator.Abort != EBTAbortMode::None )
	{
		Text += FString::Printf( "  [abort: %s]", NAI::ToString( Decorator.Abort ) );
	}
	return Text;
}

//=============================================================================
// ステートマシン
//=============================================================================
void NDebugUI::DrawStateMachineGraph( FAIGraphState& State, CStateMachineComponent& Component )
{
	const CStateMachine* Machine = Component.GetStateMachine();
	if ( Machine == nullptr )
	{
		return;
	}
	if ( State.ShownStateMachine != Machine )
	{
		State.ShownStateMachine = Machine;
		State.StateMachineView.bFitted = false;
	}

	const FStateMachineDefinition& Definition = Machine->GetDefinition();
	TArray<FStateTransitionInfo> Transitions;
	Machine->GetTransitions( Transitions );
	const FStateMachineGraphLayout Layout = NAIGraph::BuildStateMachineLayout( Definition, Transitions );

	// 今の状態から、矢印の見た目を決める
	const TArray<int32>& Active = Machine->GetActivePath();
	const double Now = Component.GetTime();
	TArray<FStateTransitionCandidate> Candidates;
	Component.GetCandidateTransitions( Candidates );
	TArray<FStateHistoryEntry> History;
	Machine->GetHistory( History );
	TArray<FEdgeStyle> Styles;
	Styles.SetNum( Transitions.Num() );
	for ( int32 Id = 0; Id < Styles.Num(); ++Id )
	{
		Styles[Id].Color = Transitions[Id].bAnyTransition ? ColorAnyArrowIdle : ColorArrowIdle;
	}
	for ( const FStateTransitionCandidate& Candidate : Candidates )
	{
		if ( Candidate.Id >= 0 && Candidate.Id < Styles.Num() )
		{
			FEdgeStyle& Style = Styles[Candidate.Id];
			Style.bReady = Candidate.bConditionsMet;
			Style.Thickness = 2.2f;
			Style.Color = Transitions[Candidate.Id].bAnyTransition ? ColorAnyArrowPossible : ColorArrowPossible;
			Style.Rank = 1;
		}
	}
	for ( const FStateHistoryEntry& Entry : History )
	{
		if ( Entry.Transition >= 0 && Entry.Transition < Styles.Num() )
		{
			Styles[Entry.Transition].FiredAge = NMath::Min( Styles[Entry.Transition].FiredAge, static_cast<float32>( NMath::Max( Now - Entry.Time, 0.0 ) ) );
		}
	}
	for ( int32 Id = 0; Id < Styles.Num(); ++Id )
	{
		FEdgeStyle& Style = Styles[Id];
		if ( Style.FiredAge < FiredFadeSeconds )
		{
			const float32 T = 1.0f - Style.FiredAge / FiredFadeSeconds;
			Style.Color = NNodeGraph::Lerp( Style.bReady ? ColorYellow : ( Transitions[Id].bAnyTransition ? ColorAnyArrowIdle : ColorArrowIdle ), ColorGreen, T );
			Style.Thickness = 2.0f + 3.0f * T;
			Style.Rank = 3;
		}
		else if ( Style.bReady )
		{
			Style.Color = ColorYellow;
			Style.Thickness = 3.0f;
			Style.Rank = 2;
		}
	}

	// ツールバー
	if ( ImGui::Button( "Fit##fsmgraph" ) )
	{
		State.StateMachineView.bFitted = false;
	}
	ImGui::SameLine();
	ImGui::TextColored( ImVec4( 0.45f, 1.0f, 0.55f, 1.0f ), "fired" );
	ImGui::SameLine();
	ImGui::TextColored( ImVec4( 0.94f, 0.82f, 0.31f, 1.0f ), "ready" );
	ImGui::SameLine();
	ImGui::TextColored( ImVec4( 0.67f, 0.71f, 0.84f, 1.0f ), "possible" );
	ImGui::SameLine();
	ImGui::TextColored( ImVec4( 0.36f, 0.37f, 0.41f, 1.0f ), "other" );
	ImGui::SameLine();
	ImGui::TextDisabled( "(drag: pan, wheel: zoom)" );

	FNodeGraphCanvas Canvas;
	if ( !Canvas.Begin( "##fsmgraph", State.StateMachineView, ImVec2( 0.0f, State.Height ), Layout.Content ) )
	{
		return;
	}
	const char* HoverText = nullptr;
	FString HoverBuffer;

	// 1) 入れ子のマシンの枠（親が先）
	for ( int32 Index = 0; Index < Definition.States.Num(); ++Index )
	{
		const FStateDef& Def = Definition.States[Index];
		if ( !Def.bMachine )
		{
			continue;
		}
		const bool bActive = Active.Contains( Index );
		const bool bHovered = Canvas.DrawBox( Layout.Pos[Index], Layout.Size[Index], ColorContainerFill, bActive ? ColorGreen : ColorBorder, bActive ? 2.0f : 1.0f, 8.0f );
		Canvas.DrawText( Add( Layout.Pos[Index], ImVec2( 10.0f, 5.0f ) ), bActive ? ColorGreen : ColorDimText, *Def.Name, 14.0f );
		if ( !Def.Entry.IsEmpty() )
		{
			const FString EntryText = FString::Printf( "entry %s", *Def.Entry );
			Canvas.DrawText( Add( Layout.Pos[Index], ImVec2( Layout.Size[Index].x - FNodeGraphCanvas::MeasureText( *EntryText, 11.0f ) - 10.0f, 7.0f ) ), ColorDimText, *EntryText, 11.0f );
		}
		if ( bHovered && HoverText == nullptr )
		{
			HoverBuffer = "State machine: " + Definition.GetPath( Index );
			HoverText = *HoverBuffer;
		}
	}

	// 2) 矢印（直角に折れる線）と、ラベル
	for ( const FStateGraphRoute& Route : Layout.Routes )
	{
		const FEdgeStyle& Style = Styles[Route.Id];
		const bool bNear = Canvas.DrawElbowArrow( Route.Points.GetData(), Route.Points.Num(), Style.Color, Style.Thickness );
		if ( bNear )
		{
			HoverBuffer = Transitions[Route.Id].Description;
			if ( Style.FiredAge < 1.0e8f )
			{
				HoverBuffer += FString::Printf( "\nlast fired %.1f s ago", static_cast<float64>( Style.FiredAge ) );
			}
			if ( Style.bReady )
			{
				HoverBuffer += "\nconditions are met now";
			}
			HoverText = *HoverBuffer;
		}
		// 遷移した直後・条件が満たされている矢印には、短い説明を添える
		if ( Style.FiredAge < FiredFadeSeconds || Style.bReady )
		{
			const FString& Description = Transitions[Route.Id].Description;
			const int32 WhenAt = Description.Find( " when " );
			const FString Label = Truncated( WhenAt != INDEX_NONE ? Description.Mid( WhenAt + 6 ) : FString( "(always)" ), 26 );
			Canvas.DrawText( ImVec2( Route.Label.x - FNodeGraphCanvas::MeasureText( *Label, 11.0f ) * 0.5f, Route.Label.y - 18.0f ), Style.Color, *Label, 11.0f );
		}
	}

	// 入口の印（入口の子へ入る、短い矢印）
	for ( int32 Parent = -1; Parent < Definition.States.Num(); ++Parent )
	{
		const FStateGraphMachine& MachineLayout = Layout.Machines[Parent + 1];
		if ( MachineLayout.EntryChild < 0 )
		{
			continue;
		}
		const ImVec2 End( Layout.Pos[MachineLayout.EntryChild].x, Layout.Pos[MachineLayout.EntryChild].y + Layout.Size[MachineLayout.EntryChild].y * 0.5f );
		// 黒い点から出る短い矢印と「start」の文字（Any State などの遷移の矢印と見分けるため）
		const ImVec2 EntryRoute[2] = { ImVec2( End.x - 32.0f, End.y ), End };
		Canvas.DrawDot( EntryRoute[0], ColorDimText );
		Canvas.DrawElbowArrow( EntryRoute, 2, ColorDimText, 1.6f );
		Canvas.DrawText( ImVec2( EntryRoute[0].x - 4.0f, End.y - 17.0f ), ColorDimText, "start", 10.0f );
	}

	// 3) 中継ノード（通る矢印のうち、いちばん目立つものの色にする）
	for ( const FStateGraphRelay& Relay : Layout.Relays )
	{
		int32 Best = Relay.Ids[0];
		for ( const int32 Id : Relay.Ids )
		{
			if ( Styles[Id].Rank > Styles[Best].Rank )
			{
				Best = Id;
			}
		}
		if ( Canvas.DrawRelay( Relay.Pos, Styles[Best].Color ) && HoverText == nullptr )
		{
			HoverBuffer = "Relay";
			for ( const int32 Id : Relay.Ids )
			{
				HoverBuffer += "\n" + Transitions[Id].Description;
			}
			HoverText = *HoverBuffer;
		}
	}

	// 4) ステート（葉）と、ANY の箱
	for ( int32 Index = 0; Index < Definition.States.Num(); ++Index )
	{
		const FStateDef& Def = Definition.States[Index];
		if ( Def.bMachine )
		{
			continue;
		}
		const int32 Depth = Active.Find( Index );
		const bool bActive = Depth != INDEX_NONE;
		const ImVec2 Min = Layout.Pos[Index];
		const bool bHovered = Canvas.DrawBox( Min, Layout.Size[Index], bActive ? ColorActiveFill : ColorNodeFill, bActive ? ColorGreen : ColorBorder, bActive ? 3.0f : 1.5f, 7.0f );
		Canvas.DrawText( Add( Min, ImVec2( 10.0f, 7.0f ) ), bActive ? ColorGreen : ColorText, *Def.Name, 14.0f, Min, Layout.Size[Index] );
		if ( !Def.Clip.IsEmpty() )
		{
			Canvas.DrawText( Add( Min, ImVec2( 10.0f, 27.0f ) ), ColorDimText, *FString::Printf( "clip %s", *Def.Clip ), 11.0f, Min, Layout.Size[Index] );
		}
		if ( bActive )
		{
			const FString TimeText = FString::Printf( "%.1f s", static_cast<float64>( Machine->GetStateTime( Depth ) ) );
			Canvas.DrawText( Add( Min, ImVec2( Layout.Size[Index].x - FNodeGraphCanvas::MeasureText( *TimeText, 11.0f ) - 8.0f, 29.0f ) ), ColorGreen, *TimeText, 11.0f );
		}
		if ( bHovered && HoverText == nullptr )
		{
			HoverBuffer = "State: " + Definition.GetPath( Index ) + ( Def.Clip.IsEmpty() ? FString() : "\nclip: " + Def.Clip );
			HoverText = *HoverBuffer;
		}
	}
	for ( int32 Parent = -1; Parent < Definition.States.Num(); ++Parent )
	{
		const FStateGraphMachine& MachineLayout = Layout.Machines[Parent + 1];
		if ( !MachineLayout.bHasAny )
		{
			continue;
		}
		const bool bHovered = Canvas.DrawBox( MachineLayout.AnyPos, ImVec2( NAIGraph::AnyWidth, NAIGraph::AnyHeight ), IM_COL32( 70, 52, 34, 255 ), ColorAnyArrowPossible, 1.5f, 7.0f );
		Canvas.DrawText( Add( MachineLayout.AnyPos, ImVec2( 12.0f, 9.0f ) ), ColorOrange, "ANY", 14.0f );
		if ( bHovered && HoverText == nullptr )
		{
			HoverBuffer = "Any State transitions: they can happen from any state of this machine.";
			HoverText = *HoverBuffer;
		}
	}
	Canvas.End();
	if ( HoverText != nullptr )
	{
		ImGui::SetTooltip( "%s", HoverText );
	}
}

//=============================================================================
// ビヘイビアツリー
//=============================================================================
namespace
{
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
		case EBTNodeType::Task:
			return IM_COL32( 56, 56, 62, 255 );
		}
		return ColorNodeFill;
	}
}

void NDebugUI::DrawBehaviorTreeGraph( FAIGraphState& State, CBehaviorTreeComponent& Component )
{
	const CBehaviorTree* Tree = Component.GetBehaviorTree();
	if ( Tree == nullptr )
	{
		return;
	}
	if ( State.ShownBehaviorTree != Tree )
	{
		State.ShownBehaviorTree = Tree;
		State.BehaviorTreeView.bFitted = false;
	}
	const FBehaviorTreeDefinition& Definition = Tree->GetDefinition();
	const int32 NumNodes = Definition.Nodes.Num();
	const double Now = Component.GetTime();
	const FBehaviorTreeGraphLayout Layout = NAIGraph::BuildBehaviorTreeLayout( Definition );

	if ( ImGui::Button( "Fit##btgraph" ) )
	{
		State.BehaviorTreeView.bFitted = false;
	}
	ImGui::SameLine();
	ImGui::TextColored( ImVec4( 0.45f, 1.0f, 0.55f, 1.0f ), "active" );
	ImGui::SameLine();
	ImGui::TextColored( ImVec4( 0.94f, 0.82f, 0.31f, 1.0f ), "running task" );
	ImGui::SameLine();
	ImGui::TextColored( ImVec4( 0.55f, 0.85f, 0.6f, 1.0f ), "success" );
	ImGui::SameLine();
	ImGui::TextColored( ImVec4( 1.0f, 0.5f, 0.45f, 1.0f ), "failure" );
	ImGui::SameLine();
	ImGui::TextColored( ImVec4( 1.0f, 0.65f, 0.3f, 1.0f ), "aborted" );
	ImGui::SameLine();
	ImGui::TextDisabled( "(children are in priority order)" );

	FNodeGraphCanvas Canvas;
	if ( !Canvas.Begin( "##btgraph", State.BehaviorTreeView, ImVec2( 0.0f, State.Height ), Layout.Content ) )
	{
		return;
	}

	// 結果の色（終わった直後ほど明るく、時間とともに暗くなる）
	auto ResultColor = [&]( int32 Node )
	{
		if ( !Tree->HasNodeResult( Node ) )
		{
			return ColorBorder;
		}
		const ImU32 Base = Tree->WasNodeAborted( Node ) ? ColorOrange : ( Tree->GetNodeResult( Node ) == EBTStatus::Success ? ColorGreen : ColorRed );
		const float32 Age = static_cast<float32>( NMath::Max( Now - Tree->GetNodeResultTime( Node ), 0.0 ) );
		return NNodeGraph::Lerp( NNodeGraph::Lerp( Base, ColorBorder, 0.6f ), Base, 1.0f - NMath::Min( Age / ResultFadeSeconds, 1.0f ) );
	};

	// 1) 親から子への線（動いている経路は、緑で太く）
	for ( int32 Node = 0; Node < NumNodes; ++Node )
	{
		if ( Layout.Links[Node].IsEmpty() )
		{
			continue;
		}
		const bool bActive = Tree->IsNodeActive( Node );
		Canvas.DrawElbowArrow( Layout.Links[Node].GetData(), Layout.Links[Node].Num(), bActive ? ColorGreen : ResultColor( Node ), bActive ? 3.4f : 1.8f );
	}
	// 中継ノード（子へ分かれる所。子のどれかが動いていれば緑）
	for ( const FBehaviorTreeGraphRelay& Relay : Layout.Relays )
	{
		bool bAnyActive = false;
		for ( const int32 Child : Definition.GetChildren( Relay.Parent ) )
		{
			bAnyActive = bAnyActive || Tree->IsNodeActive( Child );
		}
		Canvas.DrawRelay( Relay.Pos, bAnyActive ? ColorGreen : ColorBorder );
	}

	// 2) ノード
	const char* HoverText = nullptr;
	FString HoverBuffer;
	for ( int32 Node = 0; Node < NumNodes; ++Node )
	{
		const FBTNodeDef& Def = Definition.Nodes[Node];
		const bool bActive = Tree->IsNodeActive( Node );
		const ImVec2 Min = Layout.Pos[Node];
		const ImVec2 Size = Layout.Size[Node];
		const ImU32 Border = bActive ? ( Def.Type == EBTNodeType::Task ? ColorYellow : ColorGreen ) : ColorBorder;
		const ImU32 Fill = bActive ? NNodeGraph::Lerp( TypeFill( Def.Type ), Border, 0.28f ) : TypeFill( Def.Type );
		const bool bHovered = Canvas.DrawBox( Min, Size, Fill, Border, bActive ? 3.0f : 1.4f, 7.0f );
		Canvas.DrawBand( Min, Size.x, 5.0f, ResultColor( Node ), 7.0f );

		// 優先順位（兄弟の中の何番目か）
		if ( Def.Parent >= 0 )
		{
			const TArray<int32> Siblings = Definition.GetChildren( Def.Parent );
			Canvas.DrawText( Add( Min, ImVec2( Size.x - 22.0f, 9.0f ) ), ColorDimText, *FString::Printf( "#%d", Siblings.Find( Node ) + 1 ), 11.0f );
		}
		Canvas.DrawText( Add( Min, ImVec2( 8.0f, 8.0f ) ), bActive ? ColorGreen : ColorText, *Truncated( Definition.GetNodeLabel( Node ), 24 ), 14.0f, Min, ImVec2( Size.x - 26.0f, Size.y ) );
		FString Sub = Def.Type == EBTNodeType::Task ? FString::Printf( "<%s>", *Def.Task.Do ) : FString( NAI::ToString( Def.Type ) );
		if ( Def.Type == EBTNodeType::Parallel )
		{
			Sub += FString::Printf( " %s", NAI::ToString( Def.ParallelMode ) );
		}
		if ( Tree->HasNodeResult( Node ) && !bActive )
		{
			Sub += Tree->WasNodeAborted( Node ) ? "  aborted" : ( Tree->GetNodeResult( Node ) == EBTStatus::Success ? "  success" : "  failure" );
		}
		else if ( bActive )
		{
			Sub += "  ACTIVE";
		}
		Canvas.DrawText( Add( Min, ImVec2( 8.0f, 25.0f ) ), ColorDimText, *Sub, 11.0f, Min, Size );

		// デコレーターの条件の真偽（先頭）と、サービス
		float32 LineY = NAIGraph::TreeHeaderHeight + 2.0f;
		for ( int32 Index = 0; Index < Def.Decorators.Num(); ++Index )
		{
			const FBTDecoratorDef& Decorator = Def.Decorators[Index];
			const bool bIsCondition = Decorator.Type == EBTDecoratorType::BlackboardCondition || Decorator.Type == EBTDecoratorType::Condition;
			ImU32 Mark = ColorDimText;
			const char* Marker = "  *";
			if ( bIsCondition )
			{
				const bool bValue = Tree->GetDecoratorResult( Node, Index );
				Mark = bValue ? ColorGreen : ColorRed;
				Marker = bValue ? "  T" : "  F";
			}
			Canvas.DrawText( Add( Min, ImVec2( 4.0f, LineY ) ), Mark, Marker, 11.0f );
			Canvas.DrawText( Add( Min, ImVec2( 26.0f, LineY ) ), bIsCondition ? ColorText : ColorDimText, *NDebugUI::DescribeBehaviorDecorator( Decorator ), 11.0f, Min, Size );
			LineY += NAIGraph::TreeLineHeight;
		}
		for ( const FBTServiceDef& Service : Def.Services )
		{
			Canvas.DrawText( Add( Min, ImVec2( 4.0f, LineY ) ), ColorDimText, "  ~", 11.0f );
			Canvas.DrawText( Add( Min, ImVec2( 26.0f, LineY ) ), ColorDimText, *FString::Printf( "service %s / %g s", *Service.Call.Do, static_cast<float64>( Service.Interval ) ), 11.0f, Min, Size );
			LineY += NAIGraph::TreeLineHeight;
		}

		if ( bHovered && HoverText == nullptr )
		{
			HoverBuffer = Definition.GetNodeLabel( Node ) + "\n" + Sub;
			for ( int32 Index = 0; Index < Def.Decorators.Num(); ++Index )
			{
				HoverBuffer += "\n" + NDebugUI::DescribeBehaviorDecorator( Def.Decorators[Index] );
			}
			if ( Tree->HasNodeResult( Node ) )
			{
				HoverBuffer += FString::Printf( "\nlast result %.1f s ago", NMath::Max( Now - Tree->GetNodeResultTime( Node ), 0.0 ) );
			}
			HoverText = *HoverBuffer;
		}
	}
	Canvas.End();
	if ( HoverText != nullptr )
	{
		ImGui::SetTooltip( "%s", HoverText );
	}
}
