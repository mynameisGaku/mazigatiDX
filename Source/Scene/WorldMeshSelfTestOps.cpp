#include <algorithm>
#include <cmath>

#include "Library/Core/Types/Numeric.h"
#include "Library/Framework/World/Mesh/EditMesh.h"
#include "Library/Framework/World/Mesh/EditMeshBuild.h"
#include "Library/Framework/World/Mesh/EditMeshOps.h"
#include "Library/Framework/World/Mesh/EditMeshPrimitives.h"
#include "WorldMeshSelfTestSupport.h"

using namespace NAssetTest;
using namespace NWorldMeshTest;

//=============================================================================
// 編集メッシュの編集操作の確認（選択・動かす・押し出す・差す・消す・つなぐ・割る・材質・足す・取り込み）
//   期待する値は、手で求めた数（頂点・面の数・体積・面積）と、形の不変量（閉じている・開いた縁の数・外向き）。
//=============================================================================
namespace
{
	bool Near( float32 A, float32 B, float32 Tolerance = 1.0e-3f ) { return NMath::Abs( A - B ) <= Tolerance; }
	bool Near3( const FVector3& A, const FVector3& B, float32 Tolerance = 1.0e-3f ) { return Near( A.x, B.x, Tolerance ) && Near( A.y, B.y, Tolerance ) && Near( A.z, B.z, Tolerance ); }

	TArray<int32> Ints( std::initializer_list<int32> List )
	{
		TArray<int32> Result;
		for ( const int32 Value : List ) Result.Add( Value );
		return Result;
	}

	// 箱（2 x 2 x 2、底が y = 0）の面: 0 上 / 1 下 / 2 +X / 3 -X / 4 +Z / 5 -Z
	FEditMesh Box2() { return NEditMesh::MakeBox( FVector3( 2.0f, 2.0f, 2.0f ) ); }

