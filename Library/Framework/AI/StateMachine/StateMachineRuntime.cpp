// SPDX-License-Identifier: MIT
#include "StateMachineRuntime.h"

#include "Library/Core/Interfaces/AI/AIAnimationControl.h"
#include "Library/Core/Log/Log.h"
#include "Library/Framework/AI/AILog.h"
#include "Library/Framework/AI/Blackboard/Blackboard.h"
#include "Library/Json/JsonHelpers.h"

namespace
{
	bool CompareValues( EBlackboardKeyType Type, const FBlackboardValue& Current, EAICompareOp Op, const FBlackboardValue& Expected )
	{
		if ( Op == EAICompareOp::Equal )
		{
			return Current.IsEqual( Type, Expected );
		}
		if ( Op == EAICompareOp::NotEqual )
		{
			return !Current.IsEqual( Type, Expected );
		}

		// 大小の比較は、数（Int / Float / Enum の番号）だけ
		float64 A = 0.0;
		float64 B = 0.0;
		if ( Type == EBlackboardKeyType::Float )
		{
			A = Current.Real;
			B = Expected.Real;
		}
		else if ( Type == EBlackboardKeyType::Int || Type == EBlackboardKeyType::Enum )
		{
			A = static_cast<float64>( Current.Integer );
			B = static_cast<float64>( Expected.Integer );
		}
		else
		{
			return false;
		}
		switch ( Op )
		{
		case EAICompareOp::Less:
			return A < B;
		case EAICompareOp::LessEqual:
			return A <= B;
		case EAICompareOp::Greater:
			return A > B;
		case EAICompareOp::GreaterEqual:
			return A >= B;
		default:
			return false;
		}
	}

	bool SupportsOrdering( EBlackboardKeyType Type )
	{
		return Type == EBlackboardKeyType::Int || Type == EBlackboardKeyType::Float || Type == EBlackboardKeyType::Enum;
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
			Text = FString::Printf( "event '%s'", *Condition.Event );
			break;
		case EStateConditionType::ClipFinished:
			Text = "clip finished";
			break;
		case EStateConditionType::ElapsedTime:
			Text = FString::Printf( "time >= %g", static_cast<float64>( Condition.Seconds ) );
			break;
		case EStateConditionType::Custom:
			Text = FString::Printf( "'%s'", *Condition.Call.Do );
			break;
		}
		return Condition.bNot ? "not " + Text : Text;
	}

	// 優先度の大きい順、同じなら番号の小さい順に並べる（挿入ソート。要素は少ない）
	void SortByPriority( TArray<int32>& List, const TArray<int32>& Priorities )
	{
		for ( int32 I = 1; I < List.Num(); ++I )
		{
			const int32 Value = List[I];
			int32 J = I - 1;
			while ( J >= 0 && Priorities[List[J]] < Priorities[Value] )
			{
				List[J + 1] = List[J];
				--J;
			}
			List[J + 1] = Value;
		}
	}
}

CStateMachine::~CStateMachine() = default;

TUniquePtr<CStateMachine> CStateMachine::Create( const TSharedPtr<const FStateMachineDefinition>& Definition, const CAIRegistry& Registry, const FBlackboardDefinition& Blackboard, TArray<FAIIssue>* Issues )
{
	if ( !Definition.IsValid() )
	{
		NAI::AddIssue( Issues, EAIIssueSeverity::Error, FString(), "There is no state machine definition." );
		return nullptr;
	}
	TUniquePtr<CStateMachine> Machine( new CStateMachine() );
	Machine->Definition = Definition;
	if ( !Machine->Compile( Registry, Blackboard, Issues ) )
	{
		return nullptr;
	}
	return Machine;
}

