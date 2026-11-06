// SPDX-License-Identifier: MIT
#include "StateMachineDefinition.h"

#include "Library/Json/JsonHelpers.h"
#include "Library/Math/MathUtils.h"

namespace
{
	constexpr int32 CurrentVersion = 1;
	// 入れ子の深さの上限（壊れた・悪意のある JSON で、再帰が深くなりすぎないように）
	constexpr int32 MaxDepth = 16;

	//-------------------------------------------------------------------------
	// 読む
	//-------------------------------------------------------------------------
	void ReadCalls( const NlohmannJson& Parent, const char* Key, TArray<FAICall>& Out, const FString& Where, TArray<FAIIssue>* Issues )
	{
		const auto It = Parent.find( Key );
		if ( It == Parent.end() || !It->is_array() )
		{
			return;
		}
		int32 Index = 0;
		for ( const NlohmannJson& Item : *It )
		{
			const FString ItemWhere = FString::Printf( "%s/%s[%d]", *Where, Key, Index++ );
			FAICall Call;
			Call.Do = NJson::GetOr<FString>( Item, "Do", FString() );
			if ( Call.Do.IsEmpty() )
			{
				NAI::AddIssue( Issues, EAIIssueSeverity::Warning, ItemWhere, "The call has no Do. It was skipped." );
				continue;
			}
			const auto ParamsIt = Item.find( "Params" );
			if ( ParamsIt != Item.end() )
			{
				Call.Params = *ParamsIt;
			}
			Out.Add( Call );
		}
	}

	bool ReadCondition( const NlohmannJson& Item, FStateConditionDef& Out, const FString& Where, TArray<FAIIssue>* Issues )
	{
		if ( !Item.is_object() )
		{
			NAI::AddIssue( Issues, EAIIssueSeverity::Warning, Where, "The condition is not an object. It was skipped." );
			return false;
		}
		Out.bNot = NJson::GetOr<bool>( Item, "Not", false );

		if ( Item.contains( "Key" ) )
		{
			Out.Type = EStateConditionType::BlackboardCompare;
			Out.Key = NJson::GetOr<FString>( Item, "Key", FString() );
			if ( Out.Key.IsEmpty() )
			{
				NAI::AddIssue( Issues, EAIIssueSeverity::Warning, Where, "The condition has an empty Key. It was skipped." );
				return false;
			}
			if ( !NAI::TryParseCompareOp( NJson::GetOr<FString>( Item, "Op", FString( "==" ) ), Out.Op ) )
			{
				NAI::AddIssue( Issues, EAIIssueSeverity::Warning, Where, "The condition has an unknown Op. It was skipped." );
				return false;
			}
			const auto ValueIt = Item.find( "Value" );
			if ( ValueIt == Item.end() )
			{
				NAI::AddIssue( Issues, EAIIssueSeverity::Warning, Where, "The condition has no Value. It was skipped." );
				return false;
			}
			Out.Value = *ValueIt;
			return true;
		}
		if ( Item.contains( "Event" ) )
		{
			Out.Type = EStateConditionType::Event;
			Out.Event = NJson::GetOr<FString>( Item, "Event", FString() );
			if ( Out.Event.IsEmpty() )
			{
				NAI::AddIssue( Issues, EAIIssueSeverity::Warning, Where, "The condition has an empty Event. It was skipped." );
				return false;
			}
			return true;
		}
		if ( Item.contains( "ClipFinished" ) )
		{
			Out.Type = EStateConditionType::ClipFinished;
			// { "ClipFinished": false } は、「終わっていない」
			if ( !NJson::GetOr<bool>( Item, "ClipFinished", true ) )
			{
				Out.bNot = !Out.bNot;
			}
			return true;
		}
		if ( Item.contains( "Time" ) )
		{
			Out.Type = EStateConditionType::ElapsedTime;
			Out.Seconds = NJson::GetOr<float32>( Item, "Time", 0.0f );
			if ( !NMath::IsFinite( Out.Seconds ) || Out.Seconds < 0.0f )
			{
				NAI::AddIssue( Issues, EAIIssueSeverity::Warning, Where, "The Time of the condition is not a non-negative number. 0 is used." );
				Out.Seconds = 0.0f;
			}
			return true;
		}
		if ( Item.contains( "Do" ) )
		{
			Out.Type = EStateConditionType::Custom;
			Out.Call.Do = NJson::GetOr<FString>( Item, "Do", FString() );
			if ( Out.Call.Do.IsEmpty() )
			{
				NAI::AddIssue( Issues, EAIIssueSeverity::Warning, Where, "The condition has an empty Do. It was skipped." );
				return false;
			}
			const auto ParamsIt = Item.find( "Params" );
			if ( ParamsIt != Item.end() )
			{
				Out.Call.Params = *ParamsIt;
			}
			return true;
		}
		NAI::AddIssue( Issues, EAIIssueSeverity::Warning, Where, "The condition has none of Key / Event / ClipFinished / Time / Do. It was skipped." );
		return false;
	}

