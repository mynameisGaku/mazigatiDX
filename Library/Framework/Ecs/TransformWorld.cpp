#include "TransformWorld.h"


#include "Library/Core/Interfaces/Job/IJobSystem.h"
#include "Library/Core/Profiler/ProfilerScope.h"
#include "Library/Core/Thread/Atomic.h"
#include "Library/Framework/Object/GameObject.h"

namespace
{
	// 1 バッチの最小の個数。1 ノードの計算は十数 ns なので、これ以下の仕事はスレッドを起こすより、呼び出したスレッドで計算した方が速い
	constexpr int32 MinNodesPerBatch = 4096;
}

#pragma region Slots

int32 FTransformWorld::Allocate( CGameObject* Owner, const FTransform& Local )
{
	const int32 Slot = Nodes.Allocate();
	FTransformNode& Node = Nodes[Slot];
	Node.Local = Local;
	Node.World = Local;
	Node.Owner = Owner;
	Node.bDirty = true;
	bAnyDirty.Store( true, EMemoryOrder::Relaxed );
	bHierarchyDirty = true;
	return Slot;
}

void FTransformWorld::Free( int32 Slot )
{
	if ( !Nodes.IsAlive( Slot ) ) return;

	// 子は親を失う。次に組み直すとき、新しい親（または根）に付け替える
	for ( int32 Child = Nodes[Slot].FirstChild; Child != INDEX_NONE; Child = Nodes[Child].NextSibling )
	{
		if ( !Nodes.IsAlive( Child ) || Nodes[Child].Parent != Slot ) continue;
		Nodes[Child].Parent = INDEX_NONE;
		MarkDirty( Child );
	}

	Nodes.Free( Slot );
	bHierarchyDirty = true;
}

void FTransformWorld::MarkDirtySlow( int32 Slot )
{
	FTransformNode& Node = Nodes[Slot];
	Node.bDirty = true;
	bAnyDirty.Store( true, EMemoryOrder::Relaxed );
	if ( Node.FirstChild == INDEX_NONE ) return;   // 子がなければ、これだけ

	// 子孫を、深さ優先で立てる（スタックは使い回す。毎回確保しない）
	thread_local TArray<int32> Stack;
	Stack.Reset();
	Stack.Add( Slot );
	while ( !Stack.IsEmpty() )
	{
		const int32 Current = Stack.Last();
		Stack.Pop();
		for ( int32 Child = Nodes[Current].FirstChild; Child != INDEX_NONE; Child = Nodes[Child].NextSibling )
		{
			if ( !Nodes.IsAlive( Child ) || Nodes[Child].bDirty ) continue;
			Nodes[Child].bDirty = true;
			Stack.Add( Child );
		}
	}
}

#pragma endregion

void FTransformWorld::FlushDeferredDirty()
{
	for ( TArray<int32>& PerThread : DeferredDirty )
	{
		for ( const int32 Slot : PerThread )
		{
			if ( Nodes.IsAlive( Slot ) ) MarkDirty( Slot );
		}
		PerThread.Reset();
	}
}

#pragma region Hierarchy

int32 FTransformWorld::FindParentSlot( const CGameObject* Owner ) const
{
	for ( const CGameObject* Ancestor = Owner ? Owner->GetParent() : nullptr; Ancestor != nullptr; Ancestor = Ancestor->GetParent() )
	{
		const int32 Slot = Ancestor->GetTransformSlot();
		if ( Slot != INDEX_NONE && Nodes.IsAlive( Slot ) && !Nodes[Slot].bPendingDestroy ) return Slot;
	}
	return INDEX_NONE;
}

