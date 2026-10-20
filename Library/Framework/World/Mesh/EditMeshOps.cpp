#include "EditMeshOps.h"

#include <algorithm>
#include <cmath>

#include "Library/Core/Collection/Map.h"
#include "Library/Core/Collection/Set.h"

int64 NEditMesh::MakeEdgeKey( int32 A, int32 B )
{
	const int32 Low = std::min( A, B ), High = std::max( A, B );
	return ( static_cast<int64>( Low ) << 32 ) | static_cast<int64>( static_cast<uint32>( High ) );
}

void NEditMesh::SplitEdgeKey( int64 Key, int32& OutA, int32& OutB )
{
	OutA = static_cast<int32>( Key >> 32 );
	OutB = static_cast<int32>( static_cast<uint32>( Key & 0xFFFFFFFFll ) );
}

namespace
{
	template<typename T>
	int32 LowerBound( const TArray<T>& Set, T Value )
	{
		int32 Low = 0, High = Set.Num();
		while ( Low < High )
		{
			const int32 Mid = ( Low + High ) / 2;
			if ( Set[Mid] < Value ) Low = Mid + 1;
			else High = Mid;
		}
		return Low;
	}

	template<typename T>
	void AddSortedImpl( TArray<T>& Set, T Value )
	{
		const int32 Position = LowerBound( Set, Value );
		if ( Position < Set.Num() && Set[Position] == Value ) return;
		Set.Insert( Value, Position );
	}

	template<typename T>
	bool RemoveSortedImpl( TArray<T>& Set, T Value )
	{
		const int32 Position = LowerBound( Set, Value );
		if ( Position >= Set.Num() || Set[Position] != Value ) return false;
		Set.RemoveAt( Position );
		return true;
	}

	template<typename T>
	bool ContainsSortedImpl( const TArray<T>& Set, T Value )
	{
		const int32 Position = LowerBound( Set, Value );
		return Position < Set.Num() && Set[Position] == Value;
	}

	// 有効な面だけの、重複のない小さい順の一覧
	TArray<int32> ValidFaces( const FEditMesh& Mesh, const TArray<int32>& Faces )
	{
		TArray<int32> Result;
		for ( const int32 Face : Faces )
		{
			if ( Mesh.IsFaceValid( Face ) ) AddSortedImpl( Result, Face );
		}
		return Result;
	}

	TArray<int32> ValidVertices( const FEditMesh& Mesh, const TArray<int32>& Vertices )
	{
		TArray<int32> Result;
		for ( const int32 Vertex : Vertices )
		{
			if ( Mesh.Vertices.IsValidIndex( Vertex ) ) AddSortedImpl( Result, Vertex );
		}
		return Result;
	}

	int64 DirectedKey( int32 A, int32 B ) { return ( static_cast<int64>( A ) << 32 ) | static_cast<int64>( static_cast<uint32>( B ) ); }

	// 面積で重みを付けた法線（長さ = 面積の 2 倍）
	FVector3 AreaVector( const FEditMesh& Mesh, int32 Face )
	{
		const TArray<int32>& Indices = Mesh.Faces[Face].Indices;
		const FVector3& Origin = Mesh.Vertices[Indices[0]];
		FVector3 Sum = FVector3::Zero;
		for ( int32 i = 1; i + 1 < Indices.Num(); ++i ) Sum = Sum + FVector3::Cross( Mesh.Vertices[Indices[i]] - Origin, Mesh.Vertices[Indices[i + 1]] - Origin );
		return Sum;
	}

	// 頂点の統合（Parent は、頂点ごとの代表。代表の番号が小さくなるように）
	int32 FindRoot( TArray<int32>& Parent, int32 X )
	{
		while ( Parent[X] != X )
		{
			Parent[X] = Parent[Parent[X]];
			X = Parent[X];
		}
		return X;
	}

	void Unite( TArray<int32>& Parent, int32 A, int32 B )
	{
		A = FindRoot( Parent, A );
		B = FindRoot( Parent, B );
		if ( A == B ) return;
		if ( A < B ) Parent[B] = A;
		else Parent[A] = B;
	}

