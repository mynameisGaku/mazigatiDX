// SPDX-License-Identifier: MIT
#include "AIEditorInternal.h"
#include "AIEditorStateMachineInternal.h"

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

using namespace NAIEditorStateMachineUI;

namespace NAIEditorStateMachineUI
{
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

	// マシン Machine の、From（空なら Any State）から To への遷移の番号（無ければ -1）
	int32 FindTransition( const FStateMachineDefinition& Definition, int32 Machine, bool bAny, const FString& From, const FString& To )
	{
		const TArray<FStateTransitionDef>& List = TransitionList( Definition, Machine, bAny );
		for ( int32 Index = 0; Index < List.Num(); ++Index )
		{
			if ( List[Index].To == To && ( bAny || List[Index].From == From ) )
			{
				return Index;
			}
		}
		return -1;
	}

	// 矢印をクリックしたとき: まとめた矢印（同じ 2 つのステートの間の遷移が複数）なら、クリックのたびに、組の中の次の遷移を選ぶ
	void SelectRoute( FState& S, const FTransitionList& Transitions, const FStateGraphRoute& Route, bool bCycle )
	{
		int32 Pick = Route.Ids[0];
		for ( int32 Index = 0; Index < Route.Ids.Num(); ++Index )
		{
			if ( SameRef( Transitions.Refs[Route.Ids[Index]], S.SelectedTransition ) )
			{
				Pick = bCycle ? Route.Ids[( Index + 1 ) % Route.Ids.Num()] : Route.Ids[Index];
				break;
			}
		}
		const FAIEditorTransitionRef& Ref = Transitions.Refs[Pick];
		SelectTransition( S, Ref.Machine, Ref.bAny, Ref.Index );
	}

	void AddTransitionTo( FState& S, int32 State, int32 Target )
	{
		const FStateMachineDefinition& Definition = S.StateMachine.Get();
		const int32 Machine = Definition.States[State].Parent;
		// すでにつながっているなら、増やさずに、その遷移を選ぶ（条件を増やしたいときは、Inspector の条件か、Duplicate を使う）
		const int32 Existing = FindTransition( Definition, Machine, false, Definition.States[State].Name, Definition.States[Target].Name );
		if ( Existing >= 0 )
		{
			SelectTransition( S, Machine, false, Existing );
			NAIEditor::SetStatus( S, "Already connected: " + Definition.States[State].Name + " -> " + Definition.States[Target].Name + " (selected the existing transition)." );
			return;
		}
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
		const int32 Existing = FindTransition( Definition, Machine, true, FString(), Definition.States[State].Name );
		if ( Existing >= 0 )
		{
			SelectTransition( S, Machine, true, Existing );
			NAIEditor::SetStatus( S, "An Any State transition to " + Definition.States[State].Name + " already exists (selected it)." );
			return;
		}
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