void FTransformWorld::RebuildHierarchy()
{
	bHierarchyDirty = false;
	const int32 NumSlots = Nodes.GetNumSlots();

	// 1) 親を探し直す。付け替わったものは、ワールド値が変わるので後で dirty にする
	TArray<int32> Changed;
	for ( int32 Slot = 0; Slot < NumSlots; ++Slot )
	{
		if ( !Nodes.IsAlive( Slot ) ) continue;
		FTransformNode& Node = Nodes[Slot];
		const int32 NewParent = FindParentSlot( Node.Owner );
		if ( NewParent != Node.Parent )
		{
			Node.Parent = NewParent;
			Changed.Add( Slot );
		}
	}

	// 2) 子の一覧（小さい番号から並ぶよう逆順に積む）
	for ( int32 Slot = 0; Slot < NumSlots; ++Slot )
	{
		if ( !Nodes.IsAlive( Slot ) ) continue;
		Nodes[Slot].FirstChild = INDEX_NONE;
		Nodes[Slot].NextSibling = INDEX_NONE;
	}
	for ( int32 Slot = NumSlots - 1; Slot >= 0; --Slot )
	{
		if ( !Nodes.IsAlive( Slot ) || Nodes[Slot].Parent == INDEX_NONE ) continue;
		FTransformNode& Parent = Nodes[Nodes[Slot].Parent];
		Nodes[Slot].NextSibling = Parent.FirstChild;
		Parent.FirstChild = Slot;
	}

	// 3) 根から深さ順に並べる（幅優先）。同じ親の子が隣り合うので、Update で親のワールド値がキャッシュに乗る
	Entries.Reset();
	Entries.Reserve( static_cast<size_t>( Nodes.GetNumAlive() ) );
	LevelStart.Reset();
	LevelStart.Add( 0 );
	for ( int32 Slot = 0; Slot < NumSlots; ++Slot )
	{
		if ( !Nodes.IsAlive( Slot ) ) continue;
		FTransformNode& Node = Nodes[Slot];
		if ( Node.Parent != INDEX_NONE ) continue;
		Node.Depth = 0;
		Entries.Add( { &Node, nullptr } );
	}
	LevelStart.Add( Entries.Num() );

	for ( int32 Depth = 1; ; ++Depth )
	{
		const int32 PrevBegin = LevelStart[Depth - 1];
		const int32 PrevEnd = LevelStart[Depth];
		for ( int32 i = PrevBegin; i < PrevEnd; ++i )
		{
			const FTransformNode* ParentNode = Entries[i].Node;
			for ( int32 Child = ParentNode->FirstChild; Child != INDEX_NONE; Child = Nodes[Child].NextSibling )
			{
				FTransformNode& ChildNode = Nodes[Child];
				ChildNode.Depth = Depth;
				Entries.Add( { &ChildNode, ParentNode } );
			}
		}
		if ( Entries.Num() == PrevEnd ) break;
		LevelStart.Add( Entries.Num() );
	}

	// 4) 付け替わったものと、その子孫のワールド値を古くする（子の一覧を作り直したあとなので、新しい子孫まで届く）
	for ( const int32 Slot : Changed )
	{
		FTransformNode& Node = Nodes[Slot];
		if ( Node.bDirty )
		{
			// すでに dirty でも、新しい子孫はまだ立っていないことがあるので、子孫は立て直す
			Node.bDirty = false;
		}
		MarkDirty( Slot );
	}
}

#pragma endregion

#pragma region World

FORCEINLINE void FTransformWorld::ComputeWithParent( FTransformNode& Node, const FTransform& Parent )
{
	// 以前の CTransformComponent::GetWorldTransform と同じ式（結果が 1 ビットも変わらないように、順序も同じにしてある）
	FTransform World;
	World.Position = Parent.Rotation.Rotate( Parent.Scale * Node.Local.Position ) + Parent.Position;
	World.Rotation = Parent.Rotation * Node.Local.Rotation;
	World.Scale = Parent.Scale * Node.Local.Scale;
	Node.World = World;
}

void FTransformWorld::Compute( FTransformNode& Node )
{
	if ( Node.Parent == INDEX_NONE )
	{
		Node.World = Node.Local;
		return;
	}
	ComputeWithParent( Node, Nodes[Node.Parent].World );
}

void FTransformWorld::EnsureClean( int32 Slot )
{
	FTransformNode& Node = Nodes[Slot];
	if ( !Node.bDirty ) return;

	// 親から順に（深さは浅いので再帰でよい）
	if ( Node.Parent != INDEX_NONE ) EnsureClean( Node.Parent );
	Compute( Node );
	Node.bDirty = false;
}

const FTransform& FTransformWorld::GetWorldSlow( int32 Slot )
{
	if ( bHierarchyDirty ) RebuildHierarchy();
	EnsureClean( Slot );
	return Nodes[Slot].World;
}

void FTransformWorld::Update( IJobSystem* Jobs )
{
	if ( bHierarchyDirty ) RebuildHierarchy();
	if ( !bAnyDirty.Load( EMemoryOrder::Relaxed ) ) return;

	// 浅い階層から順に。同じ階層の要素は互いに独立（親は前の階層で終わっている）なので、階層ごとに一斉に計算できる
	const int32 NumLevels = LevelStart.IsEmpty() ? 0 : LevelStart.Num() - 1;
	const FLevelEntry* AllEntries = Entries.GetData();
	for ( int32 Level = 0; Level < NumLevels; ++Level )
	{
		const int32 Begin = LevelStart[Level];
		const int32 Count = LevelStart[Level + 1] - Begin;
		if ( Count <= 0 ) continue;

		const FLevelEntry* LevelEntries = AllEntries + Begin;
		if ( Level == 0 )
		{
			const auto Body = [LevelEntries]( int32 First, int32 Last )
			{
				for ( int32 i = First; i < Last; ++i )
				{
					FTransformNode& Node = *LevelEntries[i].Node;
					if ( !Node.bDirty ) continue;
					Node.World = Node.Local;
					Node.bDirty = false;
				}
			};
			if ( Jobs != nullptr ) Jobs->ParallelForRange( Count, MinNodesPerBatch, "Transforms.Update", Body );
			else Body( 0, Count );
		}
		else
		{
			const auto Body = [LevelEntries]( int32 First, int32 Last )
			{
				for ( int32 i = First; i < Last; ++i )
				{
					const FLevelEntry& Entry = LevelEntries[i];
					FTransformNode& Node = *Entry.Node;
					if ( !Node.bDirty ) continue;
					ComputeWithParent( Node, Entry.Parent->World );
					Node.bDirty = false;
				}
			};
			if ( Jobs != nullptr ) Jobs->ParallelForRange( Count, MinNodesPerBatch, "Transforms.Update", Body );
			else Body( 0, Count );
		}
	}
	bAnyDirty.Store( false, EMemoryOrder::Relaxed );
}

#pragma endregion