//=============================================================================
// コンパイル
//=============================================================================
TSharedPtr<FAIStateAction> CStateMachine::CompileAction( const FAICall& Call, const CAIRegistry& Registry, const FString& Where, TArray<FAIIssue>* Issues ) const
{
	const CAIRegistry::FStateActionEntry* Entry = Registry.FindStateAction( Call.Do );
	if ( Entry == nullptr )
	{
		NAI::AddIssue( Issues, EAIIssueSeverity::Warning, Where, FString::Printf( "The action '%s' is not registered. It was skipped.", *Call.Do ) );
		return nullptr;
	}
	TSharedPtr<FAIStateAction> Action( Entry->Create() );
	Action->Initialize( NAI::ResolveParams( Entry->Schema, Call.Params.is_null() ? nullptr : &Call.Params, Where, Issues ) );
	return Action;
}

void CStateMachine::CompileCondition( const FStateConditionDef& Def, FCompiledCondition& Out, const CAIRegistry& Registry, const FBlackboardDefinition& Blackboard, const FString& Where, TArray<FAIIssue>* Issues ) const
{
	Out.Type = Def.Type;
	Out.bNot = Def.bNot;
	switch ( Def.Type )
	{
	case EStateConditionType::BlackboardCompare:
	{
		const int32 KeyIndex = Blackboard.FindKeyIndex( Def.Key );
		if ( KeyIndex < 0 )
		{
			NAI::AddIssue( Issues, EAIIssueSeverity::Warning, Where, FString::Printf( "The blackboard key '%s' does not exist. The condition is always false.", *Def.Key ) );
			Out.bValid = false;
			return;
		}
		const FBlackboardKeyDefinition& Key = Blackboard.Keys[KeyIndex];
		Out.Key = FBlackboardKey( KeyIndex );
		Out.KeyType = Key.Type;
		Out.Op = Def.Op;
		if ( !FBlackboardDefinition::ValueFromJson( Key, Def.Value, Out.Compare ) )
		{
			NAI::AddIssue( Issues, EAIIssueSeverity::Warning, Where, FString::Printf( "The Value does not fit the %s key '%s'. The condition is always false.", NBlackboard::ToString( Key.Type ), *Def.Key ) );
			Out.bValid = false;
			return;
		}
		if ( Def.Op != EAICompareOp::Equal && Def.Op != EAICompareOp::NotEqual && !SupportsOrdering( Key.Type ) )
		{
			NAI::AddIssue( Issues, EAIIssueSeverity::Warning, Where, FString::Printf( "The %s key '%s' can only be compared with == and !=. The condition is always false.", NBlackboard::ToString( Key.Type ), *Def.Key ) );
			Out.bValid = false;
		}
		return;
	}
	case EStateConditionType::Event:
		Out.Event = Def.Event;
		return;
	case EStateConditionType::ClipFinished:
		return;
	case EStateConditionType::ElapsedTime:
		Out.Seconds = Def.Seconds;
		return;
	case EStateConditionType::Custom:
	{
		const CAIRegistry::FConditionEntry* Entry = Registry.FindCondition( Def.Call.Do );
		if ( Entry == nullptr )
		{
			NAI::AddIssue( Issues, EAIIssueSeverity::Warning, Where, FString::Printf( "The condition '%s' is not registered. The condition is always false.", *Def.Call.Do ) );
			Out.bValid = false;
			return;
		}
		Out.Custom = TSharedPtr<FAICondition>( Entry->Create() );
		Out.Custom->Initialize( NAI::ResolveParams( Entry->Schema, Def.Call.Params.is_null() ? nullptr : &Def.Call.Params, Where, Issues ) );
		return;
	}
	}
}

int32 CStateMachine::ResolveEntry( int32 Machine, const FString& EntryName, const FString& Where, TArray<FAIIssue>* Issues, bool& bFatal ) const
{
	const TArray<int32> Children = Definition->GetChildren( Machine );
	if ( Children.IsEmpty() )
	{
		NAI::AddIssue( Issues, EAIIssueSeverity::Error, Where, "The machine has no states." );
		bFatal = true;
		return -1;
	}
	if ( EntryName.IsEmpty() )
	{
		NAI::AddIssue( Issues, EAIIssueSeverity::Warning, Where, FString::Printf( "Entry is not set. The first state '%s' is used.", *Definition->States[Children[0]].Name ) );
		return Children[0];
	}
	const int32 Entry = Definition->FindByPath( Machine, EntryName );
	if ( Entry < 0 )
	{
		NAI::AddIssue( Issues, EAIIssueSeverity::Error, Where, FString::Printf( "Entry '%s' does not exist.", *EntryName ) );
		bFatal = true;
	}
	return Entry;
}

