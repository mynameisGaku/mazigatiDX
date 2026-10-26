#include "EditorMeshPicking.h"

#include <algorithm>
#include <cmath>

#include "Library/Core/Types/Numeric.h"

using NEditMesh::EEditMeshElement;

namespace
{
	// 光線（Origin + T * Direction）と三角形（両面）。T は、Direction の長さを単位にした位置
	bool RayTriangle( const FVector3& Origin, const FVector3& Direction, const FVector3& A, const FVector3& B, const FVector3& C, float32& OutT )
	{
		const FVector3 Edge1 = B - A, Edge2 = C - A;
		const FVector3 P = FVector3::Cross( Direction, Edge2 );
		const float32 Determinant = FVector3::Dot( Edge1, P );
		if ( NMath::Abs( Determinant ) < 1.0e-12f ) return false;
		const float32 InvDeterminant = 1.0f / Determinant;
		const FVector3 ToOrigin = Origin - A;
		const float32 U = FVector3::Dot( ToOrigin, P ) * InvDeterminant;
		if ( U < 0.0f || U > 1.0f ) return false;
		const FVector3 Q = FVector3::Cross( ToOrigin, Edge1 );
		const float32 V = FVector3::Dot( Direction, Q ) * InvDeterminant;
		if ( V < 0.0f || U + V > 1.0f ) return false;
		OutT = FVector3::Dot( Edge2, Q ) * InvDeterminant;
		return true;
	}

	// 画面の点 (X, Y) と、線分 (A, B)（画面の点）の距離
	float32 PointSegmentDistance( const FVector2& Point, const FVector2& A, const FVector2& B )
	{
		const FVector2 Segment = B - A;
		const float32 LengthSquared = Segment.x * Segment.x + Segment.y * Segment.y;
		float32 T = 0.0f;
		if ( LengthSquared > 1.0e-12f ) T = NMath::Clamp( ( ( Point.x - A.x ) * Segment.x + ( Point.y - A.y ) * Segment.y ) / LengthSquared, 0.0f, 1.0f );
		const FVector2 Closest( A.x + Segment.x * T, A.y + Segment.y * T );
		return std::hypot( Point.x - Closest.x, Point.y - Closest.y );
	}

	// 三角形のローカルの位置（メッシュと同じ番号）と、元の面。可視判定・面の選択で何度も使う
	struct FTriangles
	{
		TArray<uint32> Indices;
		TArray<int32> FaceOf;
	};

	FTriangles MakeTriangles( const FEditMesh& Mesh )
	{
		FTriangles Result;
		Result.Indices = Mesh.BuildTriangles( &Result.FaceOf );
		return Result;
	}

	bool IsVisible( const FEditMesh& Mesh, const FTriangles& Triangles, const FTransform& World, const FVector3& Eye, const FVector3& Point )
	{
		const FVector3 LocalEye = World.InverseTransformPoint( Eye );
		const FVector3 LocalPoint = World.InverseTransformPoint( Point );
		const FVector3 Direction = LocalPoint - LocalEye;
		for ( int32 i = 0; i + 2 < Triangles.Indices.Num(); i += 3 )
		{
			float32 T = 0.0f;
			if ( RayTriangle( LocalEye, Direction, Mesh.Vertices[static_cast<int32>( Triangles.Indices[i] )], Mesh.Vertices[static_cast<int32>( Triangles.Indices[i + 1] )], Mesh.Vertices[static_cast<int32>( Triangles.Indices[i + 2] )], T ) )
			{
				// 点に届く前（99.5% まで）に面があれば、隠れている。点を使う面は、点のところ（T = 1）で当たるので隠さない
				if ( T > 1.0e-4f && T < 0.995f ) return false;
			}
		}
		return true;
	}
}

bool NWorldEditor::IsMeshPointVisible( const FEditMesh& Mesh, const FTransform& World, const FVector3& Eye, const FVector3& Point )
{
	return IsVisible( Mesh, MakeTriangles( Mesh ), World, Eye, Point );
}

