#include "EditorPicking.h"

#include <algorithm>

#include "Library/Core/Types/Numeric.h"
#include "Library/Framework/World/WorldHierarchy.h"

bool NWorldEditor::RayVsShape( const FEditorRay& Ray, const FEditorShape& Shape, float32& OutT )
{
	switch ( Shape.Kind )
	{
	case EEditorShapeKind::Box: return RayVsOrientedBox( Ray, Shape.Center, Shape.Rotation, Shape.Half, OutT );
	case EEditorShapeKind::Sphere: return RayVsSphere( Ray, Shape.Center, Shape.Radius, OutT );
	case EEditorShapeKind::Capsule: return RayVsCapsule( Ray, Shape.A, Shape.B, Shape.Radius, OutT );
	case EEditorShapeKind::Quad: return RayVsQuad( Ray, Shape.Center, Shape.HalfRight, Shape.HalfUp, OutT );
	}
	return false;
}

namespace
{
	bool IsPickable( const FWorldObject& Object, const FEditorPickOptions& Options )
	{
		return Options.bPickDisabled || Object.bEnabled;
	}

	bool IsPickable( const FEditorShape& Shape, const FEditorPickOptions& Options )
	{
		return Options.bPickColliders || Shape.Role != EEditorShapeRole::Collider;
	}
}

TArray<FEditorHit> NWorldEditor::PickObjects( const FWorldDefinition& World, const FEditorRay& Ray, const FEditorShapeContext& Context, const FEditorPickOptions& Options )
{
	const TArray<FTransform> WorldTransforms = NWorld::ComputeAllWorldTransforms( World );
	TArray<FEditorHit> Hits;
	for ( int32 Index = 0; Index < World.Objects.Num(); ++Index )
	{
		const FWorldObject& Object = World.Objects[Index];
		if ( !IsPickable( Object, Options ) ) continue;

		FEditorHit Best;
		bool bHit = false;
		for ( const FEditorShape& Shape : GatherShapes( Object, WorldTransforms[Index], Context ) )
		{
			float32 T = 0.0f;
			if ( !IsPickable( Shape, Options ) || !RayVsShape( Ray, Shape, T ) ) continue;
			if ( !bHit || T < Best.Distance )
			{
				Best.ObjectId = Object.Id;
				Best.Distance = T;
				Best.Role = Shape.Role;
				bHit = true;
			}
		}
		if ( bHit ) Hits.Add( Best );
	}
	// 同じ距離なら、配列の順のまま
	Hits.StableSort( []( const FEditorHit& A, const FEditorHit& B ) { return A.Distance < B.Distance; } );
	return Hits;
}

int32 NWorldEditor::PickNext( const TArray<FEditorHit>& Hits, int32 CurrentId )
{
	if ( Hits.IsEmpty() ) return WorldNoId;
	for ( int32 i = 0; i < Hits.Num(); ++i )
	{
		if ( Hits[i].ObjectId == CurrentId ) return Hits[( i + 1 ) % Hits.Num()].ObjectId;
	}
	return Hits[0].ObjectId;
}

TArray<int32> NWorldEditor::FindObjectsInRect( const FWorldDefinition& World, const FEditorShapeContext& Context, float32 X0, float32 Y0, float32 X1, float32 Y1,
											   const FEditorPickOptions& Options )
{
	TArray<int32> Result;
	if ( Context.Camera == nullptr ) return Result;
	const float32 MinX = NMath::Min( X0, X1 ), MaxX = NMath::Max( X0, X1 );
	const float32 MinY = NMath::Min( Y0, Y1 ), MaxY = NMath::Max( Y0, Y1 );

	const TArray<FTransform> WorldTransforms = NWorld::ComputeAllWorldTransforms( World );
	for ( int32 Index = 0; Index < World.Objects.Num(); ++Index )
	{
		const FWorldObject& Object = World.Objects[Index];
		if ( !IsPickable( Object, Options ) ) continue;
		for ( const FEditorShape& Shape : GatherShapes( Object, WorldTransforms[Index], Context ) )
		{
			FVector2 Pixel;
			if ( !IsPickable( Shape, Options ) || !Context.Camera->WorldToScreen( Context.Viewport, GetShapeCenter( Shape ), Pixel ) ) continue;
			if ( Pixel.x >= MinX && Pixel.x <= MaxX && Pixel.y >= MinY && Pixel.y <= MaxY )
			{
				Result.Add( Object.Id );
				break;
			}
		}
	}
	return Result;
}

bool NWorldEditor::ComputeBoundingSphere( const FWorldDefinition& World, const TArray<int32>& Ids, const FEditorShapeContext& Context, FVector3& OutCenter, float32& OutRadius )
{
	const TArray<FTransform> WorldTransforms = NWorld::ComputeAllWorldTransforms( World );
	bool bAny = false;
	FVector3 Min( NMath::MaxFloat, NMath::MaxFloat, NMath::MaxFloat );
	FVector3 Max( -NMath::MaxFloat, -NMath::MaxFloat, -NMath::MaxFloat );
	for ( const int32 Id : Ids )
	{
		const int32 Index = World.FindIndexById( Id );
		if ( Index == INDEX_NONE ) continue;
		FVector3 ObjectMin, ObjectMax;
		if ( !ComputeShapesBounds( GatherShapes( World.Objects[Index], WorldTransforms[Index], Context ), ObjectMin, ObjectMax ) ) continue;
		Min = FVector3::Min( Min, ObjectMin );
		Max = FVector3::Max( Max, ObjectMax );
		bAny = true;
	}
	if ( !bAny ) return false;
	OutCenter = ( Min + Max ) * 0.5f;
	OutRadius = ( Max - Min ).Length() * 0.5f;
	return true;
}