void CStateMachine::CompileTransitions( int32 Machine, const TArray<FStateTransitionDef>& Defs, bool bAnyTransitions, const CAIRegistry& Registry, const FBlackboardDefinition& Blackboard, const FString& Where, TArray<FAIIssue>* Issues )
{
	const char* Key = bAnyTransitions ? "AnyTransitions" : "Transitions";
	for ( int32 Index = 0; Index < Defs.Num(); ++Index )
	{
		const FStateTransitionDef& Def = Defs[Index];
		const FString TransitionWhere = FString::Printf( "%s/%s[%d]", *Where, Key, Index );

		FCompiledTransition Compiled;
		Compiled.Machine = Machine;
		Compiled.bAnyTransition = bAnyTransitions;
		if ( !bAnyTransitions && !Def.From.IsEmpty() )
		{
			Compiled.FromChild = Definition->FindChild( Machine, Def.From );
			if ( Compiled.FromChild < 0 )
			{
				NAI::AddIssue( Issues, EAIIssueSeverity::Warning, TransitionWhere, FString::Printf( "From '%s' does not exist in this machine. The transition was skipped.", *Def.From ) );
				continue;
			}
		}
		Compiled.Target = Definition->FindByPath( Machine, Def.To );
		if ( Compiled.Target < 0 )
		{
			NAI::AddIssue( Issues, EAIIssueSeverity::Warning, TransitionWhere, FString::Printf( "To '%s' does not exist in this machine. The transition was skipped.", *Def.To ) );
			continue;
		}

		Compiled.bAnyOf = Def.bAnyOf;
		Compiled.Priority = Def.Priority;
		Compiled.MinTime = Def.MinTime;
		FString Conditions;
		for ( int32 ConditionIndex = 0; ConditionIndex < Def.When.Num(); ++ConditionIndex )
		{
			FCompiledCondition Condition;
			CompileCondition( Def.When[ConditionIndex], Condition, Registry, Blackboard, FString::Printf( "%s/When[%d]", *TransitionWhere, ConditionIndex ), Issues );
			Compiled.Conditions.Add( Condition );
			Conditions += ( ConditionIndex == 0 ? "" : ( Def.bAnyOf ? " or " : " and " ) ) + DescribeCondition( Def.When[ConditionIndex] );
		}
		const FString Source = bAnyTransitions || Def.From.IsEmpty() ? FString( "(any)" ) : Def.From;
		Compiled.Description = Conditions.IsEmpty() ? FString::Printf( "%s -> %s", *Source, *Def.To ) : FString::Printf( "%s -> %s when %s", *Source, *Def.To, *Conditions );

		const int32 Id = CompiledTransitions.Add( Compiled );
		if ( Machine < 0 )
		{
			( bAnyTransitions ? RootAnyTransitions : RootTransitions ).Add( Id );
		}
		else
		{
			( bAnyTransitions ? States[Machine].AnyTransitions : States[Machine].Transitions ).Add( Id );
		}
	}
}

void CStateMachine::MarkReachable( TArray<bool>& Reachable, int32 State ) const
{
	if ( State < 0 || Reachable[State] )
	{
		return;
	}
	Reachable[State] = true;
	// 入るときは、親も通る
	for ( int32 Parent = States[State].Parent; Parent >= 0 && !Reachable[Parent]; Parent = States[Parent].Parent )
	{
		Reachable[Parent] = true;
	}
	// マシンなら、入口の子まで入る
	if ( Definition->States[State].bMachine )
	{
		MarkReachable( Reachable, States[State].Entry );
	}
}

