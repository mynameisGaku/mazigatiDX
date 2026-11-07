#include "AdventureCollision.h"

#include "Source/Adventure/AdventureState.h"

namespace
{
	// 円の中心 (X, Z) から、四角形の、いちばん近い点
	void GetClosestPoint( const FAdventureSolid& Solid, float32 X, float32 Z, float32& OutX, float32& OutZ )
	{
		OutX = NMath::Clamp( X, Solid.MinX, Solid.MaxX );
		OutZ = NMath::Clamp( Z, Solid.MinZ, Solid.MaxZ );
	}
}

void NAdventureCollision::Resolve( const FAdventureState& State, FVector3& Position, float32 Radius )
{
	for ( const FAdventureSolid& Solid : State.Solids )
	{
		const TSharedPtr<CGameObject> Object = Solid.Object.Pin();
		if ( !Object.IsValid() || Object->IsPendingDestroy() )
		{
			continue;
		}
		float32 ClosestX = 0.0f;
		float32 ClosestZ = 0.0f;
		GetClosestPoint( Solid, Position.x, Position.z, ClosestX, ClosestZ );
		float32 DX = Position.x - ClosestX;
		float32 DZ = Position.z - ClosestZ;
		const float32 DistanceSquared = DX * DX + DZ * DZ;
		if ( DistanceSquared >= Radius * Radius )
		{
			continue;
		}
		if ( DistanceSquared > 0.000001f )
		{
			// 外側: 円の中心から、いちばん近い点の反対へ、半径ぶん離す
			const float32 Distance = NMath::Sqrt( DistanceSquared );
			Position.x = ClosestX + DX / Distance * Radius;
			Position.z = ClosestZ + DZ / Distance * Radius;
		}
		else
		{
			// 四角形の中に、中心が入っている: いちばん近い辺から、外へ出す
			const float32 ToLeft = Position.x - Solid.MinX;
			const float32 ToRight = Solid.MaxX - Position.x;
			const float32 ToBack = Position.z - Solid.MinZ;
			const float32 ToFront = Solid.MaxZ - Position.z;
			const float32 Smallest = NMath::Min( NMath::Min( ToLeft, ToRight ), NMath::Min( ToBack, ToFront ) );
			if ( Smallest == ToLeft )
			{
				Position.x = Solid.MinX - Radius;
			}
			else if ( Smallest == ToRight )
			{
				Position.x = Solid.MaxX + Radius;
			}
			else if ( Smallest == ToBack )
			{
				Position.z = Solid.MinZ - Radius;
			}
			else
			{
				Position.z = Solid.MaxZ + Radius;
			}
		}
	}
}

bool NAdventureCollision::IsFree( const FAdventureState& State, const FVector3& Position, float32 Radius )
{
	for ( const FAdventureSolid& Solid : State.Solids )
	{
		const TSharedPtr<CGameObject> Object = Solid.Object.Pin();
		if ( !Object.IsValid() || Object->IsPendingDestroy() )
		{
			continue;
		}
		float32 ClosestX = 0.0f;
		float32 ClosestZ = 0.0f;
		GetClosestPoint( Solid, Position.x, Position.z, ClosestX, ClosestZ );
		const float32 DX = Position.x - ClosestX;
		const float32 DZ = Position.z - ClosestZ;
		if ( DX * DX + DZ * DZ < Radius * Radius )
		{
			return false;
		}
	}
	return true;
}
