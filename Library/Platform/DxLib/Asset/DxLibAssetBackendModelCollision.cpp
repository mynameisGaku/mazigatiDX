// SPDX-License-Identifier: MIT
#include "DxLibAssetBackendSubsystem.h"

#include "Library/Math/MathUtils.h"
#include "Library/Platform/DxLib/DxLibInclude.h"
#include "Library/Platform/DxLib/DxLibTypes.h"

//=============================================================================
// CDxLibAssetBackendSubsystem のうち、モデルへの当たり判定（MV1SetupCollInfo / MV1CollCheck_Line / MV1CollCheck_Sphere）
//   DxLib の当たり判定の情報は、MV1RefreshCollInfo を呼んだときのモデルの形（置き場所の行列・アニメーション・形状を含む）を写したもの。
//   ここでは、置き場所の行列を一時的に単位行列にして写すので、情報はモデルの手元の座標になる（呼ぶ側が、線や球を手元の座標へ直す）。
//=============================================================================
namespace
{
	FModelHit ToHit( const MV1_COLL_RESULT_POLY& Poly, const FVector3& Position, float32 Distance )
	{
		FModelHit Hit;
		Hit.Position = Position;
		Hit.Normal = NDxLibTypes::FromDxLib( Poly.Normal ).Normalized();
		Hit.Distance = Distance;
		Hit.Frame = Poly.FrameIndex;
		Hit.Material = Poly.MaterialIndex;
		Hit.Triangle = Poly.PolygonIndex;
		return Hit;
	}

	// 点 P にいちばん近い、三角形 ABC の上の点
	FVector3 ClosestPointOnTriangle( const FVector3& P, const FVector3& A, const FVector3& B, const FVector3& C )
	{
		const VECTOR Point = NDxLibTypes::ToDxLib( P );
		const VECTOR First = NDxLibTypes::ToDxLib( A );
		const VECTOR Second = NDxLibTypes::ToDxLib( B );
		const VECTOR Third = NDxLibTypes::ToDxLib( C );
		TRIANGLE_POINT_RESULT Result = {};
		if ( Triangle_Point_Analyse( &First, &Second, &Third, &Point, &Result ) != 0 )
		{
			return A;
		}
		return NDxLibTypes::FromDxLib( Result.Tri_MinDist_Pos );
	}
}

bool CDxLibAssetBackendSubsystem::PrepareModelCollision( int32 Model )
{
	if ( Model < 0 || CheckHandleASyncLoad( Model ) != FALSE || MV1GetMeshNum( Model ) <= 0 )
	{
		return false;
	}
	if ( !CollisionReadyModels.Contains( Model ) && MV1SetupCollInfo( Model, -1 ) != 0 )
	{
		return false;
	}
	CollisionReadyModels.Add( Model );
	// 置き場所の行列を外して写し、元に戻す（描くときの行列は、描く命令が毎回決める）
	const MATRIX Previous = MV1GetMatrix( Model );
	MV1SetMatrix( Model, MGetIdent() );
	const bool bRefreshed = MV1RefreshCollInfo( Model, -1 ) == 0;
	MV1SetMatrix( Model, Previous );
	return bRefreshed;
}

bool CDxLibAssetBackendSubsystem::RaycastModel( int32 Model, const FVector3& From, const FVector3& To, FModelHit& Out )
{
	if ( !CollisionReadyModels.Contains( Model ) && !PrepareModelCollision( Model ) )
	{
		return false;
	}
	const MV1_COLL_RESULT_POLY Poly = MV1CollCheck_Line( Model, -1, NDxLibTypes::ToDxLib( From ), NDxLibTypes::ToDxLib( To ) );
	if ( Poly.HitFlag == 0 )
	{
		return false;
	}
	const FVector3 Position = NDxLibTypes::FromDxLib( Poly.HitPosition );
	Out = ToHit( Poly, Position, ( Position - From ).Length() );
	return true;
}

bool CDxLibAssetBackendSubsystem::OverlapModelSphere( int32 Model, const FVector3& Center, float32 Radius, TArray<FModelHit>& Out )
{
	if ( !( Radius > 0.0f ) || ( !CollisionReadyModels.Contains( Model ) && !PrepareModelCollision( Model ) ) )
	{
		return false;
	}
	MV1_COLL_RESULT_POLY_DIM Result = MV1CollCheck_Sphere( Model, -1, NDxLibTypes::ToDxLib( Center ), Radius );
	Out.Reset();
	for ( int32 Index = 0; Index < Result.HitNum; ++Index )
	{
		const MV1_COLL_RESULT_POLY& Poly = Result.Dim[Index];
		const FVector3 Closest = ClosestPointOnTriangle( Center, NDxLibTypes::FromDxLib( Poly.Position[0] ), NDxLibTypes::FromDxLib( Poly.Position[1] ), NDxLibTypes::FromDxLib( Poly.Position[2] ) );
		Out.Add( ToHit( Poly, Closest, ( Closest - Center ).Length() ) );
	}
	MV1CollResultPolyDimTerminate( Result );
	return true;
}
