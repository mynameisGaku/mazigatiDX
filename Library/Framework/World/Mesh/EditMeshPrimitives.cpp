#include "EditMeshPrimitives.h"

#include <algorithm>
#include <cmath>

#include "Library/Core/Collection/Map.h"
#include "Library/Core/Types/Numeric.h"
#include "Library/Math/MathConstants.h"

namespace
{
	constexpr float32 MinLength = 1.0e-3f;

	// 頂点を、位置で共有しながら足す
	class FBuilder
	{
	public:
		FEditMesh Mesh;

		int32 Vertex( const FVector3& Position )
		{
			const int64 Key = Quantize( Position );
			TArray<int32>& Bucket = Buckets.FindOrAdd( Key );
			for ( const int32 Index : Bucket )
			{
				if ( FVector3::DistanceSquared( Mesh.Vertices[Index], Position ) < 1.0e-10f ) return Index;
			}
			const int32 Index = Mesh.Vertices.Add( Position );
			Bucket.Add( Index );
			return Index;
		}

		void Face( std::initializer_list<FVector3> Corners )
		{
			FEditMeshFace Face;
			for ( const FVector3& Corner : Corners ) Face.Indices.Add( Vertex( Corner ) );
			Mesh.Faces.Add( Face );
		}

		void Face( const TArray<FVector3>& Corners )
		{
			FEditMeshFace Face;
			for ( const FVector3& Corner : Corners ) Face.Indices.Add( Vertex( Corner ) );
			Mesh.Faces.Add( Face );
		}

		// 軸に垂直な長方形の面（外向きに表）。名前は、表が向く方向
		void QuadPosY( float32 X0, float32 X1, float32 Y, float32 ZA, float32 ZB ) { Face( { FVector3( X0, Y, ZA ), FVector3( X0, Y, ZB ), FVector3( X1, Y, ZB ), FVector3( X1, Y, ZA ) } ); }
		void QuadNegY( float32 X0, float32 X1, float32 Y, float32 ZA, float32 ZB ) { Face( { FVector3( X0, Y, ZA ), FVector3( X1, Y, ZA ), FVector3( X1, Y, ZB ), FVector3( X0, Y, ZB ) } ); }
		void QuadPosX( float32 X, float32 YA, float32 YB, float32 ZA, float32 ZB ) { Face( { FVector3( X, YA, ZA ), FVector3( X, YB, ZA ), FVector3( X, YB, ZB ), FVector3( X, YA, ZB ) } ); }
		void QuadNegX( float32 X, float32 YA, float32 YB, float32 ZA, float32 ZB ) { Face( { FVector3( X, YA, ZA ), FVector3( X, YA, ZB ), FVector3( X, YB, ZB ), FVector3( X, YB, ZA ) } ); }
		void QuadPosZ( float32 Z, float32 X0, float32 X1, float32 YA, float32 YB ) { Face( { FVector3( X0, YA, Z ), FVector3( X1, YA, Z ), FVector3( X1, YB, Z ), FVector3( X0, YB, Z ) } ); }
		void QuadNegZ( float32 Z, float32 X0, float32 X1, float32 YA, float32 YB ) { Face( { FVector3( X0, YA, Z ), FVector3( X0, YB, Z ), FVector3( X1, YB, Z ), FVector3( X1, YA, Z ) } ); }

	private:
		static int64 Quantize( const FVector3& P )
		{
			const auto Cell = [] ( float32 V ) { return static_cast<int64>( std::floor( V * 1000.0f ) ); };
			return ( Cell( P.x ) * 73856093ll ) ^ ( Cell( P.y ) * 19349663ll ) ^ ( Cell( P.z ) * 83492791ll );
		}

		TMap<int64, TArray<int32>> Buckets;
	};

	FVector3 Clamp( const FVector3& Size )
	{
		return FVector3( NMath::Max( Size.x, MinLength ), NMath::Max( Size.y, MinLength ), NMath::Max( Size.z, MinLength ) );
	}
}