	void ReadTransitions( const NlohmannJson& Parent, const char* Key, TArray<FStateTransitionDef>& Out, const FString& Where, TArray<FAIIssue>* Issues )
	{
		const auto It = Parent.find( Key );
		if ( It == Parent.end() || !It->is_array() )
		{
			return;
		}
		int32 Index = 0;
		for ( const NlohmannJson& Item : *It )
		{
			const FString ItemWhere = FString::Printf( "%s/%s[%d]", *Where, Key, Index++ );
			if ( !Item.is_object() )
			{
				NAI::AddIssue( Issues, EAIIssueSeverity::Warning, ItemWhere, "The transition is not an object. It was skipped." );
				continue;
			}
			FStateTransitionDef Transition;
			Transition.To = NJson::GetOr<FString>( Item, "To", FString() );
			if ( Transition.To.IsEmpty() )
			{
				NAI::AddIssue( Issues, EAIIssueSeverity::Warning, ItemWhere, "The transition has no To. It was skipped." );
				continue;
			}
			Transition.From = NJson::GetOr<FString>( Item, "From", FString() );
			Transition.Priority = NJson::GetOr<int32>( Item, "Priority", 0 );
			Transition.MinTime = NJson::GetOr<float32>( Item, "MinTime", 0.0f );
			if ( !NMath::IsFinite( Transition.MinTime ) || Transition.MinTime < 0.0f )
			{
				NAI::AddIssue( Issues, EAIIssueSeverity::Warning, ItemWhere, "The MinTime is not a non-negative number. 0 is used." );
				Transition.MinTime = 0.0f;
			}
			Transition.bAnyOf = NJson::GetOr<FString>( Item, "Mode", FString( "All" ) ) == "Any";

			const FString OnEvent = NJson::GetOr<FString>( Item, "On", FString() );
			if ( !OnEvent.IsEmpty() )
			{
				FStateConditionDef Condition;
				Condition.Type = EStateConditionType::Event;
				Condition.Event = OnEvent;
				Transition.When.Add( Condition );
			}
			const auto WhenIt = Item.find( "When" );
			if ( WhenIt != Item.end() && WhenIt->is_array() )
			{
				int32 ConditionIndex = 0;
				for ( const NlohmannJson& ConditionJson : *WhenIt )
				{
					FStateConditionDef Condition;
					if ( ReadCondition( ConditionJson, Condition, FString::Printf( "%s/When[%d]", *ItemWhere, ConditionIndex++ ), Issues ) )
					{
						Transition.When.Add( Condition );
					}
				}
			}
			Out.Add( Transition );
		}
	}

