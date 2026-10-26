#include "WorldHierarchy.h"

#include <cmath>

#include "Library/Core/Collection/Map.h"
#include "Library/Core/Types/Numeric.h"

namespace
{
	// 無限ループを避けるための、親をたどる最大の回数（これを超える階層は、循環とみなす）
	constexpr int32 MaxDepth = 1024;
}

TArray<int32> NWorld::GetChildIndices( const FWorldDefinition& World, int32 ParentId )
{
	TArray<int32> Children;
	for ( int32 i = 0; i < World.Objects.Num(); ++i )
	{
		if ( World.Objects[i].Parent == ParentId && World.Objects[i].Id != ParentId ) Children.Add( i );
	}
	return Children;
}

TArray<int32> NWorld::GetSubtreeIndices( const FWorldDefinition& World, int32 RootId )
{
	TArray<int32> Result;
	const int32 RootIndex = World.FindIndexById( RootId );
	if ( RootIndex == INDEX_NONE ) return Result;

	TArray<bool> Visited;
	Visited.Init( false, World.Objects.Num() );

	// 親が先の順（深さ優先）。スタックには、後ろの兄弟が先に積まれるよう、逆順で積む
	TArray<int32> Stack;
	Stack.Add( RootIndex );
	while ( !Stack.IsEmpty() )
	{
		const int32 Index = Stack.Pop();
		if ( Visited[Index] ) continue;
		Visited[Index] = true;
		Result.Add( Index );
		const TArray<int32> Children = GetChildIndices( World, World.Objects[Index].Id );
		for ( int32 i = Children.Num() - 1; i >= 0; --i ) Stack.Add( Children[i] );
	}
	return Result;
}

bool NWorld::IsDescendantOf( const FWorldDefinition& World, int32 AncestorId, int32 CandidateId )
{
	if ( AncestorId == WorldNoId || AncestorId == CandidateId ) return false;
	int32 Current = CandidateId;
	for ( int32 Step = 0; Step < MaxDepth; ++Step )
	{
		const FWorldObject* Object = World.FindObject( Current );
		if ( Object == nullptr || Object->Parent == WorldNoId ) return false;
		if ( Object->Parent == AncestorId ) return true;
		Current = Object->Parent;
	}
	return false;
}

int32 NWorld::GetDepth( const FWorldDefinition& World, int32 ObjectId )
{
	int32 Depth = 0;
	int32 Current = ObjectId;
	for ( int32 Step = 0; Step < MaxDepth; ++Step )
	{
		const FWorldObject* Object = World.FindObject( Current );
		if ( Object == nullptr || Object->Parent == WorldNoId || World.FindObject( Object->Parent ) == nullptr ) return Depth;
		++Depth;
		Current = Object->Parent;
	}
	return Depth;
}

TArray<int32> NWorld::FilterToRoots( const FWorldDefinition& World, const TArray<int32>& Ids )
{
	TArray<int32> Roots;
	for ( const FWorldObject& Object : World.Objects )
	{
		if ( !Ids.Contains( Object.Id ) ) continue;
		bool bAncestorSelected = false;
		for ( const int32 Other : Ids )
		{
			if ( Other != Object.Id && IsDescendantOf( World, Other, Object.Id ) )
			{
				bAncestorSelected = true;
				break;
			}
		}
		if ( !bAncestorSelected ) Roots.Add( Object.Id );
	}
	return Roots;
}

FTransform NWorld::ComputeWorldTransform( const FWorldDefinition& World, int32 ObjectId )
{
	// 自分から親へたどって集め、最上位から順に合成する
	TArray<const FWorldObject*> Chain;
	int32 Current = ObjectId;
	for ( int32 Step = 0; Step < MaxDepth; ++Step )
	{
		const FWorldObject* Object = World.FindObject( Current );
		if ( Object == nullptr ) break;
		Chain.Add( Object );
		if ( Object->Parent == WorldNoId ) break;
		Current = Object->Parent;
	}

	FTransform Result = FTransform::Identity;
	for ( int32 i = Chain.Num() - 1; i >= 0; --i ) Result = FTransform::Combine( Result, Chain[i]->Transform.ToTransform() );
	return Result;
}

FTransform NWorld::ComputeParentWorldTransform( const FWorldDefinition& World, int32 ObjectId )
{
	const FWorldObject* Object = World.FindObject( ObjectId );
	if ( Object == nullptr || Object->Parent == WorldNoId || World.FindObject( Object->Parent ) == nullptr ) return FTransform::Identity;
	return ComputeWorldTransform( World, Object->Parent );
}