	// Parent に従って面の頂点を付け替え、つぶれた面を消し、使われない頂点を取り除く。OutNewIndex があれば、元の頂点 -> いまの頂点（消えたものは -1）
	void ApplyCollapse( FEditMesh& Mesh, TArray<int32>& Parent, TArray<int32>* OutNewIndex )
	{
		TArray<FEditMeshFace> Kept;
		for ( const FEditMeshFace& Face : Mesh.Faces )
		{
			TArray<int32> Loop;
			bool bBroken = false;
			for ( const int32 Index : Face.Indices )
			{
				if ( !Mesh.Vertices.IsValidIndex( Index ) )
				{
					bBroken = true;
					break;
				}
				const int32 Root = FindRoot( Parent, Index );
				if ( Loop.IsEmpty() || Loop.Last() != Root ) Loop.Add( Root );
			}
			if ( bBroken )
			{
				Kept.Add( Face );   // 壊れた面は、そのまま（ここでは直さない）
				continue;
			}
			if ( Loop.Num() > 1 && Loop[0] == Loop.Last() ) Loop.RemoveAt( Loop.Num() - 1 );
			bool bPinched = Loop.Num() < 3;
			for ( int32 i = 0; i < Loop.Num() && !bPinched; ++i )
			{
				for ( int32 j = 0; j < i; ++j ) bPinched = bPinched || Loop[i] == Loop[j];
			}
			if ( bPinched ) continue;
			FEditMeshFace Replaced = Face;
			Replaced.Indices = Loop;
			Kept.Add( Replaced );
		}
		Mesh.Faces = Kept;
		if ( OutNewIndex != nullptr )
		{
			OutNewIndex->Init( -1, Parent.Num() );
			// 後ろの詰め直し（RemoveUnusedVertices）の前に、元 -> 代表 を控え、あとで、代表 -> いまの番号に繋ぐ
			for ( int32 i = 0; i < Parent.Num(); ++i ) ( *OutNewIndex )[i] = FindRoot( Parent, i );
		}
	}
}

void NEditMesh::AddSorted( TArray<int32>& Set, int32 Value ) { AddSortedImpl( Set, Value ); }
void NEditMesh::AddSorted( TArray<int64>& Set, int64 Value ) { AddSortedImpl( Set, Value ); }
bool NEditMesh::RemoveSorted( TArray<int32>& Set, int32 Value ) { return RemoveSortedImpl( Set, Value ); }
bool NEditMesh::RemoveSorted( TArray<int64>& Set, int64 Value ) { return RemoveSortedImpl( Set, Value ); }
bool NEditMesh::ContainsSorted( const TArray<int32>& Set, int32 Value ) { return ContainsSortedImpl( Set, Value ); }
bool NEditMesh::ContainsSorted( const TArray<int64>& Set, int64 Value ) { return ContainsSortedImpl( Set, Value ); }

//=============================================================================
// 問い合わせ
//=============================================================================
void NEditMesh::CleanSelection( const FEditMesh& Mesh, FEditMeshSelection& Selection )
{
	TArray<int32> Vertices;
	for ( const int32 Vertex : Selection.Vertices )
	{
		if ( Mesh.Vertices.IsValidIndex( Vertex ) ) AddSortedImpl( Vertices, Vertex );
	}
	Selection.Vertices = Vertices;
	TArray<int32> Faces;
	for ( const int32 Face : Selection.Faces )
	{
		if ( Mesh.Faces.IsValidIndex( Face ) ) AddSortedImpl( Faces, Face );
	}
	Selection.Faces = Faces;
	if ( !Selection.Edges.IsEmpty() )
	{
		const TArray<int64> All = GetAllEdges( Mesh );
		TArray<int64> Edges;
		for ( const int64 Edge : Selection.Edges )
		{
			if ( ContainsSortedImpl( All, Edge ) ) AddSortedImpl( Edges, Edge );
		}
		Selection.Edges = Edges;
	}
}

FEditMeshSelection NEditMesh::ConvertSelection( const FEditMesh& Mesh, const FEditMeshSelection& Selection, EEditMeshElement To )
{
	const TArray<int32> Affected = GetAffectedVertices( Mesh, Selection );
	FEditMeshSelection Result;
	switch ( To )
	{
	case EEditMeshElement::Vertex:
		Result.Vertices = Affected;
		break;
	case EEditMeshElement::Edge:
		for ( const int64 Edge : GetAllEdges( Mesh ) )
		{
			int32 A = 0, B = 0;
			SplitEdgeKey( Edge, A, B );
			if ( ContainsSortedImpl( Affected, A ) && ContainsSortedImpl( Affected, B ) ) Result.Edges.Add( Edge );
		}
		break;
	case EEditMeshElement::Face:
		for ( int32 Face = 0; Face < Mesh.Faces.Num(); ++Face )
		{
			if ( !Mesh.IsFaceValid( Face ) ) continue;
			bool bAll = true;
			for ( const int32 Index : Mesh.Faces[Face].Indices ) bAll = bAll && ContainsSortedImpl( Affected, Index );
			if ( bAll ) Result.Faces.Add( Face );
		}
		break;
	}
	return Result;
}