	void ReadStates( const NlohmannJson& StatesJson, int32 Parent, FStateMachineDefinition& Definition, const FString& Where, int32 Depth, TArray<FAIIssue>* Issues )
	{
		if ( !StatesJson.is_array() )
		{
			return;
		}
		if ( Depth > MaxDepth )
		{
			NAI::AddIssue( Issues, EAIIssueSeverity::Warning, Where, "The states are nested too deeply. The rest was skipped." );
			return;
		}

		int32 Index = 0;
		for ( const NlohmannJson& Item : StatesJson )
		{
			const FString ItemWhere = FString::Printf( "%s[%d]", *Where, Index++ );
			if ( !Item.is_object() )
			{
				NAI::AddIssue( Issues, EAIIssueSeverity::Warning, ItemWhere, "The state is not an object. It was skipped." );
				continue;
			}
			FStateDef State;
			State.Name = NJson::GetOr<FString>( Item, "Name", FString() );
			if ( State.Name.IsEmpty() || State.Name.Find( "/" ) != INDEX_NONE )
			{
				NAI::AddIssue( Issues, EAIIssueSeverity::Warning, ItemWhere, "The state has no Name (or its Name contains '/'). It was skipped." );
				continue;
			}
			if ( Definition.FindChild( Parent, State.Name ) >= 0 )
			{
				NAI::AddIssue( Issues, EAIIssueSeverity::Warning, ItemWhere, FString::Printf( "The state '%s' is defined twice in the same machine. The later one was skipped.", *State.Name ) );
				continue;
			}

			State.Parent = Parent;
			const FString Type = NJson::GetOr<FString>( Item, "Type", FString() );
			State.bMachine = Type == "Machine" || ( Item.contains( "States" ) && Item["States"].is_array() );
			State.Clip = NJson::GetOr<FString>( Item, "Clip", FString() );
			State.Entry = NJson::GetOr<FString>( Item, "Entry", FString() );
			const FString StateWhere = FString::Printf( "%s/%s", *Where, *State.Name );
			ReadCalls( Item, "OnEnter", State.OnEnter, StateWhere, Issues );
			ReadCalls( Item, "OnTick", State.OnTick, StateWhere, Issues );
			ReadCalls( Item, "OnExit", State.OnExit, StateWhere, Issues );
			const auto ClipEventIt = Item.find( "OnClipEvent" );
			if ( ClipEventIt != Item.end() && ClipEventIt->is_array() )
			{
				int32 EventIndex = 0;
				for ( const NlohmannJson& EventJson : *ClipEventIt )
				{
					const FString EventWhere = FString::Printf( "%s/OnClipEvent[%d]", *StateWhere, EventIndex++ );
					FStateClipEventDef ClipEvent;
					ClipEvent.Event = NJson::GetOr<FString>( EventJson, "Event", FString() );
					ClipEvent.Call.Do = NJson::GetOr<FString>( EventJson, "Do", FString() );
					if ( ClipEvent.Event.IsEmpty() || ClipEvent.Call.Do.IsEmpty() )
					{
						NAI::AddIssue( Issues, EAIIssueSeverity::Warning, EventWhere, "The clip event needs both Event and Do. It was skipped." );
						continue;
					}
					const auto ParamsIt = EventJson.find( "Params" );
					if ( ParamsIt != EventJson.end() )
					{
						ClipEvent.Call.Params = *ParamsIt;
					}
					State.OnClipEvent.Add( ClipEvent );
				}
			}
			ReadTransitions( Item, "Transitions", State.Transitions, StateWhere, Issues );
			ReadTransitions( Item, "AnyTransitions", State.AnyTransitions, StateWhere, Issues );
			const auto EditorIt = Item.find( "Editor" );
			if ( EditorIt != Item.end() )
			{
				State.Editor = *EditorIt;
			}

			const int32 StateIndex = Definition.States.Add( State );
			const auto ChildrenIt = Item.find( "States" );
			if ( State.bMachine && ChildrenIt != Item.end() )
			{
				ReadStates( *ChildrenIt, StateIndex, Definition, StateWhere + "/States", Depth + 1, Issues );
			}
		}
	}

	//-------------------------------------------------------------------------
	// 書く
	//-------------------------------------------------------------------------
	void WriteCalls( NlohmannJson& Out, const char* Key, const TArray<FAICall>& Calls )
	{
		if ( Calls.IsEmpty() )
		{
			return;
		}
		NlohmannJson Array = NlohmannJson::array();
		for ( const FAICall& Call : Calls )
		{
			NlohmannJson Item = NlohmannJson::object();
			Item["Do"] = Call.Do;
			if ( !Call.Params.is_null() && !( Call.Params.is_object() && Call.Params.empty() ) )
			{
				Item["Params"] = Call.Params;
			}
			Array.push_back( Item );
		}
		Out[Key] = Array;
	}

