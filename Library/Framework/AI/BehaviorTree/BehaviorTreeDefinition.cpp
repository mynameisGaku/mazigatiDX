// SPDX-License-Identifier: MIT
#include "BehaviorTreeDefinition.h"

#include "Library/Json/JsonHelpers.h"
#include "Library/Math/MathUtils.h"

namespace
{
	constexpr int32 CurrentVersion = 1;
	// 入れ子の深さの上限（壊れた・悪意のある JSON で、再帰が深くなりすぎないように）
	constexpr int32 MaxDepth = 32;

	bool IsConditionType( EBTDecoratorType Type )
	{
		return Type == EBTDecoratorType::BlackboardCondition || Type == EBTDecoratorType::Condition;
	}

	bool TryParseNodeType( const FString& Text, EBTNodeType& Out )
	{
		for ( const EBTNodeType Type : { EBTNodeType::Selector, EBTNodeType::Sequence, EBTNodeType::Parallel, EBTNodeType::Task } )
		{
			if ( Text == NAI::ToString( Type ) )
			{
				Out = Type;
				return true;
			}
		}
		return false;
	}

	bool TryParseParallelMode( const FString& Text, EBTParallelMode& Out )
	{
		for ( const EBTParallelMode Mode : { EBTParallelMode::AllSuccess, EBTParallelMode::AnySuccess, EBTParallelMode::Primary } )
		{
			if ( Text == NAI::ToString( Mode ) )
			{
				Out = Mode;
				return true;
			}
		}
		return false;
	}

	bool TryParseDecoratorType( const FString& Text, EBTDecoratorType& Out )
	{
		for ( const EBTDecoratorType Type : { EBTDecoratorType::BlackboardCondition, EBTDecoratorType::Condition, EBTDecoratorType::Cooldown, EBTDecoratorType::Inverter, EBTDecoratorType::Succeeder, EBTDecoratorType::Failer, EBTDecoratorType::Repeat, EBTDecoratorType::TimeLimit } )
		{
			if ( Text == NAI::ToString( Type ) )
			{
				Out = Type;
				return true;
			}
		}
		return false;
	}

	bool TryParseAbortMode( const FString& Text, EBTAbortMode& Out )
	{
		for ( const EBTAbortMode Mode : { EBTAbortMode::None, EBTAbortMode::Self, EBTAbortMode::LowerPriority, EBTAbortMode::Both } )
		{
			if ( Text == NAI::ToString( Mode ) )
			{
				Out = Mode;
				return true;
			}
		}
		return false;
	}

	//-------------------------------------------------------------------------
	// 読む
	//-------------------------------------------------------------------------
	void ReadParams( const NlohmannJson& Item, FAICall& Call )
	{
		const auto ParamsIt = Item.find( "Params" );
		if ( ParamsIt != Item.end() )
		{
			Call.Params = *ParamsIt;
		}
	}

	float32 ReadSeconds( const NlohmannJson& Item, const char* Key, float32 Default, const FString& Where, TArray<FAIIssue>* Issues )
	{
		const float32 Value = NJson::GetOr<float32>( Item, Key, Default );
		if ( !NMath::IsFinite( Value ) || Value < 0.0f )
		{
			NAI::AddIssue( Issues, EAIIssueSeverity::Warning, Where, FString::Printf( "%s is not a non-negative number. %g is used.", Key, static_cast<float64>( Default ) ) );
			return Default;
		}
		return Value;
	}

