// SPDX-License-Identifier: MIT
#include "BehaviorTreeEdit.h"

namespace
{
	bool IsValidNode( const FBehaviorTreeDefinition& Definition, int32 NodeIndex )
	{
		return NodeIndex >= 0 && NodeIndex < Definition.Nodes.Num();
	}

	// Root とその子孫か（親の前に子が来る順なので、前から 1 回で決まる）
	TArray<bool> SubtreeFlags( const FBehaviorTreeDefinition& Definition, int32 Root )
	{
		TArray<bool> InSubtree;
		InSubtree.SetNum( Definition.Nodes.Num(), false );
		InSubtree[Root] = true;
		for ( int32 Index = Root + 1; Index < Definition.Nodes.Num(); ++Index )
		{
			const int32 Parent = Definition.Nodes[Index].Parent;
			if ( Parent >= 0 && InSubtree[Parent] )
			{
				InSubtree[Index] = true;
			}
		}
		return InSubtree;
	}

	// 部分木を取り出す。先頭がルート（Parent は -1）で、ほかの Parent は、取り出した配列の中の番号
	TArray<FBTNodeDef> ExtractSubtree( const FBehaviorTreeDefinition& Definition, int32 Root )
	{
		const TArray<bool> InSubtree = SubtreeFlags( Definition, Root );
		TArray<int32> LocalIndex;
		LocalIndex.SetNum( Definition.Nodes.Num(), -1 );
		TArray<FBTNodeDef> Block;
		for ( int32 Index = 0; Index < Definition.Nodes.Num(); ++Index )
		{
			if ( InSubtree[Index] )
			{
				LocalIndex[Index] = Block.Num();
				FBTNodeDef Node = Definition.Nodes[Index];
				Node.Parent = Index == Root ? -1 : LocalIndex[Node.Parent];
				Block.Add( Node );
			}
		}
		return Block;
	}

	// 部分木（ExtractSubtree の形）を、NewParent の子の At 番目に置く。置いたルートの番号を返す
	int32 PlaceSubtree( FBehaviorTreeDefinition& Definition, const TArray<FBTNodeDef>& Block, int32 NewParent, int32 At )
	{
		int32 Position = Definition.Nodes.Num();
		if ( NewParent >= 0 )
		{
			const TArray<int32> Children = Definition.GetChildren( NewParent );
			if ( At >= 0 && At < Children.Num() )
			{
				Position = Children[At];
			}
		}
		else
		{
			Position = 0;
		}

		// 置く場所から後ろの番号を、ずらす
		const int32 Count = Block.Num();
		for ( FBTNodeDef& Node : Definition.Nodes )
		{
			if ( Node.Parent >= Position )
			{
				Node.Parent += Count;
			}
		}
		for ( int32 Local = 0; Local < Count; ++Local )
		{
			FBTNodeDef Node = Block[Local];
			Node.Parent = Local == 0 ? NewParent : Position + Block[Local].Parent;
			Definition.Nodes.Insert( Node, Position + Local );
		}
		return Position;
	}

	// 部分木を取り除いて、番号を詰める（残ったノードの Parent も直す）
	void RemoveSubtreeNodes( FBehaviorTreeDefinition& Definition, int32 Root, TArray<int32>* OutNewIndex = nullptr )
	{
		const TArray<bool> InSubtree = SubtreeFlags( Definition, Root );
		TArray<int32> NewIndex;
		NewIndex.SetNum( Definition.Nodes.Num(), -1 );
		TArray<FBTNodeDef> Kept;
		for ( int32 Index = 0; Index < Definition.Nodes.Num(); ++Index )
		{
			if ( !InSubtree[Index] )
			{
				NewIndex[Index] = Kept.Num();
				Kept.Add( Definition.Nodes[Index] );
			}
		}
		for ( FBTNodeDef& Node : Kept )
		{
			Node.Parent = Node.Parent >= 0 ? NewIndex[Node.Parent] : -1;
		}
		Definition.Nodes = Kept;
		if ( OutNewIndex != nullptr )
		{
			*OutNewIndex = NewIndex;
		}
	}

