#include "ShadersMesh.h"

#include "Library/Math/Math.h"

namespace
{
	constexpr int32 SphereSegments = 24;
	constexpr int32 SphereRings = 16;

	NShadersMesh::FMesh MakeSphere()
	{
		NShadersMesh::FMesh Mesh;
		for ( int32 Ring = 0; Ring <= SphereRings; ++Ring )
		{
			const float32 Theta = NMath::PI * static_cast<float32>( Ring ) / static_cast<float32>( SphereRings );
			for ( int32 Segment = 0; Segment <= SphereSegments; ++Segment )
			{
				const float32 Phi = 2.0f * NMath::PI * static_cast<float32>( Segment ) / static_cast<float32>( SphereSegments );
				const FVector3 Point( NMath::Sin( Theta ) * NMath::Cos( Phi ), NMath::Cos( Theta ), NMath::Sin( Theta ) * NMath::Sin( Phi ) );
				Mesh.Vertices.Add( FShaderVertex3D::Make( Point, Point, FColor32( 255, 255, 255, 255 ), static_cast<float32>( Segment ) / static_cast<float32>( SphereSegments ), static_cast<float32>( Ring ) / static_cast<float32>( SphereRings ) ) );
			}
		}
		const int32 Stride = SphereSegments + 1;
		for ( int32 Ring = 0; Ring < SphereRings; ++Ring )
		{
			for ( int32 Segment = 0; Segment < SphereSegments; ++Segment )
			{
				const uint16 TopLeft = static_cast<uint16>( Ring * Stride + Segment );
				const uint16 TopRight = static_cast<uint16>( TopLeft + 1 );
				const uint16 BottomLeft = static_cast<uint16>( TopLeft + Stride );
				const uint16 BottomRight = static_cast<uint16>( BottomLeft + 1 );
				Mesh.Indices.Add( TopLeft );
				Mesh.Indices.Add( BottomLeft );
				Mesh.Indices.Add( TopRight );
				Mesh.Indices.Add( TopRight );
				Mesh.Indices.Add( BottomLeft );
				Mesh.Indices.Add( BottomRight );
			}
		}
		return Mesh;
	}

	NShadersMesh::FMesh MakeCube()
	{
		NShadersMesh::FMesh Mesh;
		// 面の向き（法線）と、面の中の 2 つの軸
		const FVector3 Normals[6] = { FVector3( 1, 0, 0 ), FVector3( -1, 0, 0 ), FVector3( 0, 1, 0 ), FVector3( 0, -1, 0 ), FVector3( 0, 0, 1 ), FVector3( 0, 0, -1 ) };
		const FVector3 AxesU[6] = { FVector3( 0, 1, 0 ), FVector3( 0, 1, 0 ), FVector3( 1, 0, 0 ), FVector3( 1, 0, 0 ), FVector3( 1, 0, 0 ), FVector3( 1, 0, 0 ) };
		const FVector3 AxesV[6] = { FVector3( 0, 0, 1 ), FVector3( 0, 0, 1 ), FVector3( 0, 0, 1 ), FVector3( 0, 0, 1 ), FVector3( 0, 1, 0 ), FVector3( 0, 1, 0 ) };
		const float32 CornerU[4] = { -0.5f, 0.5f, -0.5f, 0.5f };
		const float32 CornerV[4] = { -0.5f, -0.5f, 0.5f, 0.5f };
		for ( int32 Face = 0; Face < 6; ++Face )
		{
			const uint16 First = static_cast<uint16>( Mesh.Vertices.Num() );
			for ( int32 Corner = 0; Corner < 4; ++Corner )
			{
				const FVector3 Point = Normals[Face] * 0.5f + AxesU[Face] * CornerU[Corner] + AxesV[Face] * CornerV[Corner];
				Mesh.Vertices.Add( FShaderVertex3D::Make( Point, Normals[Face], FColor32( 255, 255, 255, 255 ), CornerU[Corner] + 0.5f, CornerV[Corner] + 0.5f ) );
			}
			Mesh.Indices.Add( First );
			Mesh.Indices.Add( static_cast<uint16>( First + 1 ) );
			Mesh.Indices.Add( static_cast<uint16>( First + 2 ) );
			Mesh.Indices.Add( static_cast<uint16>( First + 2 ) );
			Mesh.Indices.Add( static_cast<uint16>( First + 1 ) );
			Mesh.Indices.Add( static_cast<uint16>( First + 3 ) );
		}
		return Mesh;
	}

	NShadersMesh::FMesh MakeGround()
	{
		NShadersMesh::FMesh Mesh;
		const float32 Half = NShadersMesh::GroundHalfSize;
		const FVector3 Up( 0.0f, 1.0f, 0.0f );
		Mesh.Vertices.Add( FShaderVertex3D::Make( FVector3( -Half, 0.0f, -Half ), Up, FColor32( 255, 255, 255, 255 ), 0.0f, 0.0f ) );
		Mesh.Vertices.Add( FShaderVertex3D::Make( FVector3( Half, 0.0f, -Half ), Up, FColor32( 255, 255, 255, 255 ), 1.0f, 0.0f ) );
		Mesh.Vertices.Add( FShaderVertex3D::Make( FVector3( -Half, 0.0f, Half ), Up, FColor32( 255, 255, 255, 255 ), 0.0f, 1.0f ) );
		Mesh.Vertices.Add( FShaderVertex3D::Make( FVector3( Half, 0.0f, Half ), Up, FColor32( 255, 255, 255, 255 ), 1.0f, 1.0f ) );
		const uint16 Indices[6] = { 0, 1, 2, 2, 1, 3 };
		for ( const uint16 Index : Indices )
		{
			Mesh.Indices.Add( Index );
		}
		return Mesh;
	}
}

const NShadersMesh::FMesh& NShadersMesh::GetSphere()
{
	static const FMesh Mesh = MakeSphere();
	return Mesh;
}

const NShadersMesh::FMesh& NShadersMesh::GetCube()
{
	static const FMesh Mesh = MakeCube();
	return Mesh;
}

const NShadersMesh::FMesh& NShadersMesh::GetGround()
{
	static const FMesh Mesh = MakeGround();
	return Mesh;
}
