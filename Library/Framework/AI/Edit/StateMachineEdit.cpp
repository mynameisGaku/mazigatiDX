// SPDX-License-Identifier: MIT
#include "StateMachineEdit.h"

#include "Library/Framework/Asset/Definition/DefinitionEditing.h"

namespace
{
	bool IsValidState( const FStateMachineDefinition& Definition, int32 StateIndex )
	{
		return StateIndex >= 0 && StateIndex < Definition.States.Num();
	}

	// 遷移を持てるマシンか（-1 は最上位）
	bool IsMachine( const FStateMachineDefinition& Definition, int32 Machine )
	{
		return Machine == -1 || ( IsValidState( Definition, Machine ) && Definition.States[Machine].bMachine );
	}

	TArray<FStateTransitionDef>& GetTransitionList( FStateMachineDefinition& Definition, int32 Machine, bool bAny )
	{
		if ( Machine < 0 )
		{
			return bAny ? Definition.AnyTransitions : Definition.Transitions;
		}
		return bAny ? Definition.States[Machine].AnyTransitions : Definition.States[Machine].Transitions;
	}

	const TArray<FStateTransitionDef>& GetTransitionList( const FStateMachineDefinition& Definition, int32 Machine, bool bAny )
	{
		if ( Machine < 0 )
		{
			return bAny ? Definition.AnyTransitions : Definition.Transitions;
		}
		return bAny ? Definition.States[Machine].AnyTransitions : Definition.States[Machine].Transitions;
	}

	FString& GetEntry( FStateMachineDefinition& Definition, int32 Machine )
	{
		return Machine < 0 ? Definition.Entry : Definition.States[Machine].Entry;
	}

	// 状態名に使えない文字（経路の区切り "/"）を直す
	FString SanitizeName( const FString& Name )
	{
		return Name.Replace( "/", "_" );
	}

	TArray<FString> SiblingNames( const FStateMachineDefinition& Definition, int32 Parent, int32 ExceptIndex )
	{
		TArray<FString> Names;
		for ( int32 Index = 0; Index < Definition.States.Num(); ++Index )
		{
			if ( Index != ExceptIndex && Definition.States[Index].Parent == Parent )
			{
				Names.Add( Definition.States[Index].Name );
			}
		}
		return Names;
	}

	// 定義の中のマシンの番号（最上位 = -1 を含む）
	TArray<int32> AllMachines( const FStateMachineDefinition& Definition )
	{
		TArray<int32> Machines;
		Machines.Add( -1 );
		for ( int32 Index = 0; Index < Definition.States.Num(); ++Index )
		{
			if ( Definition.States[Index].bMachine )
			{
				Machines.Add( Index );
			}
		}
		return Machines;
	}

	// Machine から Path を辿り、Target のステートに当たる区間の名前を NewName にした経路を返す（当たらなければ、そのまま）
	FString RewritePath( const FStateMachineDefinition& Definition, int32 Machine, const FString& Path, int32 Target, const FString& NewName )
	{
		if ( Path.IsEmpty() )
		{
			return Path;
		}
		TArray<FString> Segments = Path.ParseIntoArray( "/" );
		int32 Current = Machine;
		for ( FString& Segment : Segments )
		{
			const int32 Child = Definition.FindChild( Current, Segment );
			if ( Child < 0 )
			{
				break;
			}
			if ( Child == Target )
			{
				Segment = NewName;
			}
			Current = Child;
		}
		return FString::Join( Segments, "/" );
	}

	TArray<FAICall>& GetActions( FStateDef& State, EStateActionList List )
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

	const TArray<FAICall>& GetActions( const FStateDef& State, EStateActionList List )
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

	// すべての遷移の、すべての条件に Function を呼ぶ（Definition が const なら、条件も const）
	template<typename TDefinition, typename TFunction>
	void ForEachCondition( TDefinition& Definition, TFunction Function )
	{
		for ( const int32 Machine : AllMachines( Definition ) )
		{
			for ( int32 Pass = 0; Pass < 2; ++Pass )
			{
				for ( auto& Transition : GetTransitionList( Definition, Machine, Pass == 1 ) )
				{
					for ( auto& Condition : Transition.When )
					{
						Function( Condition );
					}
				}
			}
		}
	}
}

void NStateMachineEdit::SetBlackboard( TStateMachineDocument& Document, const FString& Path )
{
	if ( Document.Get().Blackboard != Path )
	{
		Document.BeginEdit().Blackboard = Path;
	}
}