bool CStateMachine::Compile( const CAIRegistry& Registry, const FBlackboardDefinition& Blackboard, TArray<FAIIssue>* Issues )
{
	const FStateMachineDefinition& Def = *Definition;
	if ( Def.States.IsEmpty() )
	{
		NAI::AddIssue( Issues, EAIIssueSeverity::Error, FString(), "The state machine has no states." );
		return false;
	}

	States.SetNum( Def.States.Num() );
	bool bFatal = false;
	RootEntry = ResolveEntry( -1, Def.Entry, "Entry", Issues, bFatal );

	for ( int32 Index = 0; Index < Def.States.Num(); ++Index )
	{
		const FStateDef& State = Def.States[Index];
		FCompiledState& Compiled = States[Index];
		const FString Where = "States/" + Def.GetPath( Index );
		Compiled.Parent = State.Parent;
		Compiled.Clip = State.Clip;
		if ( State.bMachine )
		{
			Compiled.Entry = ResolveEntry( Index, State.Entry, Where, Issues, bFatal );
		}
		else if ( !State.Transitions.IsEmpty() || !State.AnyTransitions.IsEmpty() )
		{
			NAI::AddIssue( Issues, EAIIssueSeverity::Warning, Where, "The state has transitions but no child states. The transitions are ignored." );
		}

		for ( int32 CallIndex = 0; CallIndex < State.OnEnter.Num(); ++CallIndex )
		{
			if ( TSharedPtr<FAIStateAction> Action = CompileAction( State.OnEnter[CallIndex], Registry, FString::Printf( "%s/OnEnter[%d]", *Where, CallIndex ), Issues ) )
			{
				Compiled.OnEnter.Add( Action );
			}
		}
		for ( int32 CallIndex = 0; CallIndex < State.OnTick.Num(); ++CallIndex )
		{
			if ( TSharedPtr<FAIStateAction> Action = CompileAction( State.OnTick[CallIndex], Registry, FString::Printf( "%s/OnTick[%d]", *Where, CallIndex ), Issues ) )
			{
				Compiled.OnTick.Add( Action );
			}
		}
		for ( int32 CallIndex = 0; CallIndex < State.OnExit.Num(); ++CallIndex )
		{
			if ( TSharedPtr<FAIStateAction> Action = CompileAction( State.OnExit[CallIndex], Registry, FString::Printf( "%s/OnExit[%d]", *Where, CallIndex ), Issues ) )
			{
				Compiled.OnExit.Add( Action );
			}
		}
		for ( int32 EventIndex = 0; EventIndex < State.OnClipEvent.Num(); ++EventIndex )
		{
			if ( TSharedPtr<FAIStateAction> Action = CompileAction( State.OnClipEvent[EventIndex].Call, Registry, FString::Printf( "%s/OnClipEvent[%d]", *Where, EventIndex ), Issues ) )
			{
				FCompiledEventAction EventAction;
				EventAction.Event = State.OnClipEvent[EventIndex].Event;
				EventAction.Action = Action;
				Compiled.OnClipEvent.Add( EventAction );
			}
		}
	}
	if ( bFatal )
	{
		return false;
	}

	// 遷移（マシンごと）
	CompileTransitions( -1, Def.Transitions, false, Registry, Blackboard, FString(), Issues );
	CompileTransitions( -1, Def.AnyTransitions, true, Registry, Blackboard, FString(), Issues );
	for ( int32 Index = 0; Index < Def.States.Num(); ++Index )
	{
		if ( Def.States[Index].bMachine )
		{
			const FString Where = "States/" + Def.GetPath( Index );
			CompileTransitions( Index, Def.States[Index].Transitions, false, Registry, Blackboard, Where, Issues );
			CompileTransitions( Index, Def.States[Index].AnyTransitions, true, Registry, Blackboard, Where, Issues );
		}
	}

	// 優先度の順に並べる
	TArray<int32> Priorities;
	for ( const FCompiledTransition& Transition : CompiledTransitions )
	{
		Priorities.Add( Transition.Priority );
	}
	SortByPriority( RootAnyTransitions, Priorities );
	SortByPriority( RootTransitions, Priorities );
	for ( FCompiledState& State : States )
	{
		SortByPriority( State.AnyTransitions, Priorities );
		SortByPriority( State.Transitions, Priorities );
	}

	// 入れない（どこからも入口にならず、遷移の先にもならない）ステートの警告
	TArray<bool> Reachable;
	Reachable.AddZeroed( States.Num() );
	MarkReachable( Reachable, RootEntry );
	for ( const FCompiledTransition& Transition : CompiledTransitions )
	{
		MarkReachable( Reachable, Transition.Target );
	}
	for ( int32 Index = 0; Index < States.Num(); ++Index )
	{
		if ( !Reachable[Index] )
		{
			NAI::AddIssue( Issues, EAIIssueSeverity::Warning, "States/" + Def.GetPath( Index ), "The state can never be entered (it is not an entry and no transition leads to it)." );
		}
	}
	return true;
}