FEditMeshHit NWorldEditor::PickMeshElement( const FEditMesh& Mesh, const FTransform& World, const FEditorCamera& Camera, const FEditorViewport& Viewport, float32 X, float32 Y, EEditMeshElement Element, const FEditMeshPickOptions& Options )
{
	FEditMeshHit Hit;
	Hit.Element = Element;
	const FVector2 Mouse( X, Y );
	const FVector3 Eye = Camera.GetEye();
	FTriangles Triangles;
	if ( Options.bOnlyVisible || Element == EEditMeshElement::Face ) Triangles = MakeTriangles( Mesh );

	switch ( Element )
	{
	case EEditMeshElement::Vertex:
	{
		// 使っている頂点だけ。近い順に、隠れていないものを探す
		TArray<bool> Used;
		Used.Init( false, Mesh.GetNumVertices() );
		for ( int32 Face = 0; Face < Mesh.GetNumFaces(); ++Face )
		{
			if ( !Mesh.IsFaceValid( Face ) ) continue;
			for ( const int32 Index : Mesh.Faces[Face].Indices ) Used[Index] = true;
		}
		float32 Best = Options.VertexRadius;
		for ( int32 Vertex = 0; Vertex < Mesh.GetNumVertices(); ++Vertex )
		{
			if ( !Used[Vertex] ) continue;
			FVector2 Pixel;
			const FVector3 Point = World.TransformPoint( Mesh.Vertices[Vertex] );
			if ( !Camera.WorldToScreen( Viewport, Point, Pixel ) ) continue;
			const float32 Distance = std::hypot( Pixel.x - Mouse.x, Pixel.y - Mouse.y );
			if ( Distance >= Best ) continue;
			if ( Options.bOnlyVisible && !IsVisible( Mesh, Triangles, World, Eye, Point ) ) continue;
			Best = Distance;
			Hit.bHit = true;
			Hit.Index = Vertex;
			Hit.Distance = Distance;
		}
		break;
	}
	case EEditMeshElement::Edge:
	{
		float32 Best = Options.EdgeRadius;
		for ( const int64 Edge : NEditMesh::GetAllEdges( Mesh ) )
		{
			int32 A = 0, B = 0;
			NEditMesh::SplitEdgeKey( Edge, A, B );
			const FVector3 PointA = World.TransformPoint( Mesh.Vertices[A] ), PointB = World.TransformPoint( Mesh.Vertices[B] );
			FVector2 PixelA, PixelB;
			if ( !Camera.WorldToScreen( Viewport, PointA, PixelA ) || !Camera.WorldToScreen( Viewport, PointB, PixelB ) ) continue;
			const float32 Distance = PointSegmentDistance( Mouse, PixelA, PixelB );
			if ( Distance >= Best ) continue;
			if ( Options.bOnlyVisible && !IsVisible( Mesh, Triangles, World, Eye, ( PointA + PointB ) * 0.5f ) ) continue;
			Best = Distance;
			Hit.bHit = true;
			Hit.Edge = Edge;
			Hit.Distance = Distance;
		}
		break;
	}
	case EEditMeshElement::Face:
	{
		const FEditorRay Ray = Camera.ScreenToRay( Viewport, X, Y );
		const FVector3 LocalOrigin = World.InverseTransformPoint( Ray.Origin );
		const FVector3 LocalDirection = World.InverseTransformVector( Ray.Direction );
		float32 BestT = NMath::MaxFloat;
		for ( int32 i = 0; i + 2 < Triangles.Indices.Num(); i += 3 )
		{
			float32 T = 0.0f;
			if ( !RayTriangle( LocalOrigin, LocalDirection, Mesh.Vertices[static_cast<int32>( Triangles.Indices[i] )], Mesh.Vertices[static_cast<int32>( Triangles.Indices[i + 1] )], Mesh.Vertices[static_cast<int32>( Triangles.Indices[i + 2] )], T ) ) continue;
			if ( T <= 0.0f || T >= BestT ) continue;
			BestT = T;
			Hit.bHit = true;
			Hit.Index = Triangles.FaceOf[i / 3];
		}
		if ( Hit.bHit ) Hit.Distance = FVector3::Distance( Ray.Origin, World.TransformPoint( LocalOrigin + LocalDirection * BestT ) );
		break;
	}
	}
	return Hit;
}