TArray<int32> NEditMesh::GetAffectedVertices( const FEditMesh& Mesh, const FEditMeshSelection& Selection )
{
	TArray<int32> Result;
	for ( const int32 Vertex : Selection.Vertices )
	{
		if ( Mesh.Vertices.IsValidIndex( Vertex ) ) AddSortedImpl( Result, Vertex );
	}
	for ( const int64 Edge : Selection.Edges )
	{
		int32 A = 0, B = 0;
		SplitEdgeKey( Edge, A, B );
		if ( Mesh.Vertices.IsValidIndex( A ) ) AddSortedImpl( Result, A );
		if ( Mesh.Vertices.IsValidIndex( B ) ) AddSortedImpl( Result, B );
	}
	for ( const int32 Face : Selection.Faces )
	{
		if ( !Mesh.Faces.IsValidIndex( Face ) ) continue;
		for ( const int32 Vertex : Mesh.Faces[Face].Indices )
		{
			if ( Mesh.Vertices.IsValidIndex( Vertex ) ) AddSortedImpl( Result, Vertex );
		}
	}
	return Result;
}

FVector3 NEditMesh::ComputeSelectionCenter( const FEditMesh& Mesh, const FEditMeshSelection& Selection )
{
	const TArray<int32> Vertices = GetAffectedVertices( Mesh, Selection );
	if ( Vertices.IsEmpty() ) return FVector3::Zero;
	FVector3 Sum = FVector3::Zero;
	for ( const int32 Vertex : Vertices ) Sum = Sum + Mesh.Vertices[Vertex];
	return Sum / static_cast<float32>( Vertices.Num() );
}

FVector3 NEditMesh::ComputeSelectionNormal( const FEditMesh& Mesh, const FEditMeshSelection& Selection )
{
	FVector3 Sum = FVector3::Zero;
	for ( const int32 Face : Selection.Faces )
	{
		if ( Mesh.IsFaceValid( Face ) ) Sum = Sum + AreaVector( Mesh, Face );
	}
	const float32 Length = Sum.Length();
	return Length > 1.0e-9f ? Sum / Length : FVector3::Zero;
}

TArray<int64> NEditMesh::GetAllEdges( const FEditMesh& Mesh )
{
	TArray<int64> Result;
	for ( int32 Face = 0; Face < Mesh.Faces.Num(); ++Face )
	{
		if ( !Mesh.IsFaceValid( Face ) ) continue;
		const TArray<int32>& Indices = Mesh.Faces[Face].Indices;
		for ( int32 i = 0; i < Indices.Num(); ++i ) Result.Add( MakeEdgeKey( Indices[i], Indices[( i + 1 ) % Indices.Num()] ) );
	}
	Result.Sort();
	TArray<int64> Unique;
	for ( const int64 Edge : Result )
	{
		if ( Unique.IsEmpty() || Unique.Last() != Edge ) Unique.Add( Edge );
	}
	return Unique;
}

TArray<int32> NEditMesh::GetFacesUsingVertices( const FEditMesh& Mesh, const TArray<int32>& Vertices )
{
	TArray<int32> Result;
	TArray<int32> Sorted;
	for ( const int32 Vertex : Vertices ) AddSortedImpl( Sorted, Vertex );
	for ( int32 Face = 0; Face < Mesh.Faces.Num(); ++Face )
	{
		for ( const int32 Index : Mesh.Faces[Face].Indices )
		{
			if ( ContainsSortedImpl( Sorted, Index ) )
			{
				Result.Add( Face );
				break;
			}
		}
	}
	return Result;
}

TArray<int32> NEditMesh::GetFacesUsingEdge( const FEditMesh& Mesh, int64 Edge )
{
	TArray<int32> Result;
	for ( int32 Face = 0; Face < Mesh.Faces.Num(); ++Face )
	{
		const TArray<int32>& Indices = Mesh.Faces[Face].Indices;
		for ( int32 i = 0; i < Indices.Num(); ++i )
		{
			if ( MakeEdgeKey( Indices[i], Indices[( i + 1 ) % Indices.Num()] ) == Edge )
			{
				Result.Add( Face );
				break;
			}
		}
	}
	return Result;
}

TArray<int32> NEditMesh::GetLinkedFaces( const FEditMesh& Mesh, const TArray<int32>& Faces )
{
	// 辺 -> その辺を使う面
	TMap<int64, TArray<int32>> FacesOfEdge;
	for ( int32 Face = 0; Face < Mesh.Faces.Num(); ++Face )
	{
		if ( !Mesh.IsFaceValid( Face ) ) continue;
		const TArray<int32>& Indices = Mesh.Faces[Face].Indices;
		for ( int32 i = 0; i < Indices.Num(); ++i ) FacesOfEdge.FindOrAdd( MakeEdgeKey( Indices[i], Indices[( i + 1 ) % Indices.Num()] ) ).Add( Face );
	}
	TArray<int32> Result;
	TArray<int32> Queue;
	for ( const int32 Face : Faces )
	{
		if ( Mesh.IsFaceValid( Face ) && !ContainsSortedImpl( Result, Face ) )
		{
			AddSortedImpl( Result, Face );
			Queue.Add( Face );
		}
	}
	for ( int32 Head = 0; Head < Queue.Num(); ++Head )
	{
		const TArray<int32>& Indices = Mesh.Faces[Queue[Head]].Indices;
		for ( int32 i = 0; i < Indices.Num(); ++i )
		{
			const TArray<int32>* Neighbors = FacesOfEdge.Find( MakeEdgeKey( Indices[i], Indices[( i + 1 ) % Indices.Num()] ) );
			if ( Neighbors == nullptr ) continue;
			for ( const int32 Neighbor : *Neighbors )
			{
				if ( ContainsSortedImpl( Result, Neighbor ) ) continue;
				AddSortedImpl( Result, Neighbor );
				Queue.Add( Neighbor );
			}
		}
	}
	return Result;
}