	//=========================================================================
	// 選択と問い合わせ
	//=========================================================================
	void TestSelection( FChecker& C )
	{
		C.Group = "mesh-select";
		TArray<int32> Set;
		for ( const int32 Value : { 5, 1, 3, 3, 9, 1 } ) NEditMesh::AddSorted( Set, Value );
		C.Check( Set == Ints( { 1, 3, 5, 9 } ) && NEditMesh::ContainsSorted( Set, 5 ) && !NEditMesh::ContainsSorted( Set, 4 ), "a sorted set keeps order and has no duplicates" );
		C.Check( NEditMesh::RemoveSorted( Set, 3 ) && !NEditMesh::RemoveSorted( Set, 3 ) && Set == Ints( { 1, 5, 9 } ), "removing works once" );
		TArray<int64> Edges;
		NEditMesh::AddSorted( Edges, NEditMesh::MakeEdgeKey( 7, 2 ) );
		NEditMesh::AddSorted( Edges, NEditMesh::MakeEdgeKey( 2, 7 ) );
		int32 A = 0, B = 0;
		NEditMesh::SplitEdgeKey( Edges[0], A, B );
		C.Check( NEditMesh::MakeEdgeKey( 7, 2 ) == NEditMesh::MakeEdgeKey( 2, 7 ) && Edges.Num() == 1 && A == 2 && B == 7 && NEditMesh::ContainsSorted( Edges, NEditMesh::MakeEdgeKey( 7, 2 ) ), "an edge is the pair of vertices, smaller first, the same both ways" );
		NEditMesh::SplitEdgeKey( NEditMesh::MakeEdgeKey( 100000, 3 ), A, B );
		C.Check( A == 3 && B == 100000, "even with large numbers" );

		const FEditMesh Box = Box2();
		FEditMeshSelection Selection;
		Selection.Vertices = Ints( { 0, 0, 99 } );   // 重複・範囲外は、Clean で除く
		NEditMesh::AddSorted( Selection.Faces, 5 );
		NEditMesh::AddSorted( Selection.Faces, 40 );
		NEditMesh::AddSorted( Selection.Edges, NEditMesh::MakeEdgeKey( 0, 1 ) );
		NEditMesh::AddSorted( Selection.Edges, NEditMesh::MakeEdgeKey( 0, 6 ) );   // 箱にない辺（対角）
		NEditMesh::CleanSelection( Box, Selection );
		C.Check( Selection.Vertices == Ints( { 0 } ) && Selection.Faces == Ints( { 5 } ) && Selection.Edges.Num() == 1 && !Selection.IsEmpty(), "cleaning drops what the mesh does not have (a vertex, a face, an edge that no face uses)" );
		Selection.Clear();
		C.Check( Selection.IsEmpty() && Selection.Faces.IsEmpty(), "Clear empties every set" );

		// 動かす頂点
		FEditMeshSelection Mixed;
		Mixed.Vertices = Ints( { 7 } );
		NEditMesh::AddSorted( Mixed.Edges, NEditMesh::MakeEdgeKey( 4, 1 ) );
		Mixed.Faces = Ints( { 0 } );
		const TArray<int32> Affected = NEditMesh::GetAffectedVertices( Box, Mixed );
		TArray<int32> TopVertices = Box.Faces[0].Indices;
		TopVertices.Sort();
		TArray<int32> Expected = TopVertices;
		for ( const int32 Extra : { 7, 4, 1 } ) NEditMesh::AddSorted( Expected, Extra );
		C.Check( Affected == Expected, "the vertices a selection moves: the chosen vertices, both ends of the chosen edges, and the corners of the chosen faces (%d)", Affected.Num() );
		FEditMeshSelection TopFace;
		TopFace.Faces = Ints( { 0 } );
		C.Check( Near3( NEditMesh::ComputeSelectionCenter( Box, TopFace ), FVector3( 0, 2, 0 ) ) && Near3( NEditMesh::ComputeSelectionNormal( Box, TopFace ), FVector3::Up ) && Near3( NEditMesh::ComputeSelectionCenter( Box, FEditMeshSelection() ), FVector3::Zero ), "the centre and normal of the top face" );
		FEditMeshSelection Opposite;
		Opposite.Faces = Ints( { 0, 1 } );
		C.Check( NEditMesh::ComputeSelectionNormal( Box, Opposite ).IsNearlyZero() && Near3( NEditMesh::ComputeSelectionNormal( Box, FEditMeshSelection() ), FVector3::Zero ), "the normals of opposite faces cancel (zero), and no faces give zero" );
		FEditMeshSelection SideAndTop;
		SideAndTop.Faces = Ints( { 0, 2 } );
		C.Check( Near3( NEditMesh::ComputeSelectionNormal( Box, SideAndTop ), FVector3( 1, 1, 0 ).Normalized() ), "the normal of two equal faces is between them" );
		// 種類を替える: 面 -> 頂点 -> 辺 / 面、頂点の一部だけでは、面にならない
		FEditMeshSelection ToVertex = NEditMesh::ConvertSelection( Box, TopFace, NEditMesh::EEditMeshElement::Vertex );
		FEditMeshSelection ToEdge = NEditMesh::ConvertSelection( Box, ToVertex, NEditMesh::EEditMeshElement::Edge );
		FEditMeshSelection ToFace = NEditMesh::ConvertSelection( Box, ToVertex, NEditMesh::EEditMeshElement::Face );
		C.Check( ToVertex.Vertices == TopVertices && ToVertex.Faces.IsEmpty() && ToEdge.Edges.Num() == 4 && ToEdge.Vertices.IsEmpty() && ToFace.Faces == Ints( { 0 } ) && ToFace.Edges.IsEmpty(), "converting the top face to vertices (4), to edges (the 4 between them) and back to faces (just the top)" );
		FEditMeshSelection Half;
		Half.Vertices = Ints( { TopVertices[0], TopVertices[1] } );
		C.Check( NEditMesh::ConvertSelection( Box, Half, NEditMesh::EEditMeshElement::Face ).Faces.IsEmpty() && NEditMesh::ConvertSelection( Box, Half, NEditMesh::EEditMeshElement::Edge ).Edges.Num() <= 1 && NEditMesh::ConvertSelection( Box, FEditMeshSelection(), NEditMesh::EEditMeshElement::Vertex ).IsEmpty(), "two vertices of a face do not make a face; nothing gives nothing" );
		FEditMeshSelection Gone;
		Gone.Vertices = Ints( { 99 } );
		Gone.Faces = Ints( { 99 } );
		C.Check( NEditMesh::GetAffectedVertices( Box, Gone ).IsEmpty(), "things that do not exist move nothing" );

		// 問い合わせ
		C.Check( NEditMesh::GetAllEdges( Box ).Num() == 12 && NEditMesh::GetAllEdges( Box )[0] < NEditMesh::GetAllEdges( Box )[1] && NEditMesh::CountOpenEdges( Box ) == 0 && NEditMesh::CountOpenEdges( NEditMesh::MakePlane( 2, 2, 2, 2 ) ) == 8 && NEditMesh::CountOpenEdges( NEditMesh::MakeRamp( FVector3::One ) ) == 0,
				 "a box has 12 edges and no open ones; a 2 x 2 plane has 8 open edges" );
		TArray<int32> Using = NEditMesh::GetFacesUsingVertices( Box, Ints( { Box.Faces[0].Indices[0] } ) );
		C.Check( Using.Num() == 3, "a corner of a box is used by 3 faces (%d)", Using.Num() );
		C.Check( NEditMesh::GetFacesUsingEdge( Box, NEditMesh::MakeEdgeKey( Box.Faces[0].Indices[0], Box.Faces[0].Indices[1] ) ).Num() == 2 && NEditMesh::GetFacesUsingEdge( Box, NEditMesh::MakeEdgeKey( 0, 99 ) ).IsEmpty(), "an edge is shared by 2 faces; an edge that does not exist has none" );
		FEditMesh Two = Box2();
		NEditMesh::AppendMesh( Two, NEditMesh::MakeBox( FVector3::One ), FTransform( FVector3( 10, 0, 0 ) ) );
		C.Check( NEditMesh::GetLinkedFaces( Two, Ints( { 0 } ) ) == Ints( { 0, 1, 2, 3, 4, 5 } ) && NEditMesh::GetLinkedFaces( Two, Ints( { 7 } ) ) == Ints( { 6, 7, 8, 9, 10, 11 } ) && NEditMesh::GetLinkedFaces( Two, Ints( { 0, 7 } ) ).Num() == 12 && NEditMesh::GetLinkedFaces( Two, Ints( { 99 } ) ).IsEmpty(),
				 "linked faces are the whole island: one box, the other box, or both" );
	}

