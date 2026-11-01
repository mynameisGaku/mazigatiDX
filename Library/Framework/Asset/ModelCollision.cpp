// SPDX-License-Identifier: MIT
#include "Library/Framework/Asset/ModelCollision.h"

#include "Library/Core/Interfaces/Asset/AssetBackend.h"
#include "Library/Core/Thread/GameThread.h"
#include "Library/Framework/Asset/Asset.h"
#include "Library/Math/MathUtils.h"

namespace
{
	// 手元の座標で求めた当たりを、ワールドの座標へ戻す
	FModelHit ToWorld( const FModelHit& Local, const FMatrix4x4& World, const FVector3& Origin )
	{
		FModelHit Hit = Local;
		Hit.Position = World.TransformPoint( Local.Position );
		// 向きは、逆行列の転置で写す（拡大が縦横高さ同じなら、行列そのままで写して長さをそろえるのと同じ）
		Hit.Normal = World.TransformVector( Local.Normal ).Normalized();
		Hit.Distance = ( Hit.Position - Origin ).Length();
		return Hit;
	}

	// 行列の拡大の倍率（縦横高さ同じ前提。3 軸の平均）
	float32 UniformScale( const FMatrix4x4& World )
	{
		const FVector3 Scale = World.GetScale();
		return ( Scale.x + Scale.y + Scale.z ) / 3.0f;
	}
}

FModelCollision::FModelCollision( int32 InHandle )
	: Handle( InHandle )
{
}

bool FModelCollision::Refresh()
{
	GAME_THREAD_ONLY( false );
	IAssetBackend* Backend = NAssetService::GetBackend();
	return Backend != nullptr && Handle >= 0 && Backend->PrepareModelCollision( Handle );
}

bool FModelCollision::Raycast( const FMatrix4x4& World, const FVector3& From, const FVector3& To, FModelHit& Out ) const
{
	GAME_THREAD_ONLY( false );
	IAssetBackend* Backend = NAssetService::GetBackend();
	FMatrix4x4 Inverse;
	if ( Backend == nullptr || Handle < 0 || !World.TryInverse( Inverse ) )
	{
		return false;
	}
	FModelHit Local;
	if ( !Backend->RaycastModel( Handle, Inverse.TransformPoint( From ), Inverse.TransformPoint( To ), Local ) )
	{
		return false;
	}
	Out = ToWorld( Local, World, From );
	return true;
}

TArray<FModelHit> FModelCollision::OverlapSphere( const FMatrix4x4& World, const FVector3& Center, float32 Radius ) const
{
	GAME_THREAD_ONLY( TArray<FModelHit>() );
	TArray<FModelHit> Hits;
	IAssetBackend* Backend = NAssetService::GetBackend();
	FMatrix4x4 Inverse;
	const float32 Scale = UniformScale( World );
	if ( Backend == nullptr || Handle < 0 || !( Radius > 0.0f ) || !( Scale > 0.0f ) || !World.TryInverse( Inverse ) )
	{
		return Hits;
	}
	TArray<FModelHit> Local;
	if ( !Backend->OverlapModelSphere( Handle, Inverse.TransformPoint( Center ), Radius / Scale, Local ) )
	{
		return Hits;
	}
	for ( const FModelHit& Hit : Local )
	{
		Hits.Add( ToWorld( Hit, World, Center ) );
	}
	return Hits;
}