int32 NEditMesh::CountOpenEdges( const FEditMesh& Mesh )
{
	TMap<int64, int32> Counts;
	for ( int32 Face = 0; Face < Mesh.Faces.Num(); ++Face )
	{
		if ( !Mesh.IsFaceValid( Face ) ) continue;
		const TArray<int32>& Indices = Mesh.Faces[Face].Indices;
		for ( int32 i = 0; i < Indices.Num(); ++i ) ++Counts.FindOrAdd( MakeEdgeKey( Indices[i], Indices[( i + 1 ) % Indices.Num()] ) );
	}
	int32 NumOpen = 0;
	for ( const auto& Pair : Counts ) NumOpen += Pair.Value == 1 ? 1 : 0;
	return NumOpen;
}

//=============================================================================
// 形を変える
//=============================================================================
FEditMeshSelection NEditMesh::ExtrudeFaces( FEditMesh& Mesh, const TArray<int32>& Faces, float32 Distance, bool bIndividual )
{
	FEditMeshSelection Result;
	const TArray<int32> Selected = ValidFaces( Mesh, Faces );
	if ( Selected.IsEmpty() ) return Result;
	Result.Faces = Selected;
	TArray<FEditMeshFace> Walls;
	TArray<int32> NewVertices;

	const auto AddWall = [&]( int32 A, int32 B, int32 NewA, int32 NewB, int32 Material )
	{
		FEditMeshFace Wall;
		Wall.Indices.Add( NewB );
		Wall.Indices.Add( NewA );
		Wall.Indices.Add( A );
		Wall.Indices.Add( B );
		Wall.Material = Material;
		Walls.Add( Wall );
	};

	if ( bIndividual )
	{
		for ( const int32 Face : Selected )
		{
			const TArray<int32> Old = Mesh.Faces[Face].Indices;
			const FVector3 Normal = Mesh.ComputeFaceNormal( Face );
			TArray<int32> Fresh;
			for ( const int32 Index : Old )
			{
				const int32 Added = Mesh.Vertices.Add( Mesh.Vertices[Index] + Normal * Distance );
				Fresh.Add( Added );
				NewVertices.Add( Added );
			}
			for ( int32 i = 0; i < Old.Num(); ++i )
			{
				const int32 j = ( i + 1 ) % Old.Num();
				AddWall( Old[i], Old[j], Fresh[i], Fresh[j], Mesh.Faces[Face].Material );
			}
			Mesh.Faces[Face].Indices = Fresh;
		}
	}
	else
	{
		// 選んだ面の中で、向きのある辺（A -> B）が、逆向き（B -> A）の相手を持たないものが、塊の縁
		TSet<int64> Directed;
		TMap<int32, FVector3> NormalSum;
		for ( const int32 Face : Selected )
		{
			const TArray<int32>& Indices = Mesh.Faces[Face].Indices;
			const FVector3 Area = AreaVector( Mesh, Face );
			for ( int32 i = 0; i < Indices.Num(); ++i )
			{
				Directed.Add( DirectedKey( Indices[i], Indices[( i + 1 ) % Indices.Num()] ) );
				NormalSum.FindOrAdd( Indices[i] ) = NormalSum.FindOrAdd( Indices[i] ) + Area;
			}
		}
		TMap<int32, int32> NewIndex;
		for ( const int32 Face : Selected )
		{
			for ( const int32 Index : Mesh.Faces[Face].Indices )
			{
				if ( NewIndex.Find( Index ) != nullptr ) continue;
				const FVector3 Sum = *NormalSum.Find( Index );
				const float32 Length = Sum.Length();
				const FVector3 Offset = Length > 1.0e-9f ? Sum / Length * Distance : FVector3::Zero;
				const int32 Added = Mesh.Vertices.Add( Mesh.Vertices[Index] + Offset );
				NewIndex.Add( Index, Added );
				NewVertices.Add( Added );
			}
		}
		for ( const int32 Face : Selected )
		{
			const TArray<int32>& Indices = Mesh.Faces[Face].Indices;
			for ( int32 i = 0; i < Indices.Num(); ++i )
			{
				const int32 A = Indices[i], B = Indices[( i + 1 ) % Indices.Num()];
				if ( !Directed.Contains( DirectedKey( B, A ) ) ) AddWall( A, B, *NewIndex.Find( A ), *NewIndex.Find( B ), Mesh.Faces[Face].Material );
			}
		}
		for ( const int32 Face : Selected )
		{
			for ( int32& Index : Mesh.Faces[Face].Indices ) Index = *NewIndex.Find( Index );
		}
	}
	for ( const FEditMeshFace& Wall : Walls ) Mesh.Faces.Add( Wall );
	NewVertices.Sort();
	Result.Vertices = NewVertices;
	return Result;
}