	//=========================================================================
	// 押し出し・差す
	//=========================================================================
	void TestExtrude( FChecker& C )
	{
		C.Group = "mesh-extrude";
		FEditMesh Box = Box2();
		FEditMeshSelection Result = NEditMesh::ExtrudeFaces( Box, Ints( { 0 } ), 1.0f );
		FVector3 Min, Max;
		C.Check( Box.GetNumVertices() == 12 && Box.GetNumFaces() == 10 && Result.Faces == Ints( { 0 } ) && Result.Vertices.Num() == 4 && Result.Vertices[0] == 8 && Result.Vertices[3] == 11, "extruding the top face adds 4 vertices and 4 side faces; the face keeps its number, and the new vertices are returned (%d, %d)", Box.GetNumVertices(), Box.GetNumFaces() );
		C.Check( Near3( Box.ComputeFaceNormal( 0 ), FVector3::Up ) && Box.GetBounds( Min, Max ) && Near( Max.y, 3.0f ) && Near( Box.ComputeFaceCenter( 0 ).y, 3.0f ), "the extruded face is 1 higher and still faces up" );
		C.Check( IsClosed( Box ) && FacesLookOutward( Box ) && Near( Box.ComputeVolume(), 12.0f ) && Box.Validate().IsEmpty(), "the mesh is still closed, faces outward, has the volume of 8 + 4 = 12, and has no issues (%.3f)", Box.ComputeVolume() );
		bool bWallsUp = true;
		for ( int32 Face = 6; Face < Box.GetNumFaces(); ++Face ) bWallsUp = bWallsUp && Near( Box.ComputeFaceArea( Face ), 2.0f ) && NMath::Abs( Box.ComputeFaceNormal( Face ).y ) < 1.0e-3f;
		C.Check( bWallsUp, "the 4 new walls are vertical, each 2 x 1" );

		// 内側へ（負）: 穴になる。閉じたまま、体積は減る
		FEditMesh Pit = Box2();
		NEditMesh::ExtrudeFaces( Pit, Ints( { 0 } ), -1.0f );
		C.Check( IsClosed( Pit ) && Near( Pit.ComputeVolume(), 4.0f ) && Near( Pit.ComputeFaceCenter( 0 ).y, 1.0f ), "a negative distance digs inwards: closed, volume 8 - 4 = 4 (%.3f)", Pit.ComputeVolume() );
		// 距離 0: その場に壁（つぶれた壁）ができる。そのあと動かすと、同じ結果
		FEditMesh Later = Box2();
		const FEditMeshSelection Zero = NEditMesh::ExtrudeFaces( Later, Ints( { 0 } ), 0.0f );
		for ( const int32 Index : Zero.Vertices ) Later.Vertices[Index] = Later.Vertices[Index] + FVector3( 0, 1, 0 );
		C.Check( Later.GetNumVertices() == Box.GetNumVertices() && Later.GetNumFaces() == Box.GetNumFaces() && Near( Later.ComputeVolume(), Box.ComputeVolume() ) && IsClosed( Later ), "extruding by 0 and then dragging the new vertices up gives the same shape" );

		// 2 つの面を 1 つの塊として: 共有している辺には壁を作らない
		FEditMesh Corner = Box2();
		Result = NEditMesh::ExtrudeFaces( Corner, Ints( { 0, 2 } ), 1.0f );
		C.Check( Corner.GetNumVertices() == 8 + 6 && Corner.GetNumFaces() == 6 + 6 && Result.Faces == Ints( { 0, 2 } ) && Result.Vertices.Num() == 6 && IsClosed( Corner ), "two neighbouring faces as one region: the shared edge gets no wall (6 walls, 6 new vertices)" );
		// 1 枚ずつ
		FEditMesh Separate = Box2();
		Result = NEditMesh::ExtrudeFaces( Separate, Ints( { 0, 2 } ), 1.0f, true );
		C.Check( Separate.GetNumVertices() == 8 + 8 && Separate.GetNumFaces() == 6 + 8 && Result.Vertices.Num() == 8 && IsClosed( Separate ) && Near3( Separate.ComputeFaceNormal( 0 ), FVector3::Up ) && Near3( Separate.ComputeFaceNormal( 2 ), FVector3::Right ),
				 "each face on its own: 8 walls, 8 new vertices, still closed, each moved along its own normal" );
		// まとめた押し出しの頂点は、その頂点を使う選んだ面の向きを、面積で重みづけして混ぜた向きに出る（上の面と +x の面が共有する角は、斜め）
		FEditMesh Slanted = Box2();
		NEditMesh::ExtrudeFaces( Slanted, Ints( { 0, 2 } ), 1.0f );
		const float32 Diagonal = 0.70710678f;
		bool bSlantedFound = false;
		for ( int32 i = 8; i < Slanted.GetNumVertices(); ++i ) bSlantedFound = bSlantedFound || Near3( Slanted.Vertices[i], FVector3( 1.0f + Diagonal, 2.0f + Diagonal, 1.0f ) );
		C.Check( bSlantedFound, "a vertex shared by two chosen faces moves along the mix of their directions (the corner of the top and the +x side moves diagonally)" );

		// 開いたメッシュ（平面）
		FEditMesh Plane = NEditMesh::MakePlane( 2, 2, 1, 1 );
		NEditMesh::ExtrudeFaces( Plane, Ints( { 0 } ), 1.0f );
		C.Check( Plane.GetNumFaces() == 5 && Plane.GetNumVertices() == 8 && NEditMesh::CountOpenEdges( Plane ) == 4 && Near3( Plane.ComputeFaceNormal( 0 ), FVector3::Up ), "extruding the only face of a plane makes a tray: 4 walls, and the open edge is now the bottom of the walls" );
		FEditMesh Grid = NEditMesh::MakePlane( 2, 2, 2, 2 );
		const int32 OpenBefore = NEditMesh::CountOpenEdges( Grid );
		NEditMesh::ExtrudeFaces( Grid, Ints( { 0 } ), 1.0f );
		C.Check( Grid.GetNumFaces() == 4 + 4 && NEditMesh::CountOpenEdges( Grid ) == OpenBefore, "extruding one cell of a grid puts walls round it (the outer open edge stays the same: %d)", NEditMesh::CountOpenEdges( Grid ) );

		// 材質: 壁は、元の面の材質を引き継ぐ
		FEditMesh Painted = Box2();
		FEditMeshMaterial Red;
		Red.Color = FColor32( 255, 0, 0 );
		Painted.Materials.Add( FEditMeshMaterial() );
		Painted.Materials.Add( Red );
		Painted.Faces[0].Material = 1;
		NEditMesh::ExtrudeFaces( Painted, Ints( { 0 } ), 1.0f );
		bool bWallsRed = true;
		for ( int32 Face = 6; Face < Painted.GetNumFaces(); ++Face ) bWallsRed = bWallsRed && Painted.Faces[Face].Material == 1;
		C.Check( bWallsRed && Painted.Faces[0].Material == 1 && Painted.Faces[2].Material == 0, "the walls take the material of the face they were pushed out of" );

		// 入力
		FEditMesh Untouched = Box2();
		const uint64 Hash = Untouched.ComputeHash();
		C.Check( NEditMesh::ExtrudeFaces( Untouched, TArray<int32>(), 1.0f ).IsEmpty() && NEditMesh::ExtrudeFaces( Untouched, Ints( { 40, -1 } ), 1.0f ).IsEmpty() && Untouched.ComputeHash() == Hash, "nothing to extrude changes nothing" );
		FEditMesh Dup = Box2();
		NEditMesh::ExtrudeFaces( Dup, Ints( { 0, 0, 0 } ), 1.0f );
		C.Check( Dup.GetNumFaces() == 10 && Dup.GetNumVertices() == 12, "the same face listed 3 times is extruded once" );
		FEditMesh Odd = Box2();
		Odd.Faces.Add( FEditMeshFace() );   // 使えない面
		C.Check( NEditMesh::ExtrudeFaces( Odd, Ints( { 6 } ), 1.0f ).IsEmpty() && Odd.GetNumFaces() == 7, "an unusable face is not extruded" );

		// 差す（インセット）
		FEditMesh Inset = Box2();
		Result = NEditMesh::InsetFaces( Inset, Ints( { 0 } ), 0.5f );
		C.Check( Inset.GetNumVertices() == 12 && Inset.GetNumFaces() == 10 && Near( Inset.ComputeFaceArea( 0 ), 1.0f ) && Near3( Inset.ComputeFaceCenter( 0 ), FVector3( 0, 2, 0 ) ) && Near3( Inset.ComputeFaceNormal( 0 ), FVector3::Up ) && Near( Inset.ComputeVolume(), 8.0f ) && IsClosed( Inset ),
				 "insetting the top by 0.5: a frame round a face of half the width (area 1), in the same plane; the shape is unchanged (%.3f)", Inset.ComputeFaceArea( 0 ) );
		bool bFrame = true;
		float32 FrameArea = 0.0f;
		for ( int32 Face = 6; Face < Inset.GetNumFaces(); ++Face )
		{
			bFrame = bFrame && Near3( Inset.ComputeFaceNormal( Face ), FVector3::Up );
			FrameArea += Inset.ComputeFaceArea( Face );
		}
		C.Check( bFrame && Near( FrameArea, 3.0f ) && Result.Faces == Ints( { 0 } ) && Result.Vertices.Num() == 4, "the frame faces up and covers the rest of the top (4 - 1 = 3)" );
		FEditMesh Wide = Box2();
		NEditMesh::InsetFaces( Wide, Ints( { 0 } ), 5.0f );
		FEditMesh None = Box2();
		NEditMesh::InsetFaces( None, Ints( { 0 } ), -1.0f );
		C.Check( Near( Wide.ComputeFaceArea( 0 ), 4.0f * 0.05f * 0.05f ) && Near( None.ComputeFaceArea( 0 ), 4.0f ), "an amount above 1 is held at 0.95 (the face stays a small square); below 0 does not shrink" );
		FEditMesh Both = Box2();
		NEditMesh::InsetFaces( Both, Ints( { 0, 2 } ), 0.5f );
		C.Check( IsClosed( Both ) && Both.GetNumFaces() == 6 + 8 && Near( Both.ComputeFaceArea( 0 ), 1.0f ) && Near( Both.ComputeFaceArea( 2 ), 1.0f ) && Near( Both.ComputeVolume(), 8.0f ), "two neighbouring faces are inset one by one (each keeps its own frame)" );
	}