const char* NEditMesh::ToString( EEditMeshPrimitive Kind )
{
	switch ( Kind )
	{
	case EEditMeshPrimitive::Box: return "Box";
	case EEditMeshPrimitive::Plane: return "Plane";
	case EEditMeshPrimitive::Cylinder: return "Cylinder";
	case EEditMeshPrimitive::Sphere: return "Sphere";
	case EEditMeshPrimitive::Ramp: return "Ramp";
	case EEditMeshPrimitive::Stairs: return "Stairs";
	}
	return "Box";
}

FEditMesh NEditMesh::MakeBox( const FVector3& Size )
{
	const FVector3 S = Clamp( Size );
	const float32 X0 = -S.x * 0.5f, X1 = S.x * 0.5f, Y0 = 0.0f, Y1 = S.y, Z0 = -S.z * 0.5f, Z1 = S.z * 0.5f;
	FBuilder B;
	B.QuadPosY( X0, X1, Y1, Z0, Z1 );
	B.QuadNegY( X0, X1, Y0, Z0, Z1 );
	B.QuadPosX( X1, Y0, Y1, Z0, Z1 );
	B.QuadNegX( X0, Y0, Y1, Z0, Z1 );
	B.QuadPosZ( Z1, X0, X1, Y0, Y1 );
	B.QuadNegZ( Z0, X0, X1, Y0, Y1 );
	return B.Mesh;
}

FEditMesh NEditMesh::MakePlane( float32 Width, float32 Depth, int32 DivisionsX, int32 DivisionsZ )
{
	const float32 W = NMath::Max( Width, MinLength ), D = NMath::Max( Depth, MinLength );
	const int32 NX = NMath::Clamp( DivisionsX, 1, 256 ), NZ = NMath::Clamp( DivisionsZ, 1, 256 );
	FBuilder B;
	for ( int32 i = 0; i < NX; ++i )
	{
		for ( int32 j = 0; j < NZ; ++j )
		{
			const float32 XA = -W * 0.5f + W * static_cast<float32>( i ) / static_cast<float32>( NX );
			const float32 XB = -W * 0.5f + W * static_cast<float32>( i + 1 ) / static_cast<float32>( NX );
			const float32 ZA = -D * 0.5f + D * static_cast<float32>( j ) / static_cast<float32>( NZ );
			const float32 ZB = -D * 0.5f + D * static_cast<float32>( j + 1 ) / static_cast<float32>( NZ );
			B.QuadPosY( XA, XB, 0.0f, ZA, ZB );
		}
	}
	return B.Mesh;
}

FEditMesh NEditMesh::MakeCylinder( float32 Radius, float32 Height, int32 Segments, bool bCaps )
{
	const float32 R = NMath::Max( Radius, MinLength ), H = NMath::Max( Height, MinLength );
	const int32 N = NMath::Clamp( Segments, 3, 256 );
	const auto Ring = [&]( int32 Step, float32 Y )
	{
		const float32 Angle = NMath::TWO_PI * static_cast<float32>( Step % N ) / static_cast<float32>( N );
		return FVector3( R * std::cos( Angle ), Y, R * std::sin( Angle ) );
	};
	FBuilder B;
	for ( int32 i = 0; i < N; ++i ) B.Face( { Ring( i, 0.0f ), Ring( i, H ), Ring( i + 1, H ), Ring( i + 1, 0.0f ) } );
	if ( bCaps )
	{
		TArray<FVector3> Top, Bottom;
		for ( int32 i = 0; i < N; ++i )
		{
			Top.Add( Ring( N - 1 - i, H ) );   // 上から見て時計回り（θ の逆）
			Bottom.Add( Ring( i, 0.0f ) );
		}
		B.Face( Top );
		B.Face( Bottom );
	}
	return B.Mesh;
}