FEditMeshSelection NEditMesh::InsetFaces( FEditMesh& Mesh, const TArray<int32>& Faces, float32 Amount )
{
	const float32 Shrink = std::clamp( Amount, 0.0f, 0.95f );
	FEditMeshSelection Result = ExtrudeFaces( Mesh, Faces, 0.0f, true );
	for ( const int32 Face : Result.Faces )
	{
		const FVector3 Center = Mesh.ComputeFaceCenter( Face );
		for ( const int32 Index : Mesh.Faces[Face].Indices ) Mesh.Vertices[Index] = Center + ( Mesh.Vertices[Index] - Center ) * ( 1.0f - Shrink );
	}
	return Result;
}

int32 NEditMesh::RemoveUnusedVertices( FEditMesh& Mesh )
{
	TArray<bool> Used;
	Used.Init( false, Mesh.Vertices.Num() );
	for ( const FEditMeshFace& Face : Mesh.Faces )
	{
		for ( const int32 Index : Face.Indices )
		{
			if ( Mesh.Vertices.IsValidIndex( Index ) ) Used[Index] = true;
		}
	}
	TArray<int32> NewIndex;
	NewIndex.Init( -1, Mesh.Vertices.Num() );
	TArray<FVector3> Kept;
	for ( int32 i = 0; i < Mesh.Vertices.Num(); ++i )
	{
		if ( !Used[i] ) continue;
		NewIndex[i] = Kept.Num();
		Kept.Add( Mesh.Vertices[i] );
	}
	const int32 NumRemoved = Mesh.Vertices.Num() - Kept.Num();
	if ( NumRemoved == 0 ) return 0;
	for ( FEditMeshFace& Face : Mesh.Faces )
	{
		for ( int32& Index : Face.Indices )
		{
			if ( Mesh.Vertices.IsValidIndex( Index ) ) Index = NewIndex[Index];
		}
	}
	Mesh.Vertices = Kept;
	return NumRemoved;
}

int32 NEditMesh::RemoveInvalidFaces( FEditMesh& Mesh )
{
	TArray<FEditMeshFace> Kept;
	for ( int32 Face = 0; Face < Mesh.Faces.Num(); ++Face )
	{
		if ( Mesh.IsFaceValid( Face ) ) Kept.Add( Mesh.Faces[Face] );
	}
	const int32 NumRemoved = Mesh.Faces.Num() - Kept.Num();
	Mesh.Faces = Kept;
	return NumRemoved;
}

int32 NEditMesh::DeleteFaces( FEditMesh& Mesh, const TArray<int32>& Faces )
{
	TArray<int32> Doomed;
	for ( const int32 Face : Faces )
	{
		if ( Mesh.Faces.IsValidIndex( Face ) ) AddSortedImpl( Doomed, Face );
	}
	if ( Doomed.IsEmpty() ) return 0;
	TArray<FEditMeshFace> Kept;
	for ( int32 Face = 0; Face < Mesh.Faces.Num(); ++Face )
	{
		if ( !ContainsSortedImpl( Doomed, Face ) ) Kept.Add( Mesh.Faces[Face] );
	}
	Mesh.Faces = Kept;
	RemoveUnusedVertices( Mesh );
	return Doomed.Num();
}

int32 NEditMesh::DeleteVertices( FEditMesh& Mesh, const TArray<int32>& Vertices )
{
	const TArray<int32> Valid = ValidVertices( Mesh, Vertices );
	if ( Valid.IsEmpty() ) return 0;
	const TArray<int32> Doomed = GetFacesUsingVertices( Mesh, Valid );
	const int32 NumDeleted = DeleteFaces( Mesh, Doomed );
	if ( NumDeleted == 0 ) RemoveUnusedVertices( Mesh );   // どの面にも使われていない頂点
	return NumDeleted;
}

void NEditMesh::FlipFaces( FEditMesh& Mesh, const TArray<int32>& Faces )
{
	for ( const int32 Face : ValidFaces( Mesh, Faces ) )
	{
		TArray<int32>& Indices = Mesh.Faces[Face].Indices;
		for ( int32 i = 0, j = Indices.Num() - 1; i < j; ++i, --j ) std::swap( Indices[i], Indices[j] );
	}
}