	bool ReadDecorator( const NlohmannJson& Item, FBTDecoratorDef& Out, const FString& Where, TArray<FAIIssue>* Issues )
	{
		if ( !Item.is_object() )
		{
			NAI::AddIssue( Issues, EAIIssueSeverity::Warning, Where, "The decorator is not an object. It was skipped." );
			return false;
		}
		if ( !TryParseDecoratorType( NJson::GetOr<FString>( Item, "Type", FString() ), Out.Type ) )
		{
			NAI::AddIssue( Issues, EAIIssueSeverity::Warning, Where, "The decorator has an unknown Type. It was skipped." );
			return false;
		}
		Out.bNot = NJson::GetOr<bool>( Item, "Not", false );

		if ( Item.contains( "Abort" ) )
		{
			if ( !TryParseAbortMode( NJson::GetOr<FString>( Item, "Abort", FString() ), Out.Abort ) )
			{
				NAI::AddIssue( Issues, EAIIssueSeverity::Warning, Where, "The Abort is not None / Self / LowerPriority / Both. None is used." );
				Out.Abort = EBTAbortMode::None;
			}
			if ( Out.Abort != EBTAbortMode::None && !IsConditionType( Out.Type ) )
			{
				NAI::AddIssue( Issues, EAIIssueSeverity::Warning, Where, "Only a condition decorator can abort. Abort was set to None." );
				Out.Abort = EBTAbortMode::None;
			}
		}

		switch ( Out.Type )
		{
		case EBTDecoratorType::BlackboardCondition:
		{
			Out.Key = NJson::GetOr<FString>( Item, "Key", FString() );
			if ( Out.Key.IsEmpty() )
			{
				NAI::AddIssue( Issues, EAIIssueSeverity::Warning, Where, "The condition has no Key. It was skipped." );
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
		case EBTDecoratorType::Condition:
			Out.Call.Do = NJson::GetOr<FString>( Item, "Do", FString() );
			if ( Out.Call.Do.IsEmpty() )
			{
				NAI::AddIssue( Issues, EAIIssueSeverity::Warning, Where, "The condition has no Do. It was skipped." );
				return false;
			}
			ReadParams( Item, Out.Call );
			return true;
		case EBTDecoratorType::Cooldown:
		case EBTDecoratorType::TimeLimit:
			Out.Seconds = ReadSeconds( Item, "Seconds", 1.0f, Where, Issues );
			return true;
		case EBTDecoratorType::Repeat:
			Out.Count = NJson::GetOr<int32>( Item, "Count", 0 );
			if ( Out.Count < 0 )
			{
				NAI::AddIssue( Issues, EAIIssueSeverity::Warning, Where, "The Count is negative. 0 (forever) is used." );
				Out.Count = 0;
			}
			Out.bUntilSuccess = NJson::GetOr<bool>( Item, "UntilSuccess", false );
			return true;
		case EBTDecoratorType::Inverter:
		case EBTDecoratorType::Succeeder:
		case EBTDecoratorType::Failer:
			return true;
		}
		return false;
	}

	bool ReadService( const NlohmannJson& Item, FBTServiceDef& Out, const FString& Where, TArray<FAIIssue>* Issues )
	{
		Out.Call.Do = NJson::GetOr<FString>( Item, "Do", FString() );
		if ( !Item.is_object() || Out.Call.Do.IsEmpty() )
		{
			NAI::AddIssue( Issues, EAIIssueSeverity::Warning, Where, "The service has no Do. It was skipped." );
			return false;
		}
		ReadParams( Item, Out.Call );
		Out.Interval = ReadSeconds( Item, "Interval", 0.5f, Where, Issues );
		return true;
	}

	// 1 つのノードを読んで、足した番号を返す（読めなければ -1）。子は、続けて読む
	int32 ReadNode( const NlohmannJson& Item, int32 Parent, FBehaviorTreeDefinition& Definition, const FString& Where, int32 Depth, TArray<FAIIssue>* Issues )
	{
		if ( !Item.is_object() )
		{
			NAI::AddIssue( Issues, EAIIssueSeverity::Warning, Where, "The node is not an object. It was skipped." );
			return -1;
		}
		if ( Depth > MaxDepth )
		{
			NAI::AddIssue( Issues, EAIIssueSeverity::Warning, Where, "The tree is nested too deeply. The rest was skipped." );
			return -1;
		}

		FBTNodeDef Node;
		Node.Parent = Parent;
		Node.Name = NJson::GetOr<FString>( Item, "Name", FString() );
		const FString Do = NJson::GetOr<FString>( Item, "Do", FString() );
		const FString TypeText = NJson::GetOr<FString>( Item, "Type", FString() );
		if ( !Do.IsEmpty() )
		{
			Node.Type = EBTNodeType::Task;
			Node.Task.Do = Do;
			ReadParams( Item, Node.Task );
		}
		else if ( !TryParseNodeType( TypeText, Node.Type ) || Node.Type == EBTNodeType::Task )
		{
			NAI::AddIssue( Issues, EAIIssueSeverity::Warning, Where, "The node has neither Do nor a Type of Selector / Sequence / Parallel. It was skipped." );
			return -1;
		}
		if ( Node.Type == EBTNodeType::Parallel && Item.contains( "Mode" ) && !TryParseParallelMode( NJson::GetOr<FString>( Item, "Mode", FString() ), Node.ParallelMode ) )
		{
			NAI::AddIssue( Issues, EAIIssueSeverity::Warning, Where, "The Mode is not AllSuccess / AnySuccess / Primary. AllSuccess is used." );
			Node.ParallelMode = EBTParallelMode::AllSuccess;
		}

		const auto DecoratorsIt = Item.find( "Decorators" );
		if ( DecoratorsIt != Item.end() && DecoratorsIt->is_array() )
		{
			int32 Index = 0;
			for ( const NlohmannJson& DecoratorJson : *DecoratorsIt )
			{
				FBTDecoratorDef Decorator;
				if ( ReadDecorator( DecoratorJson, Decorator, FString::Printf( "%s/Decorators[%d]", *Where, Index++ ), Issues ) )
				{
					Node.Decorators.Add( Decorator );
				}
			}
		}
		const auto ServicesIt = Item.find( "Services" );
		if ( ServicesIt != Item.end() && ServicesIt->is_array() )
		{
			int32 Index = 0;
			for ( const NlohmannJson& ServiceJson : *ServicesIt )
			{
				FBTServiceDef Service;
				if ( ReadService( ServiceJson, Service, FString::Printf( "%s/Services[%d]", *Where, Index++ ), Issues ) )
				{
					Node.Services.Add( Service );
				}
			}
		}
		const auto EditorIt = Item.find( "Editor" );
		if ( EditorIt != Item.end() )
		{
			Node.Editor = *EditorIt;
		}

		const int32 NodeIndex = Definition.Nodes.Add( Node );
		const auto ChildrenIt = Item.find( "Children" );
		if ( ChildrenIt != Item.end() && ChildrenIt->is_array() )
		{
			if ( Node.Type == EBTNodeType::Task )
			{
				NAI::AddIssue( Issues, EAIIssueSeverity::Warning, Where, "A task node cannot have Children. They were skipped." );
			}
			else
			{
				int32 Index = 0;
				for ( const NlohmannJson& ChildJson : *ChildrenIt )
				{
					ReadNode( ChildJson, NodeIndex, Definition, FString::Printf( "%s/Children[%d]", *Where, Index++ ), Depth + 1, Issues );
				}
			}
		}
		return NodeIndex;
	}

	//-------------------------------------------------------------------------
	// 書く
	//-------------------------------------------------------------------------
	void WriteCall( NlohmannJson& Out, const FAICall& Call )
	{
		Out["Do"] = Call.Do;
		if ( !Call.Params.is_null() && !( Call.Params.is_object() && Call.Params.empty() ) )
		{
			Out["Params"] = Call.Params;
		}
	}

	NlohmannJson NodeToJson( const FBehaviorTreeDefinition& Definition, int32 Index )
	{
		const FBTNodeDef& Node = Definition.Nodes[Index];
		NlohmannJson Item = NlohmannJson::object();
		if ( !Node.Name.IsEmpty() )
		{
			Item["Name"] = Node.Name;
		}
		if ( Node.Type == EBTNodeType::Task )
		{
			WriteCall( Item, Node.Task );
		}
		else
		{
			Item["Type"] = NAI::ToString( Node.Type );
			if ( Node.Type == EBTNodeType::Parallel && Node.ParallelMode != EBTParallelMode::AllSuccess )
			{
				Item["Mode"] = NAI::ToString( Node.ParallelMode );
			}
		}

		if ( !Node.Decorators.IsEmpty() )
		{
			NlohmannJson Decorators = NlohmannJson::array();
			for ( const FBTDecoratorDef& Decorator : Node.Decorators )
			{
				NlohmannJson D = NlohmannJson::object();
				D["Type"] = NAI::ToString( Decorator.Type );
				switch ( Decorator.Type )
				{
				case EBTDecoratorType::BlackboardCondition:
					D["Key"] = Decorator.Key;
					D["Op"] = NAI::ToString( Decorator.Op );
					D["Value"] = Decorator.Value;
					break;
				case EBTDecoratorType::Condition:
					WriteCall( D, Decorator.Call );
					break;
				case EBTDecoratorType::Cooldown:
				case EBTDecoratorType::TimeLimit:
					D["Seconds"] = NJson::Round4( Decorator.Seconds );
					break;
				case EBTDecoratorType::Repeat:
					if ( Decorator.Count != 0 )
					{
						D["Count"] = Decorator.Count;
					}
					if ( Decorator.bUntilSuccess )
					{
						D["UntilSuccess"] = true;
					}
					break;
				default:
					break;
				}
				if ( Decorator.bNot )
				{
					D["Not"] = true;
				}
				if ( Decorator.Abort != EBTAbortMode::None )
				{
					D["Abort"] = NAI::ToString( Decorator.Abort );
				}
				Decorators.push_back( D );
			}
			Item["Decorators"] = Decorators;
		}
		if ( !Node.Services.IsEmpty() )
		{
			NlohmannJson Services = NlohmannJson::array();
			for ( const FBTServiceDef& Service : Node.Services )
			{
				NlohmannJson S = NlohmannJson::object();
				WriteCall( S, Service.Call );
				S["Interval"] = NJson::Round4( Service.Interval );
				Services.push_back( S );
			}
			Item["Services"] = Services;
		}
		if ( !Node.Editor.is_null() )
		{
			Item["Editor"] = Node.Editor;
		}
		if ( Node.Type != EBTNodeType::Task )
		{
			NlohmannJson Children = NlohmannJson::array();
			for ( const int32 Child : Definition.GetChildren( Index ) )
			{
				Children.push_back( NodeToJson( Definition, Child ) );
			}
			Item["Children"] = Children;
		}
		return Item;
	}
}

//=============================================================================
// 名前
//=============================================================================
const char* NAI::ToString( EBTNodeType Type )
{
	switch ( Type )
	{
	case EBTNodeType::Selector:
		return "Selector";
	case EBTNodeType::Sequence:
		return "Sequence";
	case EBTNodeType::Parallel:
		return "Parallel";
	case EBTNodeType::Task:
		return "Task";
	}
	return "?";
}

const char* NAI::ToString( EBTParallelMode Mode )
{
	switch ( Mode )
	{
	case EBTParallelMode::AllSuccess:
		return "AllSuccess";
	case EBTParallelMode::AnySuccess:
		return "AnySuccess";
	case EBTParallelMode::Primary:
		return "Primary";
	}
	return "?";
}

const char* NAI::ToString( EBTDecoratorType Type )
{
	switch ( Type )
	{
	case EBTDecoratorType::BlackboardCondition:
		return "BlackboardCondition";
	case EBTDecoratorType::Condition:
		return "Condition";
	case EBTDecoratorType::Cooldown:
		return "Cooldown";
	case EBTDecoratorType::Inverter:
		return "Inverter";
	case EBTDecoratorType::Succeeder:
		return "Succeeder";
	case EBTDecoratorType::Failer:
		return "Failer";
	case EBTDecoratorType::Repeat:
		return "Repeat";
	case EBTDecoratorType::TimeLimit:
		return "TimeLimit";
	}
	return "?";
}

const char* NAI::ToString( EBTAbortMode Mode )
{
	switch ( Mode )
	{
	case EBTAbortMode::None:
		return "None";
	case EBTAbortMode::Self:
		return "Self";
	case EBTAbortMode::LowerPriority:
		return "LowerPriority";
	case EBTAbortMode::Both:
		return "Both";
	}
	return "?";
}

//=============================================================================
// FBehaviorTreeDefinition
//=============================================================================
TArray<int32> FBehaviorTreeDefinition::GetChildren( int32 Parent ) const
{
	TArray<int32> Children;
	for ( int32 Index = 0; Index < Nodes.Num(); ++Index )
	{
		if ( Nodes[Index].Parent == Parent )
		{
			Children.Add( Index );
		}
	}
	return Children;
}

FString FBehaviorTreeDefinition::GetNodeLabel( int32 Index ) const
{
	if ( Index < 0 || Index >= Nodes.Num() )
	{
		return FString();
	}
	const FBTNodeDef& Node = Nodes[Index];
	if ( !Node.Name.IsEmpty() )
	{
		return Node.Name;
	}
	return Node.Type == EBTNodeType::Task ? FString::Printf( "%s #%d", *Node.Task.Do, Index ) : FString::Printf( "%s #%d", NAI::ToString( Node.Type ), Index );
}

NlohmannJson FBehaviorTreeDefinition::ToJson() const
{
	NlohmannJson Json = NlohmannJson::object();
	Json["Version"] = CurrentVersion;
	if ( !Blackboard.IsEmpty() )
	{
		Json["Blackboard"] = Blackboard;
	}
	if ( HasRoot() )
	{
		Json["Root"] = NodeToJson( *this, 0 );
	}
	if ( !Editor.is_null() )
	{
		Json["Editor"] = Editor;
	}
	return Json;
}

bool FBehaviorTreeDefinition::FromJson( const NlohmannJson& Json, FBehaviorTreeDefinition& OutDefinition, TArray<FAIIssue>* Issues )
{
	if ( !Json.is_object() )
	{
		return false;
	}

	FBehaviorTreeDefinition Definition;
	Definition.Blackboard = NJson::GetOr<FString>( Json, "Blackboard", FString() );
	const auto RootIt = Json.find( "Root" );
	if ( RootIt != Json.end() )
	{
		ReadNode( *RootIt, -1, Definition, "Root", 0, Issues );
	}
	const auto EditorIt = Json.find( "Editor" );
	if ( EditorIt != Json.end() )
	{
		Definition.Editor = *EditorIt;
	}
	OutDefinition = Definition;
	return true;
}