	//=========================================================================
	// 消す・つなぐ・割る
	//=========================================================================
	void TestDeleteMerge( FChecker& C )
	{
		C.Group = "mesh-delete-merge";
		FEditMesh Box = Box2();
		C.Check( NEditMesh::DeleteFaces( Box, Ints( { 0 } ) ) == 1 && Box.GetNumFaces() == 5 && Box.GetNumVertices() == 8 && NEditMesh::CountOpenEdges( Box ) == 4 && Box.Validate().IsEmpty(), "deleting the top leaves a box without a lid: 5 faces, 8 vertices, 4 open edges" );
		FEditMesh Tube = Box2();
		C.Check( NEditMesh::DeleteFaces( Tube, Ints( { 0, 1, 40, 0 } ) ) == 2 && Tube.GetNumFaces() == 4 && Tube.GetNumVertices() == 8, "top and bottom (listed with a missing and a repeated number): 2 deleted" );
		FEditMesh Grid = NEditMesh::MakePlane( 2, 2, 2, 2 );
		C.Check( NEditMesh::DeleteFaces( Grid, Ints( { 0 } ) ) == 1 && Grid.GetNumFaces() == 3 && Grid.GetNumVertices() == 8, "a vertex that no face uses any more is removed (the corner of the deleted cell)" );
		bool bIndicesValid = true;
		for ( int32 Face = 0; Face < Grid.GetNumFaces(); ++Face ) bIndicesValid = bIndicesValid && Grid.IsFaceValid( Face );
		C.Check( bIndicesValid, "and the faces still point at the right vertices after the numbers were closed up" );
		FEditMesh All = Box2();
		C.Check( NEditMesh::DeleteFaces( All, Ints( { 0, 1, 2, 3, 4, 5 } ) ) == 6 && All.GetNumFaces() == 0 && All.GetNumVertices() == 0 && NEditMesh::DeleteFaces( All, Ints( { 0 } ) ) == 0, "deleting every face empties the mesh" );

		FEditMesh Corner = Box2();
		C.Check( NEditMesh::DeleteVertices( Corner, Ints( { 0 } ) ) == 3 && Corner.GetNumFaces() == 3 && Corner.GetNumVertices() == 7 && Corner.Validate().IsEmpty(), "deleting a corner vertex removes its 3 faces, and the vertex" );
		FEditMesh Spare = Box2();
		Spare.Vertices.Add( FVector3( 9, 9, 9 ) );
		C.Check( NEditMesh::DeleteVertices( Spare, Ints( { 8 } ) ) == 0 && Spare.GetNumVertices() == 8 && Spare.GetNumFaces() == 6 && NEditMesh::DeleteVertices( Spare, Ints( { 99 } ) ) == 0 && Spare.GetNumFaces() == 6, "a vertex no face uses is just removed; one that does not exist does nothing" );

		// 裏返し
		FEditMesh Flip = Box2();
		NEditMesh::FlipFaces( Flip, Ints( { 0 } ) );
		C.Check( Near3( Flip.ComputeFaceNormal( 0 ), FVector3::Down ) && Near3( Flip.ComputeFaceNormal( 1 ), FVector3::Down ) && Near3( Flip.ComputeFaceNormal( 2 ), FVector3::Right ), "flipping one face turns only that face (the top now faces down)" );
		NEditMesh::FlipFaces( Flip, Ints( { 0, 1, 2, 3, 4, 5 } ) );
		C.Check( Near3( Flip.ComputeFaceNormal( 0 ), FVector3::Up ) && Near3( Flip.ComputeFaceNormal( 2 ), FVector3::Left ) && Near3( Flip.ComputeFaceNormal( 1 ), FVector3::Up ), "flipping all of them: the top (flipped twice) is up again, and the others point the other way" );

		// つなぐ
		FEditMesh Merge = Box2();
		const int32 TopA = Merge.Faces[0].Indices[0], TopB = Merge.Faces[0].Indices[1];
		const FVector3 Middle = ( Merge.Vertices[TopA] + Merge.Vertices[TopB] ) * 0.5f;
		const int32 Merged = NEditMesh::MergeVertices( Merge, Ints( { TopA, TopB } ) );
		C.Check( Merged >= 0 && Merge.Vertices.IsValidIndex( Merged ) && Near3( Merge.Vertices[Merged], Middle ) && Merge.GetNumVertices() == 7 && Merge.GetNumFaces() == 6 && IsClosed( Merge ) && Merge.Validate().IsEmpty(),
				 "merging the two ends of an edge puts the vertex in the middle; 7 vertices, still 6 faces and closed (the two faces that had the edge become triangles)" );
		int32 NumTriangles = 0;
		for ( const FEditMeshFace& Face : Merge.Faces ) NumTriangles += Face.Indices.Num() == 3 ? 1 : 0;
		C.Check( NumTriangles == 2, "exactly 2 faces became triangles (%d)", NumTriangles );
		FEditMesh Collapse = Box2();
		const int32 Collapsed = NEditMesh::MergeVertices( Collapse, Collapse.Faces[0].Indices );
		C.Check( Collapsed >= 0 && Collapse.GetNumVertices() == 5 && Collapse.GetNumFaces() == 5 && Near3( Collapse.Vertices[Collapsed], FVector3( 0, 2, 0 ) ) && Collapse.Validate().IsEmpty(), "merging the 4 corners of the top makes a pyramid: the top face disappears, 5 vertices, 5 faces" );
		FEditMesh One = Box2();
		const uint64 OneHash = One.ComputeHash();
		C.Check( NEditMesh::MergeVertices( One, Ints( { 3 } ) ) == -1 && NEditMesh::MergeVertices( One, Ints( { 3, 3, 99 } ) ) == -1 && NEditMesh::MergeVertices( One, TArray<int32>() ) == -1 && One.ComputeHash() == OneHash, "fewer than 2 vertices (repeats and missing ones do not count) merge nothing" );

		// 近いものをまとめる
		FEditMesh Seam = NEditMesh::MakePlane( 2, 2, 1, 1 );
		NEditMesh::AppendMesh( Seam, NEditMesh::MakePlane( 2, 2, 1, 1 ), FTransform( FVector3( 2.0f, 0.0f, 0.0f ) ) );   // 隣に、頂点を共有しないで並べる
		C.Check( Seam.GetNumVertices() == 8 && NEditMesh::CountOpenEdges( Seam ) == 8, "two planes side by side with unshared vertices: 8 vertices, 8 open edges" );
		FEditMesh Near1 = Seam;
		C.Check( NEditMesh::WeldVertices( Near1, TArray<int32>(), 0.01f ) == 2 && Near1.GetNumVertices() == 6 && Near1.GetNumFaces() == 2 && NEditMesh::CountOpenEdges( Near1 ) == 6, "welding joins the two seam edges: 2 vertices fewer, 6 open edges left" );
		FEditMesh Slight = Seam;
		Slight.Vertices[4] = Slight.Vertices[4] + FVector3( 0.005f, 0, 0 );
		C.Check( NEditMesh::WeldVertices( Slight, TArray<int32>(), 0.001f ) == 1 && NEditMesh::WeldVertices( Slight, TArray<int32>(), 0.01f ) == 1 && Slight.GetNumVertices() == 6, "vertices further apart than the distance stay apart (one pair joined at 0.001, the slightly moved one at 0.01)" );
		FEditMesh Subset = Seam;
		const int32 KeepApart = Subset.Faces[1].Indices[0];
		TArray<int32> OnlyThese;
		for ( int32 i = 0; i < Subset.GetNumVertices(); ++i )
		{
			if ( i != KeepApart ) OnlyThese.Add( i );
		}
		const int32 Welded = NEditMesh::WeldVertices( Subset, OnlyThese, 0.01f );
		C.Check( Welded == 1, "only the vertices given take part: leaving one of the pair out joins just the other pair (%d)", Welded );
		FEditMesh Everything = Seam;
		C.Check( NEditMesh::WeldVertices( Everything, TArray<int32>(), 100.0f ) > 0 && Everything.GetNumFaces() == 0, "welding at a huge distance collapses everything: the faces vanish, and nothing is left" );
		FEditMesh Solid = Box2();
		C.Check( NEditMesh::WeldVertices( Solid, TArray<int32>(), 0.0001f ) == 0, "a box has nothing to weld" );
		const auto Tri = []( int32 A, int32 B, int32 Third ) { FEditMeshFace Face; Face.Indices.Add( A ); Face.Indices.Add( B ); Face.Indices.Add( Third ); return Face; };
		// ちょうどの距離（0.5）は、溶接する。隣のセルにある頂点も（0 と 0.5 は、別のセル）
		FEditMesh Exact;
		for ( const FVector3 P : { FVector3( 0, 0, 0 ), FVector3( 10, 0, 0 ), FVector3( 0, 0, 10 ), FVector3( 0.5f, 0, 0 ), FVector3( 0.5f, 0, -10 ), FVector3( 10.5f, 0, -10 ) } ) Exact.Vertices.Add( P );
		Exact.Faces.Add( Tri( 0, 1, 2 ) );
		Exact.Faces.Add( Tri( 3, 4, 5 ) );
		C.Check( NEditMesh::WeldVertices( Exact, TArray<int32>(), 0.5f ) == 1 && Exact.GetNumVertices() == 5 && Exact.GetNumFaces() == 2, "two vertices exactly the distance apart are joined (also when they are in neighbouring cells)" );

		// つなぐ: 輪の端・向かい合う角・三角形
		const FEditMesh Square = NEditMesh::MakePlane( 2, 2, 1, 1 );   // 四角形の面 1 つ（頂点 0..3、この順）
		FEditMesh Wrap = Square;
		C.Check( NEditMesh::MergeVertices( Wrap, Ints( { 0, 3 } ) ) >= 0 && Wrap.GetNumFaces() == 1 && Wrap.Faces[0].Indices.Num() == 3 && Wrap.GetNumVertices() == 3, "merging the first and the last corner of a quad leaves a triangle (the end of the loop is dropped)" );
		FEditMesh Across = Square;
		NEditMesh::MergeVertices( Across, Ints( { 1, 3 } ) );
		C.Check( Across.GetNumFaces() == 0, "merging two opposite corners of a quad pinches the face: it is dropped" );
		FEditMesh Triangle;
		for ( const FVector3 P : { FVector3( 0, 0, 0 ), FVector3( 1, 0, 0 ), FVector3( 0, 0, 1 ) } ) Triangle.Vertices.Add( P );
		Triangle.Faces.Add( Tri( 0, 1, 2 ) );
		NEditMesh::MergeVertices( Triangle, Ints( { 0, 1 } ) );
		C.Check( Triangle.GetNumFaces() == 0, "a triangle with two corners merged is dropped (two corners are not a face)" );

		// 割る
		FEditMesh Quad = NEditMesh::MakePlane( 2, 2, 1, 1 );
		C.Check( NEditMesh::SubdivideFaces( Quad, Ints( { 0 } ) ) == 3 && Quad.GetNumFaces() == 4 && Quad.GetNumVertices() == 9 && Near( Quad.ComputeFaceArea( 0 ), 1.0f ) && Near3( Quad.ComputeFaceNormal( 2 ), FVector3::Up ), "a square split into 4 quads: 9 vertices, each of area 1, still facing up" );
		FEditMesh Lid = Box2();
		C.Check( NEditMesh::SubdivideFaces( Lid, Ints( { 0 } ) ) == 3 && Lid.GetNumFaces() == 9 && Lid.GetNumVertices() == 8 + 4 + 1 && IsClosed( Lid ) && Near( Lid.ComputeVolume(), 8.0f ) && Lid.Validate().IsEmpty(), "splitting a box's top: its 4 neighbours get the edge-middle vertices (no gaps), the box stays closed with the same volume" );
		int32 NumPentagons = 0;
		for ( const FEditMeshFace& Face : Lid.Faces ) NumPentagons += Face.Indices.Num() == 5 ? 1 : 0;
		C.Check( NumPentagons == 4, "the neighbours have 5 vertices now (%d)", NumPentagons );
		FEditMesh Pair = Box2();
		C.Check( NEditMesh::SubdivideFaces( Pair, Ints( { 0, 2 } ) ) == 6 && IsClosed( Pair ) && Pair.GetNumVertices() == 8 + 7 + 2, "two neighbouring faces: the shared edge has one middle vertex (7 edge middles + 2 centres)" );
		FEditMesh Wedge = NEditMesh::MakeRamp( FVector3( 2, 1, 2 ) );
		C.Check( NEditMesh::SubdivideFaces( Wedge, Ints( { 3 } ) ) == 2 && Wedge.GetNumFaces() == 7 && IsClosed( Wedge ), "a triangle splits into 3 quads" );
		FEditMesh Marked = Box2();
		Marked.Materials.Add( FEditMeshMaterial() );
		FEditMeshMaterial Blue;
		Blue.Color = FColor32( 0, 0, 255 );
		Marked.Materials.Add( Blue );
		Marked.Faces[0].Material = 1;
		NEditMesh::SubdivideFaces( Marked, Ints( { 0 } ) );
		int32 NumBlue = 0;
		for ( const FEditMeshFace& Face : Marked.Faces ) NumBlue += Face.Material == 1 ? 1 : 0;
		C.Check( NumBlue == 4, "the pieces keep the material (%d)", NumBlue );

		// 三角形に分ける
		FEditMesh Tris = Box2();
		C.Check( NEditMesh::TriangulateFaces( Tris, Ints( { 0, 1, 2, 3, 4, 5 } ) ) == 6 && Tris.GetNumFaces() == 12 && Near( Tris.ComputeVolume(), 8.0f ) && IsClosed( Tris ), "triangulating a box: 12 triangles, the same shape" );
		C.Check( NEditMesh::TriangulateFaces( Tris, Ints( { 0, 1, 2 } ) ) == 0, "triangles stay as they are" );
		FEditMesh Tinted = Box2();
		Tinted.Materials.Add( FEditMeshMaterial() );
		Tinted.Materials.Add( FEditMeshMaterial() );
		Tinted.Faces[0].Material = 1;
		NEditMesh::TriangulateFaces( Tinted, Ints( { 0 } ) );
		int32 NumTinted = 0;
		for ( const FEditMeshFace& Face : Tinted.Faces ) NumTinted += Face.Material == 1 ? 1 : 0;
		C.Check( NumTinted == 2, "the triangles keep the material (%d)", NumTinted );
		FEditMesh CShape;
		for ( const FVector3 P : { FVector3( 4, 0, 0 ), FVector3( 0, 0, 0 ), FVector3( 0, 0, 4 ), FVector3( 4, 0, 4 ), FVector3( 4, 0, 3 ), FVector3( 1, 0, 3 ), FVector3( 1, 0, 1 ), FVector3( 4, 0, 1 ) } ) CShape.Vertices.Add( P );
		FEditMeshFace Loop;
		for ( int32 i = 0; i < 8; ++i ) Loop.Indices.Add( i );
		CShape.Faces.Add( Loop );
		C.Check( CShape.Validate().Num() == 1 && NEditMesh::TriangulateFaces( CShape, Ints( { 0 } ) ) == 5 && CShape.GetNumFaces() == 6, "an 8-sided face becomes 6 triangles (the original face plus 5)" );

		// 後片付け
		FEditMesh Mess = Box2();
		Mess.Vertices.Add( FVector3( 9, 9, 9 ) );
		Mess.Vertices.Add( FVector3( 8, 8, 8 ) );
		Mess.Faces.Add( FEditMeshFace() );
		Mess.Faces.Add( [] { FEditMeshFace Face; Face.Indices.Add( 0 ); Face.Indices.Add( 1 ); Face.Indices.Add( 1 ); return Face; }() );
		C.Check( NEditMesh::RemoveInvalidFaces( Mess ) == 2 && Mess.GetNumFaces() == 6 && NEditMesh::RemoveUnusedVertices( Mess ) == 2 && Mess.GetNumVertices() == 8 && NEditMesh::RemoveUnusedVertices( Mess ) == 0 && Mess.Validate().IsEmpty(), "invalid faces and unused vertices are cleaned up (2 each), and the second time there is nothing" );
	}