	NlohmannJson ConditionToJson( const FStateConditionDef& Condition )
	{
		NlohmannJson Item = NlohmannJson::object();
		switch ( Condition.Type )
		{
		case EStateConditionType::BlackboardCompare:
			Item["Key"] = Condition.Key;
			Item["Op"] = NAI::ToString( Condition.Op );
			Item["Value"] = Condition.Value;
			break;
		case EStateConditionType::Event:
			Item["Event"] = Condition.Event;
			break;
		case EStateConditionType::ClipFinished:
			Item["ClipFinished"] = true;
			break;
		case EStateConditionType::ElapsedTime:
			Item["Time"] = NJson::Round4( Condition.Seconds );
			break;
		case EStateConditionType::Custom:
			Item["Do"] = Condition.Call.Do;
			if ( !Condition.Call.Params.is_null() && !( Condition.Call.Params.is_object() && Condition.Call.Params.empty() ) )
			{
				Item["Params"] = Condition.Call.Params;
			}
			break;
		}
		if ( Condition.bNot )
		{
			Item["Not"] = true;
		}
		return Item;
	}

	void WriteTransitions( NlohmannJson& Out, const char* Key, const TArray<FStateTransitionDef>& Transitions, bool bWriteFrom )
	{
		if ( Transitions.IsEmpty() )
		{
			return;
		}
		NlohmannJson Array = NlohmannJson::array();
		for ( const FStateTransitionDef& Transition : Transitions )
		{
			NlohmannJson Item = NlohmannJson::object();
			if ( bWriteFrom && !Transition.From.IsEmpty() )
			{
				Item["From"] = Transition.From;
			}
			Item["To"] = Transition.To;
			if ( !Transition.When.IsEmpty() )
			{
				NlohmannJson When = NlohmannJson::array();
				for ( const FStateConditionDef& Condition : Transition.When )
				{
					When.push_back( ConditionToJson( Condition ) );
				}
				Item["When"] = When;
			}
			if ( Transition.bAnyOf )
			{
				Item["Mode"] = "Any";
			}
			if ( Transition.Priority != 0 )
			{
				Item["Priority"] = Transition.Priority;
			}
			if ( Transition.MinTime > 0.0f )
			{
				Item["MinTime"] = NJson::Round4( Transition.MinTime );
			}
			Array.push_back( Item );
		}
		Out[Key] = Array;
	}

	NlohmannJson StatesToJson( const FStateMachineDefinition& Definition, int32 Parent )
	{
		NlohmannJson Array = NlohmannJson::array();
		for ( int32 Index = 0; Index < Definition.States.Num(); ++Index )
		{
			const FStateDef& State = Definition.States[Index];
			if ( State.Parent != Parent )
			{
				continue;
			}
			NlohmannJson Item = NlohmannJson::object();
			Item["Name"] = State.Name;
			if ( !State.Clip.IsEmpty() )
			{
				Item["Clip"] = State.Clip;
			}
			if ( State.bMachine )
			{
				if ( !State.Entry.IsEmpty() )
				{
					Item["Entry"] = State.Entry;
				}
				Item["States"] = StatesToJson( Definition, Index );
				WriteTransitions( Item, "Transitions", State.Transitions, true );
				WriteTransitions( Item, "AnyTransitions", State.AnyTransitions, false );
			}
			WriteCalls( Item, "OnEnter", State.OnEnter );
			WriteCalls( Item, "OnTick", State.OnTick );
			WriteCalls( Item, "OnExit", State.OnExit );
			if ( !State.OnClipEvent.IsEmpty() )
			{
				NlohmannJson Events = NlohmannJson::array();
				for ( const FStateClipEventDef& ClipEvent : State.OnClipEvent )
				{
					NlohmannJson Event = NlohmannJson::object();
					Event["Event"] = ClipEvent.Event;
					Event["Do"] = ClipEvent.Call.Do;
					if ( !ClipEvent.Call.Params.is_null() && !( ClipEvent.Call.Params.is_object() && ClipEvent.Call.Params.empty() ) )
					{
						Event["Params"] = ClipEvent.Call.Params;
					}
					Events.push_back( Event );
				}
				Item["OnClipEvent"] = Events;
			}
			if ( !State.Editor.is_null() )
			{
				Item["Editor"] = State.Editor;
			}
			Array.push_back( Item );
		}
		return Array;
	}
}

//=============================================================================
// 演算子
//=============================================================================
const char* NAI::ToString( EAICompareOp Op )
{
	switch ( Op )
	{
	case EAICompareOp::Equal:
		return "==";
	case EAICompareOp::NotEqual:
		return "!=";
	case EAICompareOp::Less:
		return "<";
	case EAICompareOp::LessEqual:
		return "<=";
	case EAICompareOp::Greater:
		return ">";
	case EAICompareOp::GreaterEqual:
		return ">=";
	}
	return "==";
}