	template<typename TItem>
	void MoveItem( TArray<TItem>& Items, int32 From, int32 To )
	{
		TItem Moved = Items[From];
		Items.RemoveAt( From );
		Items.Insert( Moved, To );
	}

	template<typename TFunction>
	void ForEachDecorator( FBehaviorTreeDefinition& Definition, TFunction Function )
	{
		for ( FBTNodeDef& Node : Definition.Nodes )
		{
			for ( FBTDecoratorDef& Decorator : Node.Decorators )
			{
				Function( Decorator );
			}
		}
	}
}

void NBehaviorTreeEdit::SetBlackboard( TBehaviorTreeDocument& Document, const FString& Path )
{
	if ( Document.Get().Blackboard != Path )
	{
		Document.BeginEdit().Blackboard = Path;
	}
}

int32 NBehaviorTreeEdit::AddNode( TBehaviorTreeDocument& Document, int32 Parent, EBTNodeType Type, const FString& TaskName, int32 At )
{
	const FBehaviorTreeDefinition& Current = Document.Get();
	if ( Parent < 0 ? !Current.Nodes.IsEmpty() : ( !IsValidNode( Current, Parent ) || Current.Nodes[Parent].Type == EBTNodeType::Task ) )
	{
		return INDEX_NONE;
	}
	FBTNodeDef Node;
	Node.Type = Type;
	if ( Type == EBTNodeType::Task )
	{
		Node.Task.Do = TaskName;
	}
	TArray<FBTNodeDef> Block;
	Block.Add( Node );
	return PlaceSubtree( Document.BeginEdit(), Block, Parent, At );
}

void NBehaviorTreeEdit::RemoveNode( TBehaviorTreeDocument& Document, int32 NodeIndex )
{
	if ( IsValidNode( Document.Get(), NodeIndex ) )
	{
		RemoveSubtreeNodes( Document.BeginEdit(), NodeIndex );
	}
}

bool NBehaviorTreeEdit::MoveNode( TBehaviorTreeDocument& Document, int32 NodeIndex, int32 NewParent, int32 At, int32* OutNewIndex )
{
	const FBehaviorTreeDefinition& Current = Document.Get();
	if ( NodeIndex <= 0 || !IsValidNode( Current, NodeIndex ) || !IsValidNode( Current, NewParent ) || Current.Nodes[NewParent].Type == EBTNodeType::Task )
	{
		return false;
	}
	if ( SubtreeFlags( Current, NodeIndex )[NewParent] )
	{
		return false;
	}
	const TArray<FBTNodeDef> Block = ExtractSubtree( Current, NodeIndex );

	FBehaviorTreeDefinition& Definition = Document.BeginEdit();
	TArray<int32> NewIndex;
	RemoveSubtreeNodes( Definition, NodeIndex, &NewIndex );
	const int32 Placed = PlaceSubtree( Definition, Block, NewIndex[NewParent], At );
	if ( OutNewIndex != nullptr )
	{
		*OutNewIndex = Placed;
	}
	return true;
}

int32 NBehaviorTreeEdit::DuplicateNode( TBehaviorTreeDocument& Document, int32 NodeIndex )
{
	const FBehaviorTreeDefinition& Current = Document.Get();
	if ( NodeIndex <= 0 || !IsValidNode( Current, NodeIndex ) )
	{
		return INDEX_NONE;
	}
	const int32 Parent = Current.Nodes[NodeIndex].Parent;
	const int32 Position = Current.GetChildren( Parent ).Find( NodeIndex );
	const TArray<FBTNodeDef> Block = ExtractSubtree( Current, NodeIndex );
	return PlaceSubtree( Document.BeginEdit(), Block, Parent, Position + 1 );
}

bool NBehaviorTreeEdit::ChangeNodeType( TBehaviorTreeDocument& Document, int32 NodeIndex, EBTNodeType Type )
{
	const FBehaviorTreeDefinition& Current = Document.Get();
	if ( !IsValidNode( Current, NodeIndex ) || Current.Nodes[NodeIndex].Type == Type )
	{
		return false;
	}
	if ( Type == EBTNodeType::Task && !Current.GetChildren( NodeIndex ).IsEmpty() )
	{
		return false;
	}
	FBTNodeDef& Node = Document.BeginEdit().Nodes[NodeIndex];
	Node.Type = Type;
	if ( Type != EBTNodeType::Task )
	{
		Node.Task = FAICall();
	}
	return true;
}