void NStateMachineEdit::SetEntry( TStateMachineDocument& Document, int32 Machine, const FString& Name )
{
	if ( !IsMachine( Document.Get(), Machine ) )
	{
		return;
	}
	const FStateMachineDefinition& Current = Document.Get();
	if ( ( Machine < 0 ? Current.Entry : Current.States[Machine].Entry ) != Name )
	{
		GetEntry( Document.BeginEdit(), Machine ) = Name;
	}
}

int32 NStateMachineEdit::AddState( TStateMachineDocument& Document, int32 Parent, const FString& Name, bool bMachine )
{
	if ( Parent < -1 || Parent >= Document.Get().States.Num() )
	{
		return INDEX_NONE;
	}
	FStateDef State;
	State.Name = NDefinitionEdit::MakeUniqueName( SiblingNames( Document.Get(), Parent, INDEX_NONE ), SanitizeName( Name ), "State" );
	State.Parent = Parent;
	State.bMachine = bMachine;

	FStateMachineDefinition& Definition = Document.BeginEdit();
	if ( Parent >= 0 )
	{
		Definition.States[Parent].bMachine = true;
	}
	const int32 Index = Definition.States.Add( State );
	FString& Entry = GetEntry( Definition, Parent );
	if ( Entry.IsEmpty() )
	{
		Entry = State.Name;
	}
	return Index;
}

void NStateMachineEdit::RemoveState( TStateMachineDocument& Document, int32 StateIndex )
{
	if ( !IsValidState( Document.Get(), StateIndex ) )
	{
		return;
	}
	FStateMachineDefinition& Definition = Document.BeginEdit();
	const int32 Num = Definition.States.Num();

	// 子孫ごと（親の前に子が来る順なので、前から 1 回で決まる）
	TArray<bool> Removed;
	Removed.SetNum( Num, false );
	Removed[StateIndex] = true;
	for ( int32 Index = 0; Index < Num; ++Index )
	{
		const int32 Parent = Definition.States[Index].Parent;
		if ( Parent >= 0 && Removed[Parent] )
		{
			Removed[Index] = true;
		}
	}

	// 消えるステートを指す遷移と Entry を直す（番号がずれる前の定義で引く）
	for ( const int32 Machine : AllMachines( Definition ) )
	{
		if ( Machine >= 0 && Removed[Machine] )
		{
			continue;
		}
		for ( int32 Pass = 0; Pass < 2; ++Pass )
		{
			TArray<FStateTransitionDef>& List = GetTransitionList( Definition, Machine, Pass == 1 );
			for ( int32 Index = List.Num() - 1; Index >= 0; --Index )
			{
				const int32 From = List[Index].From.IsEmpty() ? -1 : Definition.FindChild( Machine, List[Index].From );
				const int32 To = Definition.FindByPath( Machine, List[Index].To );
				if ( ( From >= 0 && Removed[From] ) || ( To >= 0 && Removed[To] ) )
				{
					List.RemoveAt( Index );
				}
			}
		}
		FString& Entry = GetEntry( Definition, Machine );
		const int32 EntryState = Entry.IsEmpty() ? -1 : Definition.FindByPath( Machine, Entry );
		if ( EntryState >= 0 && Removed[EntryState] )
		{
			Entry = FString();
			for ( const int32 Child : Definition.GetChildren( Machine ) )
			{
				if ( !Removed[Child] )
				{
					Entry = Definition.States[Child].Name;
					break;
				}
			}
		}
	}

	// 詰め直す
	TArray<int32> NewIndex;
	NewIndex.SetNum( Num, -1 );
	TArray<FStateDef> Kept;
	for ( int32 Index = 0; Index < Num; ++Index )
	{
		if ( !Removed[Index] )
		{
			NewIndex[Index] = Kept.Num();
			Kept.Add( Definition.States[Index] );
		}
	}
	for ( FStateDef& State : Kept )
	{
		State.Parent = State.Parent >= 0 ? NewIndex[State.Parent] : -1;
	}
	Definition.States = Kept;
}