bool NAI::TryParseCompareOp( const FString& Text, EAICompareOp& OutOp )
{
	static const EAICompareOp Ops[] =
	{
		EAICompareOp::Equal,
		EAICompareOp::NotEqual,
		EAICompareOp::Less,
		EAICompareOp::LessEqual,
		EAICompareOp::Greater,
		EAICompareOp::GreaterEqual,
	};
	for ( const EAICompareOp Op : Ops )
	{
		if ( Text == ToString( Op ) )
		{
			OutOp = Op;
			return true;
		}
	}
	return false;
}

//=============================================================================
// FStateMachineDefinition
//=============================================================================
int32 FStateMachineDefinition::FindChild( int32 Parent, const FString& Name ) const
{
	for ( int32 Index = 0; Index < States.Num(); ++Index )
	{
		if ( States[Index].Parent == Parent && States[Index].Name == Name )
		{
			return Index;
		}
	}
	return -1;
}

int32 FStateMachineDefinition::FindByPath( int32 Parent, const FString& Path ) const
{
	int32 Current = Parent;
	FString Remaining = Path;
	while ( !Remaining.IsEmpty() )
	{
		const int32 Slash = Remaining.Find( "/" );
		const FString Part = Slash == INDEX_NONE ? Remaining : Remaining.Left( Slash );
		Remaining = Slash == INDEX_NONE ? FString() : Remaining.Mid( Slash + 1 );
		Current = FindChild( Current, Part );
		if ( Current < 0 )
		{
			return -1;
		}
	}
	return Current == Parent ? -1 : Current;
}

TArray<int32> FStateMachineDefinition::GetChildren( int32 Parent ) const
{
	TArray<int32> Children;
	for ( int32 Index = 0; Index < States.Num(); ++Index )
	{
		if ( States[Index].Parent == Parent )
		{
			Children.Add( Index );
		}
	}
	return Children;
}

FString FStateMachineDefinition::GetPath( int32 Index ) const
{
	if ( Index < 0 || Index >= States.Num() )
	{
		return FString();
	}
	FString Path = States[Index].Name;
	// 親を辿る（壊れた定義で循環しても止まるよう、回数を限る）
	int32 Parent = States[Index].Parent;
	for ( int32 Depth = 0; Parent >= 0 && Parent < States.Num() && Depth < States.Num(); ++Depth )
	{
		Path = States[Parent].Name + "/" + Path;
		Parent = States[Parent].Parent;
	}
	return Path;
}

NlohmannJson FStateMachineDefinition::ToJson() const
{
	NlohmannJson Json = NlohmannJson::object();
	Json["Version"] = CurrentVersion;
	if ( !Blackboard.IsEmpty() )
	{
		Json["Blackboard"] = Blackboard;
	}
	if ( !Entry.IsEmpty() )
	{
		Json["Entry"] = Entry;
	}
	Json["States"] = StatesToJson( *this, -1 );
	WriteTransitions( Json, "Transitions", Transitions, true );
	WriteTransitions( Json, "AnyTransitions", AnyTransitions, false );
	if ( !Editor.is_null() )
	{
		Json["Editor"] = Editor;
	}
	return Json;
}

bool FStateMachineDefinition::FromJson( const NlohmannJson& Json, FStateMachineDefinition& OutDefinition, TArray<FAIIssue>* Issues )
{
	if ( !Json.is_object() )
	{
		return false;
	}

	FStateMachineDefinition Definition;
	Definition.Blackboard = NJson::GetOr<FString>( Json, "Blackboard", FString() );
	Definition.Entry = NJson::GetOr<FString>( Json, "Entry", FString() );
	const auto StatesIt = Json.find( "States" );
	if ( StatesIt != Json.end() )
	{
		ReadStates( *StatesIt, -1, Definition, "States", 0, Issues );
	}
	ReadTransitions( Json, "Transitions", Definition.Transitions, FString(), Issues );
	ReadTransitions( Json, "AnyTransitions", Definition.AnyTransitions, FString(), Issues );
	const auto EditorIt = Json.find( "Editor" );
	if ( EditorIt != Json.end() )
	{
		Definition.Editor = *EditorIt;
	}
	OutDefinition = Definition;
	return true;
}
