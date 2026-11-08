// SPDX-License-Identifier: MIT
#include "MaterialPreviewMesh.h"

#include "Library/Core/Types/Numeric.h"
#include "Library/Math/MathConstants.h"
#include "Library/Math/MathUtils.h"

namespace
{
	FShaderVertex3D MakeVertex( const FVector3& Position, const FVector3& Normal, float32 U, float32 V )
	{
		FShaderVertex3D Vertex;
		Vertex.Position = Position;
		Vertex.Normal = Normal;
		Vertex.U = U;
		Vertex.V = V;
		return Vertex;
	}

	void AddQuad( FMaterialPreviewMesh& Mesh, const FVector3& Origin, const FVector3& AxisU, const FVector3& AxisV, const FVector3& Normal )
	{
		const uint16 Base = static_cast<uint16>( Mesh.Vertices.Num() );
		Mesh.Vertices.Add( MakeVertex( Origin, Normal, 0.0f, 0.0f ) );
		Mesh.Vertices.Add( MakeVertex( Origin + AxisU, Normal, 1.0f, 0.0f ) );
		Mesh.Vertices.Add( MakeVertex( Origin + AxisU + AxisV, Normal, 1.0f, 1.0f ) );
		Mesh.Vertices.Add( MakeVertex( Origin + AxisV, Normal, 0.0f, 1.0f ) );
		const uint16 Order[6] = { 0, 1, 2, 0, 2, 3 };
		for ( int32 Index = 0; Index < 6; ++Index )
		{
			Mesh.Indices.Add( static_cast<uint16>( Base + Order[Index] ) );
		}
	}
}

FMaterialPreviewMesh NMaterialPreview::MakeSphere( int32 Segments, int32 Rings )
{
	Segments = NMath::Max( 3, Segments );
	Rings = NMath::Max( 2, Rings );
	// 頂点の数が 65535 を超えないように
	while ( ( Segments + 1 ) * ( Rings + 1 ) > 65535 )
	{
		Segments = NMath::Max( 3, Segments / 2 );
		Rings = NMath::Max( 2, Rings / 2 );
	}
	FMaterialPreviewMesh Mesh;
	for ( int32 Ring = 0; Ring <= Rings; ++Ring )
	{
		const float32 V = static_cast<float32>( Ring ) / static_cast<float32>( Rings );
		const float32 Polar = V * NMath::PI;
		const float32 Y = NMath::Cos( Polar );
		const float32 Radius = NMath::Sin( Polar );
		for ( int32 Segment = 0; Segment <= Segments; ++Segment )
		{
			const float32 U = static_cast<float32>( Segment ) / static_cast<float32>( Segments );
			const float32 Azimuth = U * NMath::TWO_PI;
			const FVector3 Normal( Radius * NMath::Cos( Azimuth ), Y, Radius * NMath::Sin( Azimuth ) );
			Mesh.Vertices.Add( MakeVertex( Normal, Normal, U, V ) );
		}
	}
	const int32 Stride = Segments + 1;
	for ( int32 Ring = 0; Ring < Rings; ++Ring )
	{
		for ( int32 Segment = 0; Segment < Segments; ++Segment )
		{
			const uint16 A = static_cast<uint16>( Ring * Stride + Segment );
			const uint16 B = static_cast<uint16>( A + 1 );
			const uint16 C = static_cast<uint16>( A + Stride );
			const uint16 D = static_cast<uint16>( C + 1 );
			// 外側から見て、時計回り（シェーダーのサンプルと同じ。描くとき、両面を描く設定にしてもよい）
			Mesh.Indices.Add( A );
			Mesh.Indices.Add( B );
			Mesh.Indices.Add( C );
			Mesh.Indices.Add( B );
			Mesh.Indices.Add( D );
			Mesh.Indices.Add( C );
		}
	}
	return Mesh;
}

FMaterialPreviewMesh NMaterialPreview::MakeCube()
{
	FMaterialPreviewMesh Mesh;
	AddQuad( Mesh, FVector3( -1.0f, -1.0f, 1.0f ), FVector3( 2.0f, 0.0f, 0.0f ), FVector3( 0.0f, 2.0f, 0.0f ), FVector3( 0.0f, 0.0f, 1.0f ) );    // +Z
	AddQuad( Mesh, FVector3( 1.0f, -1.0f, -1.0f ), FVector3( -2.0f, 0.0f, 0.0f ), FVector3( 0.0f, 2.0f, 0.0f ), FVector3( 0.0f, 0.0f, -1.0f ) );  // -Z
	AddQuad( Mesh, FVector3( 1.0f, -1.0f, 1.0f ), FVector3( 0.0f, 0.0f, -2.0f ), FVector3( 0.0f, 2.0f, 0.0f ), FVector3( 1.0f, 0.0f, 0.0f ) );    // +X
	AddQuad( Mesh, FVector3( -1.0f, -1.0f, -1.0f ), FVector3( 0.0f, 0.0f, 2.0f ), FVector3( 0.0f, 2.0f, 0.0f ), FVector3( -1.0f, 0.0f, 0.0f ) );  // -X
	AddQuad( Mesh, FVector3( -1.0f, 1.0f, 1.0f ), FVector3( 2.0f, 0.0f, 0.0f ), FVector3( 0.0f, 0.0f, -2.0f ), FVector3( 0.0f, 1.0f, 0.0f ) );    // +Y
	AddQuad( Mesh, FVector3( -1.0f, -1.0f, -1.0f ), FVector3( 2.0f, 0.0f, 0.0f ), FVector3( 0.0f, 0.0f, 2.0f ), FVector3( 0.0f, -1.0f, 0.0f ) );  // -Y
	return Mesh;
}

FMaterialPreviewMesh NMaterialPreview::MakePlane()
{
	FMaterialPreviewMesh Mesh;
	AddQuad( Mesh, FVector3( -1.0f, 0.0f, 1.0f ), FVector3( 2.0f, 0.0f, 0.0f ), FVector3( 0.0f, 0.0f, -2.0f ), FVector3( 0.0f, 1.0f, 0.0f ) );
	return Mesh;
}