FString NStateMachineEdit::RenameState( TStateMachineDocument& Document, int32 StateIndex, const FString& Name )
{
	if ( !IsValidState( Document.Get(), StateIndex ) )
	{
		return FString();
	}
	const FStateMachineDefinition& Current = Document.Get();
	const FString Unique = NDefinitionEdit::MakeUniqueName( SiblingNames( Current, Current.States[StateIndex].Parent, StateIndex ), SanitizeName( Name ), "State" );
	if ( Unique == Current.States[StateIndex].Name )
	{
		return Unique;
	}

	FStateMachineDefinition& Definition = Document.BeginEdit();
	// 参照を、まだ古い名前のうちに直す（名前を引いて当たりを付けるため）
	for ( const int32 Machine : AllMachines( Definition ) )
	{
		FString& Entry = GetEntry( Definition, Machine );
		Entry = RewritePath( Definition, Machine, Entry, StateIndex, Unique );
		for ( int32 Pass = 0; Pass < 2; ++Pass )
		{
			for ( FStateTransitionDef& Transition : GetTransitionList( Definition, Machine, Pass == 1 ) )
			{
				Transition.From = RewritePath( Definition, Machine, Transition.From, StateIndex, Unique );
				Transition.To = RewritePath( Definition, Machine, Transition.To, StateIndex, Unique );
			}
		}
	}
	Definition.States[StateIndex].Name = Unique;
	return Unique;
}

void NStateMachineEdit::SetStateIsMachine( TStateMachineDocument& Document, int32 StateIndex, bool bMachine )
{
	if ( !IsValidState( Document.Get(), StateIndex ) || Document.Get().States[StateIndex].bMachine == bMachine )
	{
		return;
	}
	if ( !bMachine && !Document.Get().GetChildren( StateIndex ).IsEmpty() )
	{
		return;
	}
	FStateDef& State = Document.BeginEdit().States[StateIndex];
	State.bMachine = bMachine;
	if ( !bMachine )
	{
		State.Entry = FString();
		State.Transitions.Reset();
		State.AnyTransitions.Reset();
	}
}

void NStateMachineEdit::SetStateClip( TStateMachineDocument& Document, int32 StateIndex, const FString& Clip, const char* CoalesceKey )
{
	if ( IsValidState( Document.Get(), StateIndex ) && Document.Get().States[StateIndex].Clip != Clip )
	{
		Document.BeginEdit( CoalesceKey ).States[StateIndex].Clip = Clip;
	}
}

void NStateMachineEdit::SetStatePosition( TStateMachineDocument& Document, int32 StateIndex, float32 X, float32 Y, const char* CoalesceKey )
{
	if ( !IsValidState( Document.Get(), StateIndex ) )
	{
		return;
	}
	const NlohmannJson& Editor = Document.Get().States[StateIndex].Editor;
	if ( Editor.is_object() && Editor.value( "X", 0.0f ) == X && Editor.value( "Y", 0.0f ) == Y && Editor.contains( "X" ) && Editor.contains( "Y" ) )
	{
		return;
	}
	NlohmannJson& Target = Document.BeginEdit( CoalesceKey ).States[StateIndex].Editor;
	if ( !Target.is_object() )
	{
		Target = NlohmannJson::object();
	}
	Target["X"] = X;
	Target["Y"] = Y;
}

int32 NStateMachineEdit::AddTransition( TStateMachineDocument& Document, int32 Machine, bool bAny, const FString& From, const FString& To )
{
	if ( !IsMachine( Document.Get(), Machine ) )
	{
		return INDEX_NONE;
	}
	FStateTransitionDef Transition;
	Transition.From = bAny ? FString() : From;
	Transition.To = To;
	return GetTransitionList( Document.BeginEdit(), Machine, bAny ).Add( Transition );
}

void NStateMachineEdit::RemoveTransition( TStateMachineDocument& Document, int32 Machine, bool bAny, int32 Index )
{
	if ( !IsMachine( Document.Get(), Machine ) || Index < 0 || Index >= GetTransitionList( Document.Get(), Machine, bAny ).Num() )
	{
		return;
	}
	GetTransitionList( Document.BeginEdit(), Machine, bAny ).RemoveAt( Index );
}

void NStateMachineEdit::MoveTransition( TStateMachineDocument& Document, int32 Machine, bool bAny, int32 From, int32 To )
{
	if ( !IsMachine( Document.Get(), Machine ) )
	{
		return;
	}
	const int32 Num = GetTransitionList( Document.Get(), Machine, bAny ).Num();
	if ( From < 0 || From >= Num || To < 0 || To >= Num || From == To )
	{
		return;
	}
	TArray<FStateTransitionDef>& List = GetTransitionList( Document.BeginEdit(), Machine, bAny );
	FStateTransitionDef Moved = List[From];
	List.RemoveAt( From );
	List.Insert( Moved, To );
}

