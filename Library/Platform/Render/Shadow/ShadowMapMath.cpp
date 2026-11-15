// SPDX-License-Identifier: MIT
#include "ShadowMapMath.h"
#include "Library/Core/Types/Numeric.h"
#include "Library/Math/MathUtils.h"

namespace
{
	bool IsFinite( const FVector3& Value )
	{
		return NMath::IsFinite( Value.x ) && NMath::IsFinite( Value.y ) && NMath::IsFinite( Value.z );
	}
}

bool NShadowMapMath::ComputeLightViewProjection( const FDrawBeginShadowMapData& Data, const FCamera& Camera, const FCameraViewport& Viewport, FMatrix4x4& Out )
{
	const FVector3 Direction( Data.Direction[0], Data.Direction[1], Data.Direction[2] );
	if ( !IsFinite( Direction ) || Direction.Length() <= NMath::SmallNumber )
	{
		return false;
	}
	// 影を計算する範囲の 8 つの角（ワールド）。
	FVector3 Corners[8];
	if ( Data.HasArea != 0 )
	{
		const FVector3 Min( Data.AreaMin[0], Data.AreaMin[1], Data.AreaMin[2] ), Max( Data.AreaMax[0], Data.AreaMax[1], Data.AreaMax[2] );
		if ( !IsFinite( Min ) || !IsFinite( Max ) || !( Min.x < Max.x && Min.y < Max.y && Min.z < Max.z ) )
		{
			return false;
		}
		for ( int32 Index = 0; Index < 8; ++Index )
		{
			Corners[Index] = FVector3( Index & 1 ? Max.x : Min.x, Index & 2 ? Max.y : Min.y, Index & 4 ? Max.z : Min.z );
		}
	}
	else
	{
		// カメラの見える範囲（視錐台）の角。クリップ座標（奥行きは 0〜1）の立方体の角を、ワールドへ戻す。
		FMatrix4x4 Inverse;
		if ( !Camera.IsValid() || !( Camera.GetViewMatrix() * Camera.GetProjectionMatrix( Viewport ) ).TryInverse( Inverse ) )
		{
			return false;
		}
		for ( int32 Index = 0; Index < 8; ++Index )
		{
			const FVector4 World = Inverse.Transform( FVector4( Index & 1 ? 1.0f : -1.0f, Index & 2 ? 1.0f : -1.0f, Index & 4 ? 1.0f : 0.0f, 1.0f ) );
			if ( !NMath::IsFinite( World.w ) || NMath::Abs( World.w ) < NMath::SmallNumber )
			{
				return false;
			}
			Corners[Index] = FVector3( World.x / World.w, World.y / World.w, World.z / World.w );
		}
	}
	FVector3 Center = FVector3::Zero;
	for ( const FVector3& Corner : Corners )
	{
		if ( !IsFinite( Corner ) )
		{
			return false;
		}
		Center += Corner;
	}
	Center = Center / 8.0f;
	const FVector3 Forward = Direction.Normalized();
	const FVector3 Up = NMath::Abs( Forward.y ) > 0.99f ? FVector3::Forward : FVector3::Up; // 真上・真下を向くときは、別の向きを上にする。
	const FMatrix4x4 View = FMatrix4x4::LookAt( Center, Center + Forward, Up );
	FVector3 Min( 1.0e30f, 1.0e30f, 1.0e30f ), Max( -1.0e30f, -1.0e30f, -1.0e30f );
	for ( const FVector3& Corner : Corners )
	{
		const FVector3 Local = View.TransformPoint( Corner ); // 光から見た位置（x 右、y 上、z 奥）。
		Min = FVector3( NMath::Min( Min.x, Local.x ), NMath::Min( Min.y, Local.y ), NMath::Min( Min.z, Local.z ) );
		Max = FVector3( NMath::Max( Max.x, Local.x ), NMath::Max( Max.y, Local.y ), NMath::Max( Max.z, Local.z ) );
	}
	const float32 Width = Max.x - Min.x, Height = Max.y - Min.y, Depth = Max.z - Min.z;
	if ( !( Width > NMath::SmallNumber ) || !( Height > NMath::SmallNumber ) || !( Depth > NMath::SmallNumber ) || !NMath::IsFinite( Width + Height + Depth ) )
	{
		return false;
	}
	// 平行投影（x, y は -1〜1、奥行きは 0〜1）。行ベクトルに右から掛ける形。
	FMatrix4x4 Projection = FMatrix4x4::Identity;
	Projection.m[0][0] = 2.0f / Width;
	Projection.m[1][1] = 2.0f / Height;
	Projection.m[2][2] = 1.0f / Depth;
	Projection.m[3][0] = -( Max.x + Min.x ) / Width;
	Projection.m[3][1] = -( Max.y + Min.y ) / Height;
	Projection.m[3][2] = -Min.z / Depth;
	Out = View * Projection;
	return true;
}
