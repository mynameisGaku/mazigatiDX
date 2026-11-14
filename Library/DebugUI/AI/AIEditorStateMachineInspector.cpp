// SPDX-License-Identifier: MIT
#include "AIEditorStateMachineInternal.h"

namespace NAIEditorStateMachineUI
{
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
		if ( ImGui::Button( "Duplicate" ) )
		{
			// 同じ 2 つのステートの間に、もう 1 本（条件や優先度を変えて使う）。グラフでは、1 本の矢印にまとまり、本数が出る
			const int32 Copy = NStateMachineEdit::AddTransition( S.StateMachine, Ref.Machine, Ref.bAny, Edited.From, Edited.To );
			if ( Copy != INDEX_NONE )
			{
				NStateMachineEdit::SetTransition( S.StateMachine, Ref.Machine, Ref.bAny, Copy, Edited );
				SelectTransition( S, Ref.Machine, Ref.bAny, Copy );
				return;
			}
		}
		ImGui::SameLine();
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