//=============================================================================
// 実行
//=============================================================================
bool CStateMachine::HasPendingEvent( const FString& Name ) const
{
	return PendingEvents.Contains( Name );
}

void CStateMachine::ConsumeEvent( const FString& Name )
{
	const int32 Index = PendingEvents.Find( Name );
	if ( Index != INDEX_NONE )
	{
		PendingEvents.RemoveAt( Index );
	}
}

void CStateMachine::SendEvent( const FString& Name )
{
	if ( Name.IsEmpty() )
	{
		return;
	}
	if ( PendingEvents.Num() >= MaxPendingEvents )
	{
		if ( !bWarnedEventOverflow )
		{
			bWarnedEventOverflow = true;
			LOG( LogAI, Warning, "StateMachine: too many pending events (%d). The event '%s' was dropped.", MaxPendingEvents, *Name );
		}
		return;
	}
	PendingEvents.Add( Name );
}

void CStateMachine::AddHistory( EStateTransitionCause Cause, int32 From, int32 To, const FString& Description, const FAIContext& Context )
{
	FStateHistoryEntry& Entry = History[HistoryHead];
	Entry.Time = Context.Time;
	Entry.From = From;
	Entry.To = To;
	Entry.Cause = Cause;
	Entry.Description = Description;
	HistoryHead = ( HistoryHead + 1 ) % MaxHistory;
	if ( HistoryCount < MaxHistory )
	{
		++HistoryCount;
	}
}

void CStateMachine::GetHistory( TArray<FStateHistoryEntry>& OutHistory ) const
{
	OutHistory.Empty();
	const int32 Start = ( HistoryHead - HistoryCount + MaxHistory ) % MaxHistory;
	for ( int32 Index = 0; Index < HistoryCount; ++Index )
	{
		OutHistory.Add( History[( Start + Index ) % MaxHistory] );
	}
}

void CStateMachine::EnterState( int32 State, FAIContext& Context, bool bDescend )
{
	ActivePath.Add( State );
	ActiveTimes.Add( 0.0f );
	const FCompiledState& Compiled = States[State];
	for ( const TSharedPtr<FAIStateAction>& Action : Compiled.OnEnter )
	{
		Action->OnEnter( Context );
	}
	if ( !Compiled.Clip.IsEmpty() && Context.Animation != nullptr )
	{
		Context.Animation->PlayClip( Compiled.Clip, true );
	}
	if ( bDescend && Definition->States[State].bMachine && Compiled.Entry >= 0 )
	{
		EnterState( Compiled.Entry, Context, true );
	}
}

void CStateMachine::ExitDownTo( int32 Level, FAIContext& Context )
{
	for ( int32 Index = ActivePath.Num() - 1; Index >= Level; --Index )
	{
		for ( const TSharedPtr<FAIStateAction>& Action : States[ActivePath[Index]].OnExit )
		{
			Action->OnExit( Context );
		}
	}
	ActivePath.SetNum( Level );
	ActiveTimes.SetNum( Level );
}

void CStateMachine::Start( FAIContext& Context )
{
	if ( bRunning )
	{
		return;
	}
	bRunning = true;
	ActivePath.Empty();
	ActiveTimes.Empty();
	PendingEvents.Empty();
	HistoryCount = 0;
	HistoryHead = 0;
	bHitTransitionLimit = false;
	if ( RootEntry >= 0 )
	{
		EnterState( RootEntry, Context, true );
		AddHistory( EStateTransitionCause::Initial, -1, ActivePath.Last(), "(start)", Context );
	}
}