int32 NBehaviorTreeEdit::WrapNode( TBehaviorTreeDocument& Document, int32 NodeIndex, EBTNodeType CompositeType )
{
	if ( !IsValidNode( Document.Get(), NodeIndex ) || CompositeType == EBTNodeType::Task )
	{
		return INDEX_NONE;
	}
	FBehaviorTreeDefinition& Definition = Document.BeginEdit();
	FBTNodeDef Wrapper;
	Wrapper.Type = CompositeType;
	Wrapper.Parent = Definition.Nodes[NodeIndex].Parent;
	for ( FBTNodeDef& Node : Definition.Nodes )
	{
		if ( Node.Parent >= NodeIndex )
		{
			++Node.Parent;
		}
	}
	Definition.Nodes.Insert( Wrapper, NodeIndex );
	Definition.Nodes[NodeIndex + 1].Parent = NodeIndex;
	return NodeIndex;
}

void NBehaviorTreeEdit::SetNodeName( TBehaviorTreeDocument& Document, int32 NodeIndex, const FString& Name, const char* CoalesceKey )
{
	if ( IsValidNode( Document.Get(), NodeIndex ) && Document.Get().Nodes[NodeIndex].Name != Name )
	{
		Document.BeginEdit( CoalesceKey ).Nodes[NodeIndex].Name = Name;
	}
}

void NBehaviorTreeEdit::SetNodeTask( TBehaviorTreeDocument& Document, int32 NodeIndex, const FAICall& Call, const char* CoalesceKey )
{
	if ( IsValidNode( Document.Get(), NodeIndex ) && Document.Get().Nodes[NodeIndex].Type == EBTNodeType::Task )
	{
		Document.BeginEdit( CoalesceKey ).Nodes[NodeIndex].Task = Call;
	}
}

void NBehaviorTreeEdit::SetParallelMode( TBehaviorTreeDocument& Document, int32 NodeIndex, EBTParallelMode Mode )
{
	if ( IsValidNode( Document.Get(), NodeIndex ) && Document.Get().Nodes[NodeIndex].ParallelMode != Mode )
	{
		Document.BeginEdit().Nodes[NodeIndex].ParallelMode = Mode;
	}
}

int32 NBehaviorTreeEdit::AddDecorator( TBehaviorTreeDocument& Document, int32 NodeIndex, const FBTDecoratorDef& Decorator )
{
	if ( !IsValidNode( Document.Get(), NodeIndex ) )
	{
		return INDEX_NONE;
	}
	return Document.BeginEdit().Nodes[NodeIndex].Decorators.Add( Decorator );
}

void NBehaviorTreeEdit::RemoveDecorator( TBehaviorTreeDocument& Document, int32 NodeIndex, int32 Index )
{
	if ( IsValidNode( Document.Get(), NodeIndex ) && Index >= 0 && Index < Document.Get().Nodes[NodeIndex].Decorators.Num() )
	{
		Document.BeginEdit().Nodes[NodeIndex].Decorators.RemoveAt( Index );
	}
}

void NBehaviorTreeEdit::MoveDecorator( TBehaviorTreeDocument& Document, int32 NodeIndex, int32 From, int32 To )
{
	if ( !IsValidNode( Document.Get(), NodeIndex ) )
	{
		return;
	}
	const int32 Num = Document.Get().Nodes[NodeIndex].Decorators.Num();
	if ( From >= 0 && From < Num && To >= 0 && To < Num && From != To )
	{
		MoveItem( Document.BeginEdit().Nodes[NodeIndex].Decorators, From, To );
	}
}

void NBehaviorTreeEdit::SetDecorator( TBehaviorTreeDocument& Document, int32 NodeIndex, int32 Index, const FBTDecoratorDef& Decorator, const char* CoalesceKey )
{
	if ( IsValidNode( Document.Get(), NodeIndex ) && Index >= 0 && Index < Document.Get().Nodes[NodeIndex].Decorators.Num() )
	{
		Document.BeginEdit( CoalesceKey ).Nodes[NodeIndex].Decorators[Index] = Decorator;
	}
}