FEditMeshSelection NWorldEditor::FindMeshElementsInRect( const FEditMesh& Mesh, const FTransform& World, const FEditorCamera& Camera, const FEditorViewport& Viewport, float32 X0, float32 Y0, float32 X1, float32 Y1, EEditMeshElement Element, bool bOnlyVisible )
{
	FEditMeshSelection Result;
	const float32 Left = NMath::Min( X0, X1 ), Right = NMath::Max( X0, X1 ), Top = NMath::Min( Y0, Y1 ), Bottom = NMath::Max( Y0, Y1 );
	const auto Inside = [&]( const FVector2& Pixel ) { return Pixel.x >= Left && Pixel.x <= Right && Pixel.y >= Top && Pixel.y <= Bottom; };
	const FVector3 Eye = Camera.GetEye();
	const FTriangles Triangles = bOnlyVisible ? MakeTriangles( Mesh ) : FTriangles();
	const auto Visible = [&]( const FVector3& Point ) { return !bOnlyVisible || IsVisible( Mesh, Triangles, World, Eye, Point ); };

	switch ( Element )
	{
	case EEditMeshElement::Vertex:
	{
		TArray<bool> Used;
		Used.Init( false, Mesh.GetNumVertices() );
		for ( int32 Face = 0; Face < Mesh.GetNumFaces(); ++Face )
		{
			if ( !Mesh.IsFaceValid( Face ) ) continue;
			for ( const int32 Index : Mesh.Faces[Face].Indices ) Used[Index] = true;
		}
		for ( int32 Vertex = 0; Vertex < Mesh.GetNumVertices(); ++Vertex )
		{
			if ( !Used[Vertex] ) continue;
			FVector2 Pixel;
			const FVector3 Point = World.TransformPoint( Mesh.Vertices[Vertex] );
			if ( Camera.WorldToScreen( Viewport, Point, Pixel ) && Inside( Pixel ) && Visible( Point ) ) Result.Vertices.Add( Vertex );
		}
		break;
	}
	case EEditMeshElement::Edge:
		for ( const int64 Edge : NEditMesh::GetAllEdges( Mesh ) )
		{
			int32 A = 0, B = 0;
			NEditMesh::SplitEdgeKey( Edge, A, B );
			const FVector3 PointA = World.TransformPoint( Mesh.Vertices[A] ), PointB = World.TransformPoint( Mesh.Vertices[B] );
			FVector2 PixelA, PixelB;
			if ( Camera.WorldToScreen( Viewport, PointA, PixelA ) && Camera.WorldToScreen( Viewport, PointB, PixelB ) && Inside( PixelA ) && Inside( PixelB ) && Visible( ( PointA + PointB ) * 0.5f ) ) Result.Edges.Add( Edge );
		}
		break;
	case EEditMeshElement::Face:
		for ( int32 Face = 0; Face < Mesh.GetNumFaces(); ++Face )
		{
			if ( !Mesh.IsFaceValid( Face ) ) continue;
			FVector2 Pixel;
			const FVector3 Point = World.TransformPoint( Mesh.ComputeFaceCenter( Face ) );
			if ( Camera.WorldToScreen( Viewport, Point, Pixel ) && Inside( Pixel ) && Visible( Point ) ) Result.Faces.Add( Face );
		}
		break;
	}
	return Result;
}
