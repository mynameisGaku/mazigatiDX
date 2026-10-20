#include "CollisionShape.h"


TResult<FCollisionShape> FCollisionShape::ConvexHull( TArrayView<const FVector3> Points )
{
	if ( Points.Num() < 4 ) return TResult<FCollisionShape>::Err( "ConvexHull: at least 4 points are required" );

	JPH::Array<PhysVec3> PhysPoints;
	PhysPoints.reserve( static_cast<usize>( Points.Num() ) );
	for ( const FVector3& P : Points ) PhysPoints.push_back( NPhysics::ToPhys( P ) );

	JPH::ConvexHullShapeSettings Settings( PhysPoints );
	return FromShapeResult( Settings.Create(), "ConvexHull" );
}

TResult<FCollisionShape> FCollisionShape::Mesh( TArrayView<const FVector3> Vertices, TArrayView<const uint32> Indices )
{
	if ( Vertices.IsEmpty() || Indices.IsEmpty() || Indices.Num() % 3 != 0 )
	{
		return TResult<FCollisionShape>::Err( "Mesh: index count must be a non-zero multiple of 3" );
	}

	JPH::VertexList PhysVertices;
	PhysVertices.reserve( static_cast<usize>( Vertices.Num() ) );
	for ( const FVector3& V : Vertices ) PhysVertices.push_back( JPH::Float3( V.x, V.y, V.z ) );

	JPH::IndexedTriangleList Triangles;
	Triangles.reserve( static_cast<usize>( Indices.Num() / 3 ) );
	const uint32 VertexCount = static_cast<uint32>( Vertices.Num() );
	for ( int32 i = 0; i < Indices.Num(); i += 3 )
	{
		if ( Indices[i] >= VertexCount || Indices[i + 1] >= VertexCount || Indices[i + 2] >= VertexCount )
		{
			return TResult<FCollisionShape>::Err( "Mesh: index out of range" );
		}
		Triangles.push_back( JPH::IndexedTriangle( Indices[i], Indices[i + 1], Indices[i + 2], 0 ) );
	}

	JPH::MeshShapeSettings Settings( std::move( PhysVertices ), std::move( Triangles ) );
	return FromShapeResult( Settings.Create(), "Mesh" );
}