FEditMesh NEditMesh::MakeSphere( float32 Radius, int32 Segments, int32 Rings )
{
	const float32 R = NMath::Max( Radius, MinLength );
	const int32 N = NMath::Clamp( Segments, 3, 256 ), M = NMath::Clamp( Rings, 2, 256 );
	const FVector3 Top( 0.0f, 2.0f * R, 0.0f ), Bottom( 0.0f, 0.0f, 0.0f );
	const auto Point = [&]( int32 Ring, int32 Step )
	{
		const float32 Polar = NMath::PI * static_cast<float32>( Ring ) / static_cast<float32>( M );
		const float32 Angle = NMath::TWO_PI * static_cast<float32>( Step % N ) / static_cast<float32>( N );
		return FVector3( R * std::sin( Polar ) * std::cos( Angle ), R + R * std::cos( Polar ), R * std::sin( Polar ) * std::sin( Angle ) );
	};
	FBuilder B;
	for ( int32 s = 0; s < N; ++s ) B.Face( { Top, Point( 1, s + 1 ), Point( 1, s ) } );
	for ( int32 r = 1; r + 1 < M; ++r )
	{
		for ( int32 s = 0; s < N; ++s ) B.Face( { Point( r + 1, s ), Point( r, s ), Point( r, s + 1 ), Point( r + 1, s + 1 ) } );
	}
	for ( int32 s = 0; s < N; ++s ) B.Face( { Bottom, Point( M - 1, s ), Point( M - 1, s + 1 ) } );
	return B.Mesh;
}

FEditMesh NEditMesh::MakeRamp( const FVector3& Size )
{
	const FVector3 S = Clamp( Size );
	const float32 X0 = -S.x * 0.5f, X1 = S.x * 0.5f, Z0 = -S.z * 0.5f, Z1 = S.z * 0.5f;
	const FVector3 P0( X0, 0.0f, Z0 ), P1( X1, 0.0f, Z0 ), P2( X1, 0.0f, Z1 ), P3( X0, 0.0f, Z1 ), P4( X1, S.y, Z1 ), P5( X0, S.y, Z1 );
	FBuilder B;
	B.Face( { P0, P1, P2, P3 } );   // 底
	B.Face( { P0, P5, P4, P1 } );   // 斜面
	B.Face( { P3, P2, P4, P5 } );   // 奥の壁
	B.Face( { P1, P4, P2 } );       // +X
	B.Face( { P0, P3, P5 } );       // -X
	return B.Mesh;
}

FEditMesh NEditMesh::MakeStairs( const FVector3& Size, int32 Steps )
{
	const FVector3 S = Clamp( Size );
	const int32 N = NMath::Clamp( Steps, 1, 128 );
	const float32 X0 = -S.x * 0.5f, X1 = S.x * 0.5f, Z0 = -S.z * 0.5f, Z1 = S.z * 0.5f;
	const float32 StepH = S.y / static_cast<float32>( N ), StepD = S.z / static_cast<float32>( N );
	FBuilder B;
	for ( int32 k = 0; k < N; ++k )
	{
		const float32 Low = StepH * static_cast<float32>( k ), High = StepH * static_cast<float32>( k + 1 );
		const float32 ZA = Z0 + StepD * static_cast<float32>( k ), ZB = Z0 + StepD * static_cast<float32>( k + 1 );
		B.QuadPosY( X0, X1, High, ZA, ZB );   // 踏み面
		B.QuadNegZ( ZA, X0, X1, Low, High );  // 蹴上げ
		B.QuadPosX( X1, 0.0f, High, ZA, ZB );
		B.QuadNegX( X0, 0.0f, High, ZA, ZB );
	}
	B.QuadPosZ( Z1, X0, X1, 0.0f, S.y );
	B.QuadNegY( X0, X1, 0.0f, Z0, Z1 );
	return B.Mesh;
}

FEditMesh NEditMesh::MakePrimitive( EEditMeshPrimitive Kind )
{
	switch ( Kind )
	{
	case EEditMeshPrimitive::Box: return MakeBox( FVector3( 2.0f, 2.0f, 2.0f ) );
	case EEditMeshPrimitive::Plane: return MakePlane( 4.0f, 4.0f, 1, 1 );
	case EEditMeshPrimitive::Cylinder: return MakeCylinder( 1.0f, 2.0f, 16, true );
	case EEditMeshPrimitive::Sphere: return MakeSphere( 1.0f, 16, 8 );
	case EEditMeshPrimitive::Ramp: return MakeRamp( FVector3( 2.0f, 1.0f, 3.0f ) );
	case EEditMeshPrimitive::Stairs: return MakeStairs( FVector3( 2.0f, 2.0f, 3.0f ), 5 );
	}
	return MakeBox( FVector3( 2.0f, 2.0f, 2.0f ) );
}