void CStateMachine::Stop( FAIContext& Context )
{
	if ( !bRunning )
	{
		return;
	}
	ExitDownTo( 0, Context );
	PendingEvents.Empty();
	bRunning = false;
}

bool CStateMachine::EvaluateCondition( const FCompiledCondition& Condition, int32 Level, FAIContext& Context ) const
{
	if ( !Condition.bValid )
	{
		return false;
	}
	bool bResult = false;
	switch ( Condition.Type )
	{
	case EStateConditionType::BlackboardCompare:
	{
		FBlackboardValue Current;
		bResult = Context.Blackboard != nullptr && Context.Blackboard->GetValue( Condition.Key, Current ) && CompareValues( Condition.KeyType, Current, Condition.Op, Condition.Compare );
		break;
	}
	case EStateConditionType::Event:
		bResult = HasPendingEvent( Condition.Event );
		break;
	case EStateConditionType::ClipFinished:
		bResult = Context.Animation != nullptr && Context.Animation->IsClipFinished();
		break;
	case EStateConditionType::ElapsedTime:
		bResult = Level >= 0 && Level < ActiveTimes.Num() && ActiveTimes[Level] >= Condition.Seconds;
		break;
	case EStateConditionType::Custom:
		bResult = Condition.Custom.IsValid() && Condition.Custom->Evaluate( Context );
		break;
	}
	return Condition.bNot ? !bResult : bResult;
}

bool CStateMachine::EvaluateTransition( const FCompiledTransition& Transition, int32 Level, FAIContext& Context ) const
{
	if ( Transition.MinTime > 0.0f && ( Level < 0 || Level >= ActiveTimes.Num() || ActiveTimes[Level] < Transition.MinTime ) )
	{
		return false;
	}
	if ( Transition.Conditions.IsEmpty() )
	{
		return true;
	}
	if ( Transition.bAnyOf )
	{
		for ( const FCompiledCondition& Condition : Transition.Conditions )
		{
			if ( EvaluateCondition( Condition, Level, Context ) )
			{
				return true;
			}
		}
		return false;
	}
	for ( const FCompiledCondition& Condition : Transition.Conditions )
	{
		if ( !EvaluateCondition( Condition, Level, Context ) )
		{
			return false;
		}
	}
	return true;
}

bool CStateMachine::EvaluateList( const TArray<int32>& List, int32 ActiveChild, int32 Level, FAIContext& Context, int32& OutTransition )
{
	for ( const int32 Id : List )
	{
		const FCompiledTransition& Transition = CompiledTransitions[Id];
		if ( Transition.FromChild >= 0 && Transition.FromChild != ActiveChild )
		{
			continue;
		}
		if ( FiredThisTick.Contains( Id ) )
		{
			continue;
		}
		if ( EvaluateTransition( Transition, Level, Context ) )
		{
			OutTransition = Id;
			return true;
		}
	}
	return false;
}

bool CStateMachine::FindTransition( FAIContext& Context, int32& OutTransition )
{
	if ( ActivePath.IsEmpty() )
	{
		return false;
	}
	// 最上位の Any State 遷移
	if ( EvaluateList( RootAnyTransitions, ActivePath[0], 0, Context, OutTransition ) )
	{
		return true;
	}
	// 現在のステートの遷移（葉から親へ）
	for ( int32 Level = ActivePath.Num() - 1; Level >= 0; --Level )
	{
		const int32 State = ActivePath[Level];
		const int32 Machine = States[State].Parent;
		if ( Machine >= 0 && EvaluateList( States[Machine].AnyTransitions, State, Level, Context, OutTransition ) )
		{
			return true;
		}
		const TArray<int32>& List = Machine >= 0 ? States[Machine].Transitions : RootTransitions;
		if ( EvaluateList( List, State, Level, Context, OutTransition ) )
		{
			return true;
		}
	}
	return false;
}