int32 NEditMesh::MergeVertices( FEditMesh& Mesh, const TArray<int32>& Vertices )
{
	const TArray<int32> Valid = ValidVertices( Mesh, Vertices );
	if ( Valid.Num() < 2 ) return -1;
	FVector3 Sum = FVector3::Zero;
	for ( const int32 Vertex : Valid ) Sum = Sum + Mesh.Vertices[Vertex];
	TArray<int32> Parent;
	for ( int32 i = 0; i < Mesh.Vertices.Num(); ++i ) Parent.Add( i );
	for ( int32 i = 1; i < Valid.Num(); ++i ) Unite( Parent, Valid[0], Valid[i] );
	Mesh.Vertices[Valid[0]] = Sum / static_cast<float32>( Valid.Num() );

	TArray<int32> Roots;
	ApplyCollapse( Mesh, Parent, &Roots );
	// 詰める前の代表の番号 -> 詰めたあとの番号
	TArray<bool> Used;
	Used.Init( false, Mesh.Vertices.Num() );
	for ( const FEditMeshFace& Face : Mesh.Faces )
	{
		for ( const int32 Index : Face.Indices ) Used[Index] = true;
	}
	int32 NewRoot = -1, Count = 0;
	for ( int32 i = 0; i < Mesh.Vertices.Num(); ++i )
	{
		if ( !Used[i] ) continue;
		if ( i == Roots[Valid[0]] ) NewRoot = Count;
		++Count;
	}
	RemoveUnusedVertices( Mesh );
	return NewRoot;
}

int32 NEditMesh::WeldVertices( FEditMesh& Mesh, const TArray<int32>& Vertices, float32 Distance )
{
	const float32 Reach = std::max( Distance, 1.0e-6f );
	TArray<int32> Candidates = Vertices.IsEmpty() ? TArray<int32>() : ValidVertices( Mesh, Vertices );
	if ( Vertices.IsEmpty() )
	{
		for ( int32 i = 0; i < Mesh.Vertices.Num(); ++i ) Candidates.Add( i );
	}
	TArray<int32> Parent;
	for ( int32 i = 0; i < Mesh.Vertices.Num(); ++i ) Parent.Add( i );

	const auto CellOf = [&]( float32 Value ) { return static_cast<int64>( std::floor( Value / Reach ) ); };
	const auto KeyOf = [&]( int64 X, int64 Y, int64 Z ) { return ( X * 73856093ll ) ^ ( Y * 19349663ll ) ^ ( Z * 83492791ll ); };
	TMap<int64, TArray<int32>> Grid;
	const float32 ReachSquared = Reach * Reach;
	for ( const int32 Vertex : Candidates )
	{
		const FVector3& P = Mesh.Vertices[Vertex];
		const int64 CX = CellOf( P.x ), CY = CellOf( P.y ), CZ = CellOf( P.z );
		for ( int64 DX = -1; DX <= 1; ++DX )
		{
			for ( int64 DY = -1; DY <= 1; ++DY )
			{
				for ( int64 DZ = -1; DZ <= 1; ++DZ )
				{
					const TArray<int32>* Cell = Grid.Find( KeyOf( CX + DX, CY + DY, CZ + DZ ) );
					if ( Cell == nullptr ) continue;
					for ( const int32 Other : *Cell )
					{
						if ( FVector3::DistanceSquared( Mesh.Vertices[Other], P ) <= ReachSquared ) Unite( Parent, Other, Vertex );
					}
				}
			}
		}
		Grid.FindOrAdd( KeyOf( CX, CY, CZ ) ).Add( Vertex );
	}
	const int32 Before = Mesh.Vertices.Num();
	ApplyCollapse( Mesh, Parent, nullptr );
	RemoveUnusedVertices( Mesh );
	return Before - Mesh.Vertices.Num();
}