int32 NBehaviorTreeEdit::AddService( TBehaviorTreeDocument& Document, int32 NodeIndex, const FBTServiceDef& Service )
{
	if ( !IsValidNode( Document.Get(), NodeIndex ) )
	{
		return INDEX_NONE;
	}
	return Document.BeginEdit().Nodes[NodeIndex].Services.Add( Service );
}

void NBehaviorTreeEdit::RemoveService( TBehaviorTreeDocument& Document, int32 NodeIndex, int32 Index )
{
	if ( IsValidNode( Document.Get(), NodeIndex ) && Index >= 0 && Index < Document.Get().Nodes[NodeIndex].Services.Num() )
	{
		Document.BeginEdit().Nodes[NodeIndex].Services.RemoveAt( Index );
	}
}

void NBehaviorTreeEdit::MoveService( TBehaviorTreeDocument& Document, int32 NodeIndex, int32 From, int32 To )
{
	if ( !IsValidNode( Document.Get(), NodeIndex ) )
	{
		return;
	}
	const int32 Num = Document.Get().Nodes[NodeIndex].Services.Num();
	if ( From >= 0 && From < Num && To >= 0 && To < Num && From != To )
	{
		MoveItem( Document.BeginEdit().Nodes[NodeIndex].Services, From, To );
	}
}

void NBehaviorTreeEdit::SetService( TBehaviorTreeDocument& Document, int32 NodeIndex, int32 Index, const FBTServiceDef& Service, const char* CoalesceKey )
{
	if ( IsValidNode( Document.Get(), NodeIndex ) && Index >= 0 && Index < Document.Get().Nodes[NodeIndex].Services.Num() )
	{
		Document.BeginEdit( CoalesceKey ).Nodes[NodeIndex].Services[Index] = Service;
	}
}

NlohmannJson NBehaviorTreeEdit::CopySubtree( const FBehaviorTreeDefinition& Definition, int32 NodeIndex )
{
	if ( !IsValidNode( Definition, NodeIndex ) )
	{
		return NlohmannJson();
	}
	FBehaviorTreeDefinition Copy;
	Copy.Nodes = ExtractSubtree( Definition, NodeIndex );
	return Copy.ToJson();
}

int32 NBehaviorTreeEdit::PasteSubtree( TBehaviorTreeDocument& Document, int32 Parent, int32 At, const NlohmannJson& Subtree )
{
	FBehaviorTreeDefinition Pasted;
	if ( !FBehaviorTreeDefinition::FromJson( Subtree, Pasted ) || Pasted.Nodes.IsEmpty() )
	{
		return INDEX_NONE;
	}
	const FBehaviorTreeDefinition& Current = Document.Get();
	if ( Parent < 0 ? !Current.Nodes.IsEmpty() : ( !IsValidNode( Current, Parent ) || Current.Nodes[Parent].Type == EBTNodeType::Task ) )
	{
		return INDEX_NONE;
	}
	return PlaceSubtree( Document.BeginEdit(), Pasted.Nodes, Parent, At );
}

int32 NBehaviorTreeEdit::CountBlackboardKeyUsage( const FBehaviorTreeDefinition& Definition, const FString& Key )
{
	int32 Count = 0;
	for ( const FBTNodeDef& Node : Definition.Nodes )
	{
		for ( const FBTDecoratorDef& Decorator : Node.Decorators )
		{
			Count += ( Decorator.Type == EBTDecoratorType::BlackboardCondition && Decorator.Key == Key ) ? 1 : 0;
		}
	}
	return Count;
}

int32 NBehaviorTreeEdit::RenameBlackboardKey( TBehaviorTreeDocument& Document, const FString& OldName, const FString& NewName )
{
	if ( OldName == NewName || CountBlackboardKeyUsage( Document.Get(), OldName ) == 0 )
	{
		return 0;
	}
	int32 Changed = 0;
	ForEachDecorator( Document.BeginEdit(), [&]( FBTDecoratorDef& Decorator )
	{
		if ( Decorator.Type == EBTDecoratorType::BlackboardCondition && Decorator.Key == OldName )
		{
			Decorator.Key = NewName;
			++Changed;
		}
	} );
	return Changed;
}