void CStateMachine::PerformTransition( int32 TransitionId, FAIContext& Context )
{
	const FCompiledTransition& Transition = CompiledTransitions[TransitionId];

	// 持ち主のマシンの子で、今いるステートの位置
	int32 Level = -1;
	for ( int32 Index = 0; Index < ActivePath.Num(); ++Index )
	{
		if ( States[ActivePath[Index]].Parent == Transition.Machine )
		{
			Level = Index;
			break;
		}
	}
	if ( Level < 0 )
	{
		return;
	}

	// 条件に使ったイベントは消費する（同じイベントで、続けてもう 1 度遷移しない）
	for ( const FCompiledCondition& Condition : Transition.Conditions )
	{
		if ( Condition.Type == EStateConditionType::Event && !Condition.bNot )
		{
			ConsumeEvent( Condition.Event );
		}
	}
	FiredThisTick.Add( TransitionId );

	const int32 FromLeaf = ActivePath.Last();
	ExitDownTo( Level, Context );

	// 持ち主のマシンの直下から、先へ向かって入る（先がマシンなら、入口の子まで）
	TArray<int32> Chain;
	for ( int32 State = Transition.Target; State >= 0 && State != Transition.Machine; State = States[State].Parent )
	{
		Chain.Add( State );
	}
	for ( int32 Index = Chain.Num() - 1; Index >= 0; --Index )
	{
		EnterState( Chain[Index], Context, Index == 0 );
	}
	AddHistory( Transition.bAnyTransition ? EStateTransitionCause::AnyTransition : EStateTransitionCause::Transition, FromLeaf, ActivePath.Last(), Transition.Description, Context );
}

void CStateMachine::Tick( FAIContext& Context, float32 DeltaTime )
{
	if ( !bRunning || ActivePath.IsEmpty() )
	{
		return;
	}
	for ( float32& Time : ActiveTimes )
	{
		Time += DeltaTime;
	}

	// 遷移（連鎖には上限がある）
	FiredThisTick.Empty();
	int32 NumTransitions = 0;
	int32 TransitionId = -1;
	while ( FindTransition( Context, TransitionId ) )
	{
		if ( NumTransitions >= MaxTransitionsPerTick )
		{
			if ( !bHitTransitionLimit )
			{
				bHitTransitionLimit = true;
				LOG( LogAI, Warning, "StateMachine: more than %d transitions in one tick (now in '%s', next '%s'). The rest was postponed to the next tick.", MaxTransitionsPerTick, *DescribeActiveState(), *CompiledTransitions[TransitionId].Description );
			}
			break;
		}
		PerformTransition( TransitionId, Context );
		++NumTransitions;
	}

	// いるステートの処理（最上位から葉へ）と、アニメーションの出来事への反応
	for ( const int32 State : ActivePath )
	{
		for ( const TSharedPtr<FAIStateAction>& Action : States[State].OnTick )
		{
			Action->OnTick( Context, DeltaTime );
		}
	}
	if ( Context.Animation != nullptr )
	{
		for ( int32 Index = ActivePath.Num() - 1; Index >= 0; --Index )
		{
			for ( const FCompiledEventAction& EventAction : States[ActivePath[Index]].OnClipEvent )
			{
				if ( Context.Animation->ConsumeClipEvent( EventAction.Event ) )
				{
					EventAction.Action->OnEvent( Context, EventAction.Event );
				}
			}
		}
	}
	PendingEvents.Empty();
}

//=============================================================================
// 状態を調べる
//=============================================================================
FString CStateMachine::DescribeActiveState() const
{
	return ActivePath.IsEmpty() ? FString() : Definition->GetPath( ActivePath.Last() );
}

bool CStateMachine::IsInState( const FString& Path ) const
{
	const int32 State = Definition->FindByPath( -1, Path );
	return State >= 0 && ActivePath.Contains( State );
}

float32 CStateMachine::GetLeafStateTime() const
{
	return ActiveTimes.IsEmpty() ? 0.0f : ActiveTimes.Last();
}

float32 CStateMachine::GetStateTime( int32 Depth ) const
{
	return Depth >= 0 && Depth < ActiveTimes.Num() ? ActiveTimes[Depth] : 0.0f;
}