int32 NEditMesh::SubdivideFaces( FEditMesh& Mesh, const TArray<int32>& Faces )
{
	const TArray<int32> Selected = ValidFaces( Mesh, Faces );
	if ( Selected.IsEmpty() ) return 0;
	const int32 NumBefore = Mesh.Faces.Num();

	TMap<int64, int32> Middle;   // 辺 -> 辺の真ん中の頂点
	for ( const int32 Face : Selected )
	{
		const TArray<int32>& Indices = Mesh.Faces[Face].Indices;
		for ( int32 i = 0; i < Indices.Num(); ++i )
		{
			const int64 Key = MakeEdgeKey( Indices[i], Indices[( i + 1 ) % Indices.Num()] );
			if ( Middle.Find( Key ) == nullptr ) Middle.Add( Key, Mesh.Vertices.Add( ( Mesh.Vertices[Indices[i]] + Mesh.Vertices[Indices[( i + 1 ) % Indices.Num()]] ) * 0.5f ) );
		}
	}

	// 隣の面（選んでいない面）にも、辺の真ん中の頂点を足す（隙間ができないように）
	for ( int32 Face = 0; Face < NumBefore; ++Face )
	{
		if ( ContainsSortedImpl( Selected, Face ) ) continue;
		const TArray<int32> Old = Mesh.Faces[Face].Indices;
		bool bInRange = true;
		for ( const int32 Index : Old ) bInRange = bInRange && Mesh.Vertices.IsValidIndex( Index );
		if ( !bInRange || Old.Num() < 3 ) continue;
		TArray<int32> Loop;
		for ( int32 i = 0; i < Old.Num(); ++i )
		{
			Loop.Add( Old[i] );
			if ( const int32* Mid = Middle.Find( MakeEdgeKey( Old[i], Old[( i + 1 ) % Old.Num()] ) ) ) Loop.Add( *Mid );
		}
		Mesh.Faces[Face].Indices = Loop;
	}

	// 選んだ面: 角ごとの四角形（辺の真ん中・角・次の辺の真ん中・面の中心）
	for ( const int32 Face : Selected )
	{
		const TArray<int32> Old = Mesh.Faces[Face].Indices;
		const int32 Material = Mesh.Faces[Face].Material;
		FVector3 Sum = FVector3::Zero;
		for ( const int32 Index : Old ) Sum = Sum + Mesh.Vertices[Index];
		const int32 Center = Mesh.Vertices.Add( Sum / static_cast<float32>( Old.Num() ) );
		for ( int32 i = 0; i < Old.Num(); ++i )
		{
			const int32 Previous = Old[( i + Old.Num() - 1 ) % Old.Num()], Next = Old[( i + 1 ) % Old.Num()];
			FEditMeshFace Quad;
			Quad.Material = Material;
			Quad.Indices.Add( *Middle.Find( MakeEdgeKey( Previous, Old[i] ) ) );
			Quad.Indices.Add( Old[i] );
			Quad.Indices.Add( *Middle.Find( MakeEdgeKey( Old[i], Next ) ) );
			Quad.Indices.Add( Center );
			if ( i == 0 ) Mesh.Faces[Face] = Quad;
			else Mesh.Faces.Add( Quad );
		}
	}
	return Mesh.Faces.Num() - NumBefore;
}

int32 NEditMesh::TriangulateFaces( FEditMesh& Mesh, const TArray<int32>& Faces )
{
	const int32 NumBefore = Mesh.Faces.Num();
	for ( const int32 Face : ValidFaces( Mesh, Faces ) )
	{
		const TArray<int32> Old = Mesh.Faces[Face].Indices;
		if ( Old.Num() <= 3 ) continue;
		const int32 Material = Mesh.Faces[Face].Material;
		for ( int32 i = 1; i + 1 < Old.Num(); ++i )
		{
			FEditMeshFace Triangle;
			Triangle.Material = Material;
			Triangle.Indices.Add( Old[0] );
			Triangle.Indices.Add( Old[i] );
			Triangle.Indices.Add( Old[i + 1] );
			if ( i == 1 ) Mesh.Faces[Face] = Triangle;
			else Mesh.Faces.Add( Triangle );
		}
	}
	return Mesh.Faces.Num() - NumBefore;
}

//=============================================================================
// 材質
//=============================================================================
bool NEditMesh::IsSameMaterial( const FEditMeshMaterial& A, const FEditMeshMaterial& B )
{
	return A.Name == B.Name && A.Color.r == B.Color.r && A.Color.g == B.Color.g && A.Color.b == B.Color.b && A.Texture == B.Texture && A.Tile == B.Tile && A.Offset.x == B.Offset.x && A.Offset.y == B.Offset.y && A.bTwoSided == B.bTwoSided && A.Effect.IsSameAs( B.Effect );
}

void NEditMesh::SetFaceMaterial( FEditMesh& Mesh, const TArray<int32>& Faces, int32 Material )
{
	for ( const int32 Face : Faces )
	{
		if ( Mesh.Faces.IsValidIndex( Face ) ) Mesh.Faces[Face].Material = Material;
	}
}

int32 NEditMesh::AddMaterial( FEditMesh& Mesh, const FEditMeshMaterial& Material )
{
	for ( int32 i = 0; i < Mesh.Materials.Num(); ++i )
	{
		if ( IsSameMaterial( Mesh.Materials[i], Material ) ) return i;
	}
	return Mesh.Materials.Add( Material );
}

bool NEditMesh::RemoveMaterial( FEditMesh& Mesh, int32 Index )
{
	if ( !Mesh.Materials.IsValidIndex( Index ) ) return false;
	Mesh.Materials.RemoveAt( Index );
	for ( FEditMeshFace& Face : Mesh.Faces )
	{
		if ( Face.Material == Index ) Face.Material = 0;
		else if ( Face.Material > Index ) --Face.Material;
	}
	return true;
}