	//=========================================================================
	// 材質・足す・取り込み
	//=========================================================================
	void TestMaterialsAndAppend( FChecker& C )
	{
		C.Group = "mesh-materials";
		FEditMeshMaterial A, B;
		A.Name = "A";
		B.Name = "A";
		C.Check( NEditMesh::IsSameMaterial( A, B ) && NEditMesh::IsSameMaterial( FEditMeshMaterial(), FEditMeshMaterial() ), "equal materials are the same" );
		const auto Differs = [&]( const auto& Change ) { FEditMeshMaterial Changed = A; Change( Changed ); return !NEditMesh::IsSameMaterial( A, Changed ); };
		C.Check( Differs( []( FEditMeshMaterial& M ) { M.Name = "B"; } ) && Differs( []( FEditMeshMaterial& M ) { M.Color.r = 1; } ) && Differs( []( FEditMeshMaterial& M ) { M.Color.g = 1; } ) && Differs( []( FEditMeshMaterial& M ) { M.Color.b = 1; } ) && Differs( []( FEditMeshMaterial& M ) { M.Texture = "a.png"; } )
					 && Differs( []( FEditMeshMaterial& M ) { M.Tile = 2.0f; } ) && Differs( []( FEditMeshMaterial& M ) { M.Offset.x = 1.0f; } ) && Differs( []( FEditMeshMaterial& M ) { M.Offset.y = 1.0f; } ) && Differs( []( FEditMeshMaterial& M ) { M.bTwoSided = true; } ),
				 "each field makes a material different" );

		FEditMesh Mesh = Box2();
		C.Check( NEditMesh::AddMaterial( Mesh, A ) == 0 && NEditMesh::AddMaterial( Mesh, A ) == 0 && Mesh.Materials.Num() == 1, "adding a material twice gives the same number" );
		FEditMeshMaterial Other;
		Other.Name = "Other";
		C.Check( NEditMesh::AddMaterial( Mesh, Other ) == 1 && Mesh.Materials.Num() == 2, "a different one is a new number" );
		NEditMesh::SetFaceMaterial( Mesh, Ints( { 2, 3, 99, -1 } ), 1 );
		C.Check( Mesh.Faces[2].Material == 1 && Mesh.Faces[3].Material == 1 && Mesh.Faces[4].Material == 0, "setting the material changes the listed faces only (and skips missing ones)" );
		FEditMeshMaterial Third;
		Third.Name = "Third";
		NEditMesh::AddMaterial( Mesh, Third );
		Mesh.Faces[4].Material = 2;
		C.Check( NEditMesh::RemoveMaterial( Mesh, 1 ) && Mesh.Materials.Num() == 2 && Mesh.Faces[2].Material == 0 && Mesh.Faces[3].Material == 0 && Mesh.Faces[4].Material == 1 && Mesh.Materials[1].Name == "Third", "removing a material sends its faces to material 0, and the later numbers move up by one" );
		C.Check( !NEditMesh::RemoveMaterial( Mesh, 5 ) && !NEditMesh::RemoveMaterial( Mesh, -1 ) && NEditMesh::RemoveMaterial( Mesh, 0 ) && Mesh.Materials.Num() == 1 && Mesh.Faces[4].Material == 0 && Mesh.Faces[2].Material == 0, "a number that does not exist is refused; removing 0 leaves the old 1 as the new 0" );

		// 足す
		C.Group = "mesh-append";
		FEditMesh Dest = Box2();
		FEditMesh Source = NEditMesh::MakeBox( FVector3( 1, 1, 1 ) );
		const int32 First = NEditMesh::AppendMesh( Dest, Source, FTransform( FVector3( 5, 0, 0 ) ) );
		FVector3 Min, Max;
		C.Check( First == 6 && Dest.GetNumVertices() == 16 && Dest.GetNumFaces() == 12 && Dest.Materials.IsEmpty() && Dest.ComputeVolume() > 8.99f && Dest.ComputeVolume() < 9.01f && Dest.GetBounds( Min, Max ) && Near( Max.x, 5.5f ), "a second box is appended (first new face is 6): 16 vertices, 12 faces, volume 8 + 1, no materials made" );
		C.Check( Dest.IsFaceValid( 11 ) && Dest.Faces[6].Indices[0] >= 8, "the new faces point at the new vertices" );
		FEditMesh Mirrored = Box2();
		NEditMesh::AppendMesh( Mirrored, NEditMesh::MakeRamp( FVector3( 2, 1, 2 ) ), FTransform( FVector3( 5, 0, 0 ), FQuaternion::Identity, FVector3( -1.0f, 1.0f, 1.0f ) ) );
		FEditMesh MirroredAlone = NEditMesh::MakeRamp( FVector3( 2, 1, 2 ) );
		C.Check( Near( Mirrored.ComputeVolume(), 8.0f + MirroredAlone.ComputeVolume() ), "a mirrored copy (negative scale) keeps its faces facing outward: the volume is added, not subtracted (%.3f)", Mirrored.ComputeVolume() );
		// 材質: 同じものは共有、違うものは足す。こちらに無くて元が既定なら、足さない
		FEditMesh Texted = Box2();
		FEditMeshMaterial Brick;
		Brick.Name = "Brick";
		Brick.Texture = "Brick.png";
		Texted.Materials.Add( Brick );
		FEditMesh Painted = NEditMesh::MakeBox( FVector3::One );
		Painted.Materials.Add( Brick );
		FEditMeshMaterial Grass;
		Grass.Name = "Grass";
		Painted.Materials.Add( Grass );
		Painted.Faces[0].Material = 1;
		NEditMesh::AppendMesh( Texted, Painted, FTransform::Identity );
		C.Check( Texted.Materials.Num() == 2 && Texted.Materials[1].Name == "Grass" && Texted.Faces[6].Material == 1 && Texted.Faces[7].Material == 0, "materials: Brick (equal) is shared, Grass is added (2 now), and the faces point at the right ones (%d)", Texted.Materials.Num() );
		FEditMesh Plain = Box2();
		FEditMesh Marked = Box2();
		Marked.Materials.Add( Grass );
		Marked.Materials.Add( Brick );
		NEditMesh::AppendMesh( Marked, Plain, FTransform::Identity );
		C.Check( Marked.Materials.Num() == 3 && NEditMesh::IsSameMaterial( Marked.Materials[2], FEditMeshMaterial() ) && Marked.Faces[6].Material == 2, "a source with no materials, onto a mesh with materials: the default material is added and used" );
		FEditMesh Empty;
		C.Check( NEditMesh::AppendMesh( Empty, Box2(), FTransform::Identity ) == 0 && Empty.GetNumFaces() == 6 && NEditMesh::AppendMesh( Empty, FEditMesh(), FTransform::Identity ) == 6 && Empty.GetNumFaces() == 6, "appending onto an empty mesh starts at 0; appending nothing changes nothing" );

		// 三角形から作る
		C.Group = "mesh-import";
		const FEditMeshBuild Build = NEditMesh::BuildRenderMesh( Box2() );
		TArray<FVector3> Positions;
		for ( const FMeshVertex& V : Build.Vertices ) Positions.Add( FVector3( V.Position[0], V.Position[1], V.Position[2] ) );
		const FEditMesh Welded = NEditMesh::MakeFromTriangles( TArrayView<const FVector3>( Positions ), TArrayView<const uint32>( Build.Indices ), 1.0e-4f );
		C.Check( Welded.GetNumVertices() == 8 && Welded.GetNumFaces() == 12 && Near( Welded.ComputeVolume(), 8.0f ) && IsClosed( Welded ) && Welded.Validate().IsEmpty(), "the 24 corner vertices of a box weld back into 8, with 12 triangles, the same volume, closed (%d vertices)", Welded.GetNumVertices() );
		const FEditMesh Loose = NEditMesh::MakeFromTriangles( TArrayView<const FVector3>( Positions ), TArrayView<const uint32>( Build.Indices ), 0.0f );
		C.Check( Loose.GetNumVertices() == 24 && Loose.GetNumFaces() == 12 && Near( Loose.ComputeVolume(), 8.0f ), "without a weld distance every vertex stays (24)" );
		TArray<FVector3> Few;
		Few.Add( FVector3( 0, 0, 0 ) );
		Few.Add( FVector3( 0, 0, 1 ) );
		Few.Add( FVector3( 1, 0, 1 ) );
		Few.Add( FVector3( 2, 0, 2 ) );
		TArray<uint32> Bad;
		for ( const uint32 Index : { 0u, 1u, 2u, 0u, 1u, 1u, 0u, 2u, 3u, 0u, 1u, 9u, 0u, 1u } ) Bad.Add( Index );   // 1 つめ: 良い / 同じ頂点 / 一直線 / 範囲外 / 端数
		const FEditMesh Clean = NEditMesh::MakeFromTriangles( TArrayView<const FVector3>( Few ), TArrayView<const uint32>( Bad ), 0.0f );
		C.Check( Clean.GetNumFaces() == 1 && Clean.GetNumVertices() == 3 && Near3( Clean.ComputeFaceNormal( 0 ), FVector3::Up ), "bad triangles are dropped (a repeated vertex, three on a line, an index out of range, a leftover pair), unused vertices are removed, and the front stays the front (%d faces)", Clean.GetNumFaces() );
		C.Check( NEditMesh::MakeFromTriangles( TArrayView<const FVector3>(), TArrayView<const uint32>(), 0.1f ).IsEmpty(), "nothing in, nothing out" );
	}
}

void NWorldTest::RunMeshOpsTests( FChecker& C )
{
	TestSelection( C );
	TestExtrude( C );
	TestDeleteMerge( C );
	TestMaterialsAndAppend( C );
}