void NStateMachineEdit::SetTransition( TStateMachineDocument& Document, int32 Machine, bool bAny, int32 Index, const FStateTransitionDef& Transition, const char* CoalesceKey )
{
	if ( !IsMachine( Document.Get(), Machine ) || Index < 0 || Index >= GetTransitionList( Document.Get(), Machine, bAny ).Num() )
	{
		return;
	}
	FStateTransitionDef Fixed = Transition;
	if ( bAny )
	{
		Fixed.From = FString();
	}
	GetTransitionList( Document.BeginEdit( CoalesceKey ), Machine, bAny )[Index] = Fixed;
}

int32 NStateMachineEdit::AddAction( TStateMachineDocument& Document, int32 StateIndex, EStateActionList List, const FAICall& Call )
{
	if ( !IsValidState( Document.Get(), StateIndex ) )
	{
		return INDEX_NONE;
	}
	return GetActions( Document.BeginEdit().States[StateIndex], List ).Add( Call );
}

void NStateMachineEdit::RemoveAction( TStateMachineDocument& Document, int32 StateIndex, EStateActionList List, int32 Index )
{
	if ( !IsValidState( Document.Get(), StateIndex ) || Index < 0 || Index >= GetActions( Document.Get().States[StateIndex], List ).Num() )
	{
		return;
	}
	GetActions( Document.BeginEdit().States[StateIndex], List ).RemoveAt( Index );
}

void NStateMachineEdit::MoveAction( TStateMachineDocument& Document, int32 StateIndex, EStateActionList List, int32 From, int32 To )
{
	if ( !IsValidState( Document.Get(), StateIndex ) )
	{
		return;
	}
	const int32 Num = GetActions( Document.Get().States[StateIndex], List ).Num();
	if ( From < 0 || From >= Num || To < 0 || To >= Num || From == To )
	{
		return;
	}
	TArray<FAICall>& Actions = GetActions( Document.BeginEdit().States[StateIndex], List );
	FAICall Moved = Actions[From];
	Actions.RemoveAt( From );
	Actions.Insert( Moved, To );
}

void NStateMachineEdit::SetAction( TStateMachineDocument& Document, int32 StateIndex, EStateActionList List, int32 Index, const FAICall& Call, const char* CoalesceKey )
{
	if ( !IsValidState( Document.Get(), StateIndex ) || Index < 0 || Index >= GetActions( Document.Get().States[StateIndex], List ).Num() )
	{
		return;
	}
	GetActions( Document.BeginEdit( CoalesceKey ).States[StateIndex], List )[Index] = Call;
}

int32 NStateMachineEdit::AddClipEvent( TStateMachineDocument& Document, int32 StateIndex, const FStateClipEventDef& Event )
{
	if ( !IsValidState( Document.Get(), StateIndex ) )
	{
		return INDEX_NONE;
	}
	return Document.BeginEdit().States[StateIndex].OnClipEvent.Add( Event );
}

void NStateMachineEdit::RemoveClipEvent( TStateMachineDocument& Document, int32 StateIndex, int32 Index )
{
	if ( !IsValidState( Document.Get(), StateIndex ) || Index < 0 || Index >= Document.Get().States[StateIndex].OnClipEvent.Num() )
	{
		return;
	}
	Document.BeginEdit().States[StateIndex].OnClipEvent.RemoveAt( Index );
}

void NStateMachineEdit::SetClipEvent( TStateMachineDocument& Document, int32 StateIndex, int32 Index, const FStateClipEventDef& Event, const char* CoalesceKey )
{
	if ( !IsValidState( Document.Get(), StateIndex ) || Index < 0 || Index >= Document.Get().States[StateIndex].OnClipEvent.Num() )
	{
		return;
	}
	Document.BeginEdit( CoalesceKey ).States[StateIndex].OnClipEvent[Index] = Event;
}

int32 NStateMachineEdit::CountBlackboardKeyUsage( const FStateMachineDefinition& Definition, const FString& Key )
{
	int32 Count = 0;
	ForEachCondition( Definition, [&]( const FStateConditionDef& Condition )
	{
		Count += ( Condition.Type == EStateConditionType::BlackboardCompare && Condition.Key == Key ) ? 1 : 0;
	} );
	return Count;
}

int32 NStateMachineEdit::RenameBlackboardKey( TStateMachineDocument& Document, const FString& OldName, const FString& NewName )
{
	if ( OldName == NewName || CountBlackboardKeyUsage( Document.Get(), OldName ) == 0 )
	{
		return 0;
	}
	int32 Changed = 0;
	ForEachCondition( Document.BeginEdit(), [&]( FStateConditionDef& Condition )
	{
		if ( Condition.Type == EStateConditionType::BlackboardCompare && Condition.Key == OldName )
		{
			Condition.Key = NewName;
			++Changed;
		}
	} );
	return Changed;
}