//=============================================================================
// 足す・作る
//=============================================================================
int32 NEditMesh::AppendMesh( FEditMesh& Destination, const FEditMesh& Source, const FTransform& Transform )
{
	const int32 FirstFace = Destination.Faces.Num();
	const int32 VertexOffset = Destination.Vertices.Num();
	for ( const FVector3& Vertex : Source.Vertices ) Destination.Vertices.Add( Transform.TransformPoint( Vertex ) );

	// 材質の対応（元の番号 -> こちらの番号）
	TArray<int32> MaterialMap;
	const int32 NumSourceMaterials = std::max( Source.Materials.Num(), 1 );
	for ( int32 i = 0; i < NumSourceMaterials; ++i )
	{
		const FEditMeshMaterial& Material = Source.GetMaterial( i );
		if ( Destination.Materials.IsEmpty() && IsSameMaterial( Material, FEditMeshMaterial() ) ) MaterialMap.Add( 0 );
		else MaterialMap.Add( AddMaterial( Destination, Material ) );
	}
	const bool bMirrored = Transform.Scale.x * Transform.Scale.y * Transform.Scale.z < 0.0f;
	for ( const FEditMeshFace& Face : Source.Faces )
	{
		FEditMeshFace Copy;
		for ( const int32 Index : Face.Indices ) Copy.Indices.Add( Index + VertexOffset );
		if ( bMirrored )
		{
			for ( int32 i = 0, j = Copy.Indices.Num() - 1; i < j; ++i, --j ) std::swap( Copy.Indices[i], Copy.Indices[j] );
		}
		Copy.Material = Face.Material >= 0 && Face.Material < MaterialMap.Num() ? MaterialMap[Face.Material] : 0;
		Destination.Faces.Add( Copy );
	}
	return FirstFace;
}

FEditMesh NEditMesh::MakeFromTriangles( TArrayView<const FVector3> Positions, TArrayView<const uint32> Indices, float32 WeldDistance )
{
	FEditMesh Mesh;
	TArray<int32> Remap;
	Remap.Init( -1, Positions.Num() );
	const float32 Reach = std::max( WeldDistance, 0.0f );
	const bool bWeld = Reach > 0.0f;
	const auto CellOf = [&]( float32 Value ) { return static_cast<int64>( std::floor( Value / Reach ) ); };
	const auto KeyOf = [&]( int64 X, int64 Y, int64 Z ) { return ( X * 73856093ll ) ^ ( Y * 19349663ll ) ^ ( Z * 83492791ll ); };
	TMap<int64, TArray<int32>> Grid;
	const float32 ReachSquared = Reach * Reach;
	for ( int32 i = 0; i < Positions.Num(); ++i )
	{
		const FVector3& P = Positions[i];
		if ( !bWeld )
		{
			Remap[i] = Mesh.Vertices.Add( P );
			continue;
		}
		const int64 CX = CellOf( P.x ), CY = CellOf( P.y ), CZ = CellOf( P.z );
		int32 Found = -1;
		for ( int64 DX = -1; DX <= 1 && Found < 0; ++DX )
		{
			for ( int64 DY = -1; DY <= 1 && Found < 0; ++DY )
			{
				for ( int64 DZ = -1; DZ <= 1 && Found < 0; ++DZ )
				{
					const TArray<int32>* Cell = Grid.Find( KeyOf( CX + DX, CY + DY, CZ + DZ ) );
					if ( Cell == nullptr ) continue;
					for ( const int32 Other : *Cell )
					{
						if ( FVector3::DistanceSquared( Mesh.Vertices[Other], P ) <= ReachSquared )
						{
							Found = Other;
							break;
						}
					}
				}
			}
		}
		if ( Found < 0 )
		{
			Found = Mesh.Vertices.Add( P );
			Grid.FindOrAdd( KeyOf( CX, CY, CZ ) ).Add( Found );
		}
		Remap[i] = Found;
	}
	for ( int32 i = 0; i + 2 < Indices.Num(); i += 3 )
	{
		const uint32 A = Indices[i], B = Indices[i + 1], C = Indices[i + 2];
		if ( A >= static_cast<uint32>( Positions.Num() ) || B >= static_cast<uint32>( Positions.Num() ) || C >= static_cast<uint32>( Positions.Num() ) ) continue;
		const int32 VA = Remap[A], VB = Remap[B], VC = Remap[C];
		if ( VA == VB || VB == VC || VA == VC ) continue;
		FEditMeshFace Face;
		Face.Indices.Add( VA );
		Face.Indices.Add( VB );
		Face.Indices.Add( VC );
		Mesh.Faces.Add( Face );
		if ( Mesh.ComputeFaceNormal( Mesh.Faces.Num() - 1 ).IsNearlyZero() ) Mesh.Faces.RemoveAt( Mesh.Faces.Num() - 1 );
	}
	RemoveUnusedVertices( Mesh );
	return Mesh;
}
