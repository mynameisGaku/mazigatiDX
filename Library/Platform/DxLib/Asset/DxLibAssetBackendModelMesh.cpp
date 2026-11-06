// SPDX-License-Identifier: MIT
#include "DxLibAssetBackendSubsystem.h"

#include "Library/Platform/DxLib/DxLibInclude.h"
#include "Library/Platform/DxLib/DxLibTypes.h"
#include "Library/Core/Types/Numeric.h"

//=============================================================================
// CDxLibAssetBackendSubsystem のうち、メッシュの中身（頂点と三角形）を読む部分（MV1 の参照用メッシュ）
//   MV1SetupReferenceMesh で作った参照用メッシュを写して、すぐに MV1TerminateReferenceMesh で捨てる（DxLib に持たせたままにしない）。
//=============================================================================
namespace
{
	// 参照用メッシュを作ったら、どの道を通っても捨てる
	struct FReferenceMeshLifetime
	{
		int32 Model = -1;
		int32 Transform = FALSE;
		bool bActive = false;

		FReferenceMeshLifetime( int32 InModel, int32 InTransform )
			: Model( InModel ), Transform( InTransform )
		{
			bActive = MV1SetupReferenceMesh( Model, -1, Transform, FALSE ) >= 0;
		}

		~FReferenceMeshLifetime()
		{
			if ( bActive )
			{
				MV1TerminateReferenceMesh( Model, -1, Transform, FALSE );
			}
		}
	};

	float32 ToUnit( uint8 Byte )
	{
		return static_cast<float32>( Byte ) / 255.0f;
	}
}

bool CDxLibAssetBackendSubsystem::GetModelMeshData( int32 Model, int32 Mesh, bool bCurrentPose, FModelMeshData& Out )
{
	if ( Model < 0 || CheckHandleASyncLoad( Model ) != FALSE )
	{
		return false;
	}
	const int32 MeshCount = MV1GetMeshNum( Model );
	if ( MeshCount <= 0 || Mesh < -1 || Mesh >= MeshCount )
	{
		return false;
	}

	const FReferenceMeshLifetime Lifetime( Model, bCurrentPose ? TRUE : FALSE );
	if ( !Lifetime.bActive )
	{
		return false;
	}
	const MV1_REF_POLYGONLIST Reference = MV1GetReferenceMesh( Model, -1, Lifetime.Transform, FALSE );
	if ( Reference.PolygonNum <= 0 || Reference.VertexNum <= 0 || Reference.Polygons == nullptr || Reference.Vertexs == nullptr )
	{
		return false;
	}

	// メッシュの指定があれば、そのメッシュの三角形が使う頂点だけを、詰めて写す（元の頂点の番号 -> 写した先の番号）
	TArray<int32> Remap;
	Remap.SetNum( Reference.VertexNum );
	for ( int32 Index = 0; Index < Reference.VertexNum; ++Index )
	{
		Remap[Index] = -1;
	}
	FModelMeshData Data;
	// 参照用メッシュの頂点 1 つを写して、写した先の番号を返す
	const auto AddVertex = [&]( int32 SourceIndex ) -> int32
	{
		const MV1_REF_VERTEX& SourceVertex = Reference.Vertexs[SourceIndex];
		FModelMeshVertex Vertex;
		Vertex.Position = NDxLibTypes::FromDxLib( SourceVertex.Position );
		Vertex.Normal = NDxLibTypes::FromDxLib( SourceVertex.Normal );
		Vertex.UV = NDxLibTypes::FromDxLib( SourceVertex.TexCoord[0] );
		Vertex.Color = FColor( ToUnit( SourceVertex.DiffuseColor.r ), ToUnit( SourceVertex.DiffuseColor.g ), ToUnit( SourceVertex.DiffuseColor.b ), ToUnit( SourceVertex.DiffuseColor.a ) );
		Vertex.DominantFrame = SourceVertex.MaxWeightFrameIndex >= 0 ? SourceVertex.MaxWeightFrameIndex : -1;
		Data.Vertices.Add( Vertex );
		return Data.Vertices.Num() - 1;
	};
	// 全部のときは、頂点を元の並びのまま写す
	if ( Mesh < 0 )
	{
		Data.Vertices.Reserve( Reference.VertexNum );
		for ( int32 Index = 0; Index < Reference.VertexNum; ++Index )
		{
			Remap[Index] = AddVertex( Index );
		}
	}
	Data.Triangles.Reserve( Reference.PolygonNum );
	for ( int32 Index = 0; Index < Reference.PolygonNum; ++Index )
	{
		const MV1_REF_POLYGON& Source = Reference.Polygons[Index];
		// フレームを指定しないので、番号はモデル全体の頂点の並びのもののはず。違えば、写さない
		if ( Source.VIndexTarget != 0 )
		{
			return false;
		}
		if ( Mesh >= 0 && Source.MeshIndex != Mesh )
		{
			continue;
		}
		FModelMeshTriangle Triangle;
		for ( int32 Corner = 0; Corner < 3; ++Corner )
		{
			const int32 SourceIndex = Source.VIndex[Corner];
			if ( SourceIndex < 0 || SourceIndex >= Reference.VertexNum )
			{
				return false;
			}
			if ( Remap[SourceIndex] < 0 )
			{
				Remap[SourceIndex] = AddVertex( SourceIndex );
			}
			Triangle.Vertices[Corner] = Remap[SourceIndex];
		}
		Triangle.Mesh = Source.MeshIndex;
		Triangle.Material = Source.MaterialIndex;
		Triangle.Frame = Source.FrameIndex;
		Data.Triangles.Add( Triangle );
	}
	if ( Data.Triangles.IsEmpty() )
	{
		return false;
	}
	if ( Mesh < 0 )
	{
		Data.BoundsMin = NDxLibTypes::FromDxLib( Reference.MinPosition );
		Data.BoundsMax = NDxLibTypes::FromDxLib( Reference.MaxPosition );
	}
	else
	{
		// 指定のメッシュだけの範囲
		Data.BoundsMin = Data.Vertices[0].Position;
		Data.BoundsMax = Data.Vertices[0].Position;
		for ( const FModelMeshVertex& Vertex : Data.Vertices )
		{
			Data.BoundsMin = FVector3( NMath::Min( Data.BoundsMin.x, Vertex.Position.x ), NMath::Min( Data.BoundsMin.y, Vertex.Position.y ), NMath::Min( Data.BoundsMin.z, Vertex.Position.z ) );
			Data.BoundsMax = FVector3( NMath::Max( Data.BoundsMax.x, Vertex.Position.x ), NMath::Max( Data.BoundsMax.y, Vertex.Position.y ), NMath::Max( Data.BoundsMax.z, Vertex.Position.z ) );
		}
	}
	Out = MoveTemp( Data );
	return true;
}