FTransform NWorld::WorldToLocal( const FTransform& ParentWorld, const FTransform& ChildWorld )
{
	const auto Divide = []( float32 Value, float32 By ) { return NMath::Abs( By ) > 1.0e-8f ? Value / By : Value; };
	FTransform Local;
	Local.Position = ParentWorld.InverseTransformPoint( ChildWorld.Position );
	Local.Rotation = ( ParentWorld.Rotation.Inverse() * ChildWorld.Rotation ).Normalized();
	Local.Scale = FVector3( Divide( ChildWorld.Scale.x, ParentWorld.Scale.x ), Divide( ChildWorld.Scale.y, ParentWorld.Scale.y ), Divide( ChildWorld.Scale.z, ParentWorld.Scale.z ) );
	return Local;
}

TArray<FTransform> NWorld::ComputeAllWorldTransforms( const FWorldDefinition& World )
{
	const int32 Num = World.Objects.Num();
	TMap<int32, int32> IndexById;
	for ( int32 i = 0; i < Num; ++i )
	{
		if ( !IndexById.Contains( World.Objects[i].Id ) ) IndexById.Add( World.Objects[i].Id, i );   // 重複は、先のものが正（FindIndexById と同じ）
	}

	TArray<FTransform> Result;
	Result.SetNum( Num );
	TArray<uint8> State;   // 0 = 未計算、1 = たどっている途中（循環の検出）、2 = 済み
	State.Init( 0, Num );
	TArray<int32> Chain;
	for ( int32 Start = 0; Start < Num; ++Start )
	{
		if ( State[Start] == 2 ) continue;

		// 計算済み・最上位・循環のどれかに当たるまで、親をたどる
		Chain.Reset();
		FTransform ParentWorld = FTransform::Identity;
		int32 Current = Start;
		while ( true )
		{
			if ( State[Current] == 2 )
			{
				ParentWorld = Result[Current];
				break;
			}
			if ( State[Current] == 1 ) break;   // 循環
			State[Current] = 1;
			Chain.Add( Current );
			const int32 Parent = World.Objects[Current].Parent;
			const int32* ParentIndex = Parent == WorldNoId ? nullptr : IndexById.Find( Parent );
			if ( ParentIndex == nullptr ) break;
			Current = *ParentIndex;
		}

		for ( int32 i = Chain.Num() - 1; i >= 0; --i )
		{
			Result[Chain[i]] = FTransform::Combine( ParentWorld, World.Objects[Chain[i]].Transform.ToTransform() );
			ParentWorld = Result[Chain[i]];
			State[Chain[i]] = 2;
		}
	}
	return Result;
}

NWorld::FWorldSpawnOrder NWorld::ComputeSpawnOrder( const FWorldDefinition& World, bool bSkipDisabled )
{
	FWorldSpawnOrder Result;
	const int32 Num = World.Objects.Num();
	TArray<bool> Done;   // 順番に入れた、または、無効な親の下なので入れない
	Done.Init( false, Num );

	// 親が先の順（深さ優先）。兄弟は配列の順
	const auto Visit = [&]( int32 StartIndex )
	{
		TArray<int32> Stack;
		Stack.Add( StartIndex );
		while ( !Stack.IsEmpty() )
		{
			const int32 Index = Stack.Pop();
			if ( Done[Index] ) continue;
			Done[Index] = true;
			if ( bSkipDisabled && !World.Objects[Index].bEnabled )
			{
				// 無効なオブジェクトの子孫も、入れない（Cyclic にもしない）
				for ( const int32 Skipped : GetSubtreeIndices( World, World.Objects[Index].Id ) ) Done[Skipped] = true;
				continue;
			}
			Result.Order.Add( Index );
			const TArray<int32> Children = GetChildIndices( World, World.Objects[Index].Id );
			for ( int32 i = Children.Num() - 1; i >= 0; --i ) Stack.Add( Children[i] );
		}
	};

	for ( int32 i = 0; i < Num; ++i )
	{
		const FWorldObject& Object = World.Objects[i];
		const bool bRoot = Object.Parent == WorldNoId;
		const bool bOrphan = !bRoot && World.FindIndexById( Object.Parent ) == INDEX_NONE;
		if ( !bRoot && !bOrphan ) continue;
		if ( bOrphan ) Result.Orphans.Add( i );
		Visit( i );
	}

	for ( int32 i = 0; i < Num; ++i )
	{
		if ( !Done[i] ) Result.Cyclic.Add( i );
	}
	return Result;
}
