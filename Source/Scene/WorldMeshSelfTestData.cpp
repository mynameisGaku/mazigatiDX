#include <algorithm>
#include <cmath>
#include <limits>

#include "Library/Core/Collection/Map.h"
#include "Library/Framework/World/Mesh/EditMesh.h"
#include "Library/Framework/World/Mesh/EditMeshBuild.h"
#include "Library/Framework/World/Mesh/EditMeshPrimitives.h"
#include "Library/Framework/World/WorldCollision.h"
#include "Library/Framework/World/WorldDefinition.h"
#include "Library/Framework/World/WorldValidate.h"
#include "WorldMeshSelfTestSupport.h"

using namespace NAssetTest;
using namespace NWorldMeshTest;

//=============================================================================
// 編集メッシュの、データ・基本の形・描く形・JSON・検証の確認
//   DxLib もシーンも使わない。期待する値は、手で求めた数（体積・面積・個数）か、形の不変量（閉じている・外向き）。
//=============================================================================
namespace
{
	bool Near( float32 A, float32 B, float32 Tolerance = 1.0e-3f ) { return std::fabs( A - B ) <= Tolerance; }
	bool Near3( const FVector3& A, const FVector3& B, float32 Tolerance = 1.0e-3f ) { return Near( A.x, B.x, Tolerance ) && Near( A.y, B.y, Tolerance ) && Near( A.z, B.z, Tolerance ); }

	// 面を、頂点の位置で 1 つ足す（番号を気にせずに、小さな形を作る）
	int32 AddVertex( FEditMesh& Mesh, float32 X, float32 Y, float32 Z ) { return Mesh.Vertices.Add( FVector3( X, Y, Z ) ); }

	FEditMeshFace MakeFace( std::initializer_list<int32> Indices, int32 Material = 0 )
	{
		FEditMeshFace Face;
		for ( const int32 Index : Indices ) Face.Indices.Add( Index );
		Face.Material = Material;
		return Face;
	}

	// 地面（y = 0）の、上向きの正方形（-1..1）
	FEditMesh MakeSquare()
	{
		FEditMesh Mesh;
		AddVertex( Mesh, -1, 0, -1 );
		AddVertex( Mesh, -1, 0, 1 );
		AddVertex( Mesh, 1, 0, 1 );
		AddVertex( Mesh, 1, 0, -1 );
		Mesh.Faces.Add( MakeFace( { 0, 1, 2, 3 } ) );
		return Mesh;
	}

	int32 CountIssues( const TArray<FEditMeshIssue>& Issues, bool bError, const char* Contains = nullptr )
	{
		int32 Count = 0;
		for ( const FEditMeshIssue& Issue : Issues )
		{
			if ( Issue.bError == bError && ( Contains == nullptr || Issue.Message.Contains( Contains ) ) ) ++Count;
		}
		return Count;
	}

	//=========================================================================
	// 基本の形
	//=========================================================================
	void TestPrimitives( FChecker& C )
	{
		C.Group = "mesh-primitives";
		const FEditMesh Box = NEditMesh::MakeBox( FVector3( 2.0f, 3.0f, 4.0f ) );
		FVector3 Min, Max;
		C.Check( Box.GetNumVertices() == 8 && Box.GetNumFaces() == 6 && Box.GetNumTriangles() == 12, "a box: 8 vertices shared between 6 faces (%d, %d)", Box.GetNumVertices(), Box.GetNumFaces() );
		C.Check( Box.GetBounds( Min, Max ) && Near3( Min, FVector3( -1.0f, 0.0f, -2.0f ) ) && Near3( Max, FVector3( 1.0f, 3.0f, 2.0f ) ), "its base is at y = 0, centred on x and z" );
		C.Check( Near( Box.ComputeVolume(), 24.0f ), "its volume is 2 x 3 x 4 (positive: the faces look outward) (%.3f)", Box.ComputeVolume() );
		C.Check( IsClosed( Box ) && FacesLookOutward( Box ), "it is closed (every edge is shared by two faces, once each way) and every face looks away from the middle" );

		const FEditMesh Plane = NEditMesh::MakePlane( 2.0f, 3.0f, 2, 3 );
		C.Check( Plane.GetNumFaces() == 6 && Plane.GetNumVertices() == 12, "a plane of 2 x 3 divisions: 6 faces, 12 shared vertices (%d, %d)", Plane.GetNumFaces(), Plane.GetNumVertices() );
		bool bUp = true;
		for ( int32 Face = 0; Face < Plane.GetNumFaces(); ++Face ) bUp = bUp && Near3( Plane.ComputeFaceNormal( Face ), FVector3::Up );
		C.Check( bUp && Near( Plane.ComputeFaceArea( 0 ), 1.0f ) && Plane.GetBounds( Min, Max ) && Near3( Min, FVector3( -1.0f, 0.0f, -1.5f ) ) && Near3( Max, FVector3( 1.0f, 0.0f, 1.5f ) ), "a plane faces up, its cells have equal area, and it spans the size" );

		const FEditMesh Cylinder = NEditMesh::MakeCylinder( 1.0f, 2.0f, 16 );
		C.Check( Cylinder.GetNumFaces() == 18 && Cylinder.GetNumVertices() == 32, "a 16-sided cylinder: 16 sides and 2 caps, 32 vertices (%d, %d)", Cylinder.GetNumFaces(), Cylinder.GetNumVertices() );
		C.Check( Near( Cylinder.ComputeVolume(), 8.0f * std::sin( 2.0f * 3.14159265f / 16.0f ) * 2.0f, 2.0e-3f ) && IsClosed( Cylinder ) && FacesLookOutward( Cylinder ), "its volume is the area of the 16-gon times the height (%.4f)", Cylinder.ComputeVolume() );
		const FEditMesh Tube = NEditMesh::MakeCylinder( 1.0f, 2.0f, 8, false );
		C.Check( Tube.GetNumFaces() == 8 && Tube.GetNumVertices() == 16, "without caps it is a tube of 8 faces" );

		const FEditMesh Sphere = NEditMesh::MakeSphere( 1.0f, 8, 4 );
		C.Check( Sphere.GetNumFaces() == 32 && Sphere.GetNumVertices() == 26 && IsClosed( Sphere ) && FacesLookOutward( Sphere ), "a sphere of 8 segments and 4 rings: 32 faces, 26 vertices, closed, facing outward (%d, %d)", Sphere.GetNumFaces(), Sphere.GetNumVertices() );
		const FEditMesh FineSphere = NEditMesh::MakeSphere( 2.0f, 32, 16 );
		const float32 Ideal = 4.0f / 3.0f * 3.14159265f * 8.0f;
		C.Check( FineSphere.ComputeVolume() > Ideal * 0.95f && FineSphere.ComputeVolume() < Ideal && FineSphere.GetBounds( Min, Max ) && Near3( Min, FVector3( -2.0f, 0.0f, -2.0f ), 5.0e-2f ) && Near3( Max, FVector3( 2.0f, 4.0f, 2.0f ), 5.0e-2f ),
				 "a fine sphere of radius 2 stands on y = 0 and holds 95..100%% of the ideal volume (%.3f of %.3f)", FineSphere.ComputeVolume(), Ideal );

		const FEditMesh Ramp = NEditMesh::MakeRamp( FVector3( 2.0f, 1.0f, 3.0f ) );
		C.Check( Ramp.GetNumFaces() == 5 && Ramp.GetNumVertices() == 6 && Near( Ramp.ComputeVolume(), 3.0f ) && IsClosed( Ramp ) && FacesLookOutward( Ramp ), "a ramp: 5 faces, 6 vertices, half of the box volume (%.3f)", Ramp.ComputeVolume() );
		C.Check( Near3( Ramp.ComputeFaceNormal( 1 ), FVector3( 0.0f, 3.0f, -1.0f ).Normalized() ), "its slope faces up and back, away from the high wall" );

		const FEditMesh Stairs = NEditMesh::MakeStairs( FVector3( 2.0f, 2.0f, 3.0f ), 5 );
		C.Check( Stairs.GetNumFaces() == 22 && Near( Stairs.ComputeVolume(), 2.0f * 0.6f * 0.4f * 15.0f ), "5 stairs: 4 faces per step and a back and a bottom; the volume is the sum of the steps (%.3f, %d faces)", Stairs.ComputeVolume(), Stairs.GetNumFaces() );

		// 引数の補正と、既定の形
		const FEditMesh Tiny = NEditMesh::MakeBox( FVector3::Zero );
		C.Check( Tiny.GetNumFaces() == 6 && Tiny.ComputeFaceArea( 0 ) > 0.0f && Tiny.Validate().IsEmpty(), "a box of size 0 is made at the smallest size, and is valid" );
		C.Check( NEditMesh::MakeCylinder( 1.0f, 1.0f, 1 ).GetNumFaces() == 5 && NEditMesh::MakeSphere( 1.0f, 1, 0 ).GetNumFaces() == 6, "too few segments / rings are raised to the minimum (a triangular prism: 5 faces; a double pyramid: 6)" );
		C.Check( NEditMesh::MakeStairs( FVector3::One, 0 ).GetNumFaces() == 6 && NEditMesh::MakeStairs( FVector3::One, 3 ).GetNumFaces() == 14, "stairs have at least 1 step" );
		// 上限（多すぎる分割は、上限に丸める）
		C.Check( NEditMesh::MakePlane( 1.0f, 1.0f, 1000, 1 ).GetNumFaces() == 256 && NEditMesh::MakePlane( 1.0f, 1.0f, 1, 1000 ).GetNumFaces() == 256, "a plane is cut into at most 256 cells each way" );
		C.Check( NEditMesh::MakeCylinder( 1.0f, 1.0f, 1000, false ).GetNumFaces() == 256 && NEditMesh::MakeSphere( 1.0f, 1000, 4 ).GetNumFaces() == 256 * 4 && NEditMesh::MakeSphere( 1.0f, 4, 1000 ).GetNumFaces() == 4 * 256 && NEditMesh::MakeStairs( FVector3::One, 1000 ).GetNumFaces() == 128 * 4 + 2,
				 "a cylinder, a sphere and stairs are cut into at most 256 segments, 256 rings and 128 steps" );
		bool bAllValid = true;
		TArray<FString> Names;
		for ( int32 i = 0; i < NEditMesh::NumPrimitives; ++i )
		{
			const EEditMeshPrimitive Kind = static_cast<EEditMeshPrimitive>( i );
			const FEditMesh Mesh = NEditMesh::MakePrimitive( Kind );
			bAllValid = bAllValid && !Mesh.IsEmpty() && CountIssues( Mesh.Validate(), true ) == 0 && Mesh.GetBounds( Min, Max ) && Near( Min.y, 0.0f );
			Names.AddUnique( FString( NEditMesh::ToString( Kind ) ) );
		}
		C.Check( bAllValid && Names.Num() == NEditMesh::NumPrimitives, "every default primitive is valid, stands on y = 0, and has its own name" );
	}

	//=========================================================================
	// データ
	//=========================================================================
	void TestData( FChecker& C )
	{
		C.Group = "mesh-data";
		FEditMesh Square = MakeSquare();
		C.Check( Near3( Square.ComputeFaceNormal( 0 ), FVector3::Up ) && Near( Square.ComputeFaceArea( 0 ), 4.0f ) && Near3( Square.ComputeFaceCenter( 0 ), FVector3::Zero ), "a square seen from above, listed clockwise (x right, z up): faces up, area 4, centred" );
		FEditMesh Reversed = Square;
		Reversed.Faces[0] = MakeFace( { 3, 2, 1, 0 } );
		C.Check( Near3( Reversed.ComputeFaceNormal( 0 ), FVector3::Down ), "listed the other way, it faces down" );
		C.Check( FVector3::Dot( FVector3::Cross( Square.Vertices[1] - Square.Vertices[0], Square.Vertices[2] - Square.Vertices[0] ), FVector3::Up ) > 0.0f, "the front is (V1 - V0) x (V2 - V0), as for the physics mesh" );

		// 三角形の分け方
		TArray<int32> FaceOf;
		TArray<uint32> Triangles = Square.BuildTriangles( &FaceOf );
		C.Check( Triangles.Num() == 6 && Triangles[0] == 0 && Triangles[1] == 1 && Triangles[2] == 2 && Triangles[3] == 0 && Triangles[4] == 2 && Triangles[5] == 3 && FaceOf.Num() == 2 && FaceOf[0] == 0 && FaceOf[1] == 0, "a quad is a fan of 2 triangles around its first vertex" );
		FEditMesh Mixed = Square;
		const int32 Apex = AddVertex( Mixed, 0, 2, 0 );
		Mixed.Faces.Add( MakeFace( { 0, 1, Apex } ) );             // 有効
		Mixed.Faces.Add( MakeFace( { 0, 1 } ) );                    // 頂点が 2 つ
		Mixed.Faces.Add( MakeFace( { 0, 1, 99 } ) );                // 範囲外
		Mixed.Faces.Add( MakeFace( { 0, 1, 1 } ) );                 // 同じ頂点を 2 度
		Mixed.Faces.Add( MakeFace( { 0, 3, 2, 1, Apex } ) );        // 五角形（有効）
		C.Check( Mixed.IsFaceValid( 0 ) && Mixed.IsFaceValid( 1 ) && !Mixed.IsFaceValid( 2 ) && !Mixed.IsFaceValid( 3 ) && !Mixed.IsFaceValid( 4 ) && Mixed.IsFaceValid( 5 ) && !Mixed.IsFaceValid( -1 ) && !Mixed.IsFaceValid( 6 ), "a face is valid with 3 or more distinct vertices that exist" );
		Triangles = Mixed.BuildTriangles( &FaceOf );
		C.Check( Triangles.Num() == ( 2 + 1 + 3 ) * 3 && Mixed.GetNumTriangles() == 6 && FaceOf.Num() == 6 && FaceOf[2] == 1 && FaceOf[5] == 5, "unusable faces give no triangles; a pentagon gives 3 (%d)", Triangles.Num() / 3 );

		// 大きさ・体積
		FVector3 Min = FVector3( 9, 9, 9 ), Max = FVector3( -9, -9, -9 );
		C.Check( !FEditMesh().GetBounds( Min, Max ) && Near3( Min, FVector3( 9, 9, 9 ) ), "an empty mesh has no bounds, and the arguments are left alone" );
		FEditMesh Spare = Square;
		AddVertex( Spare, 100, 100, 100 );   // どの面も使わない
		C.Check( Spare.GetBounds( Min, Max ) && Near3( Min, FVector3( -1, 0, -1 ) ) && Near3( Max, FVector3( 1, 0, 1 ) ), "bounds cover the vertices the faces use, not the spare ones" );
		FEditMesh Flipped = NEditMesh::MakeBox( FVector3::One );
		for ( FEditMeshFace& Face : Flipped.Faces )
		{
			for ( int32 i = 0, j = Face.Indices.Num() - 1; i < j; ++i, --j ) std::swap( Face.Indices[i], Face.Indices[j] );
		}
		C.Check( NEditMesh::MakeBox( FVector3::One ).ComputeVolume() > 0.99f && Flipped.ComputeVolume() < -0.99f, "turning every face around makes the volume negative (inside out)" );

		// 材質
		FEditMesh Painted = Square;
		FEditMeshMaterial Red;
		Red.Name = "Red";
		Red.Color = FColor32( 255, 0, 0, 255 );
		Painted.Materials.Add( Red );
		C.Check( Painted.GetMaterial( 0 ).Name == "Red" && Painted.GetMaterial( 1 ).Name.IsEmpty() && Painted.GetMaterial( -1 ).Name.IsEmpty() && Painted.GetMaterial( 1 ).Color.r == 200 && Square.GetMaterial( 0 ).Tile == 1.0f, "a material number out of range gives the default material" );

		// 内容の識別
		const uint64 Base = Painted.ComputeHash();
		FEditMesh Copy = Painted;
		C.Check( Copy.ComputeHash() == Base && NEditMesh::MakeBox( FVector3::One ).ComputeHash() == NEditMesh::MakeBox( FVector3::One ).ComputeHash() && NEditMesh::MakeBox( FVector3::One ).ComputeHash() != NEditMesh::MakeBox( FVector3( 1.0f, 1.0f, 1.1f ) ).ComputeHash(), "equal meshes have equal hashes, different ones do not" );
		const auto HashAfter = [&]( const auto& Change ) { FEditMesh Changed = Painted; Change( Changed ); return Changed.ComputeHash(); };
		TArray<uint64> Hashes;
		Hashes.Add( Base );
		Hashes.Add( HashAfter( []( FEditMesh& M ) { M.Vertices[0].x += 0.5f; } ) );
		Hashes.Add( HashAfter( []( FEditMesh& M ) { M.Faces[0].Indices[0] = 1; M.Faces[0].Indices[1] = 0; } ) );
		Hashes.Add( HashAfter( []( FEditMesh& M ) { M.Faces[0].Material = 1; } ) );
		Hashes.Add( HashAfter( []( FEditMesh& M ) { M.Materials[0].Color.r = 1; } ) );
		Hashes.Add( HashAfter( []( FEditMesh& M ) { M.Materials[0].Color.g = 1; } ) );
		Hashes.Add( HashAfter( []( FEditMesh& M ) { M.Materials[0].Color.b = 1; } ) );
		Hashes.Add( HashAfter( []( FEditMesh& M ) { M.Materials[0].Texture = "a.png"; } ) );
		Hashes.Add( HashAfter( []( FEditMesh& M ) { M.Materials[0].Tile = 2.0f; } ) );
		Hashes.Add( HashAfter( []( FEditMesh& M ) { M.Materials[0].Offset.y = 0.5f; } ) );
		Hashes.Add( HashAfter( []( FEditMesh& M ) { M.Materials[0].bTwoSided = true; } ) );
		Hashes.Add( HashAfter( []( FEditMesh& M ) { M.Materials[0].Name = "Blue"; } ) );
		// 文字列の境目だけが違うもの（"ab" + "c" と "a" + "bc"）も、別のハッシュ
		Hashes.Add( HashAfter( []( FEditMesh& M ) { M.Materials[0].Name = "ab"; M.Materials[0].Texture = "c"; } ) );
		Hashes.Add( HashAfter( []( FEditMesh& M ) { M.Materials[0].Name = "a"; M.Materials[0].Texture = "bc"; } ) );
		Hashes.Add( HashAfter( []( FEditMesh& M ) { M.SmoothAngle = 30.0f; } ) );
		Hashes.Add( HashAfter( []( FEditMesh& M ) { M.Vertices.Add( FVector3::One ); } ) );
		TArray<uint64> Unique;
		for ( const uint64 Hash : Hashes ) Unique.AddUnique( Hash );
		C.Check( Unique.Num() == Hashes.Num(), "every kind of change gives a different hash (%d of %d are different)", Unique.Num(), Hashes.Num() );
		// 区切り: 名前の最後の文字を色のバイトへずらしても、並びが同じにならない（"a" + 色 (98, 99, 100) + "d" と、"ab" + 色 (99, 100, 100) + ""）
		FEditMesh Shifted1 = Painted, Shifted2 = Painted;
		Shifted1.Materials[0].Name = "a";
		Shifted1.Materials[0].Color = FColor32( 98, 99, 100, 255 );
		Shifted1.Materials[0].Texture = "d";
		Shifted2.Materials[0].Name = "ab";
		Shifted2.Materials[0].Color = FColor32( 99, 100, 100, 255 );
		Shifted2.Materials[0].Texture = FString();
		C.Check( Shifted1.ComputeHash() != Shifted2.ComputeHash(), "moving a character from the name into the colour bytes changes the hash (the strings are separated)" );
		FEditMesh SplitA = Painted, SplitB = Painted;
		SplitA.Materials[0].Name = "ab";
		SplitA.Materials[0].Texture = "c";
		SplitB.Materials[0].Name = "a";
		SplitB.Materials[0].Texture = "bc";
		C.Check( SplitA.ComputeHash() != SplitB.ComputeHash(), "the end of one text does not run into the next (ab + c is not a + bc)" );
	}

	//=========================================================================
	// 描く形
	//=========================================================================
	void TestBuild( FChecker& C )
	{
		C.Group = "mesh-build";
		const FEditMesh Box = NEditMesh::MakeBox( FVector3( 2.0f, 2.0f, 2.0f ) );
		FEditMeshBuild Build = NEditMesh::BuildRenderMesh( Box );
		C.Check( Build.Vertices.Num() == 24 && Build.Indices.Num() == 36 && Build.Batches.Num() == 1 && Build.Batches[0].Material == -1 && Build.Batches[0].StartIndex == 0 && Build.Batches[0].NumIndices == 36 && Build.GetNumTriangles() == 12,
				 "a box is 24 corner vertices (a vertex per face corner), 12 triangles, 1 batch of the default material (%d, %d)", Build.Vertices.Num(), Build.Indices.Num() );
		bool bInRange = true;
		for ( const uint32 Index : Build.Indices ) bInRange = bInRange && Index < static_cast<uint32>( Build.Vertices.Num() );
		C.Check( bInRange, "every index is a vertex" );

		// 三角形の向きが、面の向き（法線）と合っている。法線は、面の向き
		bool bWindingMatches = true, bFlat = true;
		for ( int32 i = 0; i + 2 < Build.Indices.Num(); i += 3 )
		{
			const FMeshVertex& A = Build.Vertices[Build.Indices[i]];
			const FMeshVertex& B = Build.Vertices[Build.Indices[i + 1]];
			const FMeshVertex& D = Build.Vertices[Build.Indices[i + 2]];
			const FVector3 Geometric = FVector3::Cross( FVector3( B.Position[0] - A.Position[0], B.Position[1] - A.Position[1], B.Position[2] - A.Position[2] ), FVector3( D.Position[0] - A.Position[0], D.Position[1] - A.Position[1], D.Position[2] - A.Position[2] ) ).Normalized();
			const FVector3 Stored( A.Normal[0], A.Normal[1], A.Normal[2] );
			bWindingMatches = bWindingMatches && FVector3::Dot( Geometric, Stored ) > 0.999f;
			bFlat = bFlat && Near( Stored.Length(), 1.0f ) && ( std::fabs( Stored.x ) > 0.999f || std::fabs( Stored.y ) > 0.999f || std::fabs( Stored.z ) > 0.999f );
		}
		C.Check( bWindingMatches && bFlat, "each triangle's winding agrees with its stored normal, and a box's normals are flat and along the axes" );

		// 多角形は、扇形
		FEditMesh Pentagon;
		for ( int32 i = 0; i < 5; ++i )
		{
			const float32 Angle = -2.0f * 3.14159265f * static_cast<float32>( i ) / 5.0f;
			AddVertex( Pentagon, std::cos( Angle ), 0.0f, std::sin( Angle ) );
		}
		Pentagon.Faces.Add( MakeFace( { 0, 1, 2, 3, 4 } ) );
		Build = NEditMesh::BuildRenderMesh( Pentagon );
		C.Check( Build.Vertices.Num() == 5 && Build.GetNumTriangles() == 3 && Near3( FVector3( Build.Vertices[0].Normal[0], Build.Vertices[0].Normal[1], Build.Vertices[0].Normal[2] ), FVector3::Up ), "a pentagon is 5 vertices and 3 triangles, facing up when listed clockwise from above (%d, %d)", Build.Vertices.Num(), Build.GetNumTriangles() );

		// 使えない面・面積のない面は出さない
		FEditMesh Broken = MakeSquare();
		AddVertex( Broken, 3, 0, 3 );
		AddVertex( Broken, 4, 0, 4 );
		AddVertex( Broken, 5, 0, 5 );   // 一直線の 3 点（面積 0）
		Broken.Faces.Add( MakeFace( { 4, 5, 6 } ) );
		Broken.Faces.Add( MakeFace( { 0, 1, 99 } ) );
		Broken.Faces.Add( MakeFace( { 0, 1 } ) );
		Build = NEditMesh::BuildRenderMesh( Broken );
		C.Check( Build.Vertices.Num() == 4 && Build.GetNumTriangles() == 2, "faces that cannot be drawn (out of range, 2 vertices, no area) give nothing (%d vertices)", Build.Vertices.Num() );
		C.Check( NEditMesh::BuildRenderMesh( FEditMesh() ).Vertices.IsEmpty() && NEditMesh::BuildRenderMesh( FEditMesh() ).Batches.IsEmpty(), "an empty mesh gives an empty build" );

		// 材質ごとのまとまり（番号の小さい順。範囲外は既定 = -1 で、先頭）
		FEditMesh Banded;
		FEditMeshMaterial Material;
		Banded.Materials.Add( Material );
		Banded.Materials.Add( Material );
		for ( int32 i = 0; i < 5; ++i )
		{
			const float32 X = static_cast<float32>( i ) * 3.0f;
			const int32 A = AddVertex( Banded, X, 0, 0 );
			const int32 B = AddVertex( Banded, X, 0, 1 );
			const int32 D = AddVertex( Banded, X + 1, 0, 1 );
			const int32 Materials[5] = { 1, 5, 0, -3, 1 };
			Banded.Faces.Add( MakeFace( { A, B, D }, Materials[i] ) );
		}
		Build = NEditMesh::BuildRenderMesh( Banded );
		C.Check( Build.Batches.Num() == 3 && Build.Batches[0].Material == -1 && Build.Batches[0].NumIndices == 6 && Build.Batches[1].Material == 0 && Build.Batches[1].NumIndices == 3 && Build.Batches[2].Material == 1 && Build.Batches[2].NumIndices == 6
					 && Build.Batches[0].StartIndex == 0 && Build.Batches[1].StartIndex == 6 && Build.Batches[2].StartIndex == 9 && Build.Indices.Num() == 15,
				 "triangles are grouped by material: the default one (two faces: numbers 5 and -3) first, then 0, then 1 (two faces) (%d batches)", Build.Batches.Num() );

		// なめらかさ
		FEditMesh Sphere = NEditMesh::MakeSphere( 1.0f, 16, 8 );
		const auto RadialDot = [&]( const FEditMeshBuild& B )
		{
			float32 Worst = 1.0f;
			for ( const FMeshVertex& V : B.Vertices )
			{
				const FVector3 Radial = ( FVector3( V.Position[0], V.Position[1] - 1.0f, V.Position[2] ) ).Normalized();
				Worst = std::min( Worst, FVector3::Dot( Radial, FVector3( V.Normal[0], V.Normal[1], V.Normal[2] ) ) );
			}
			return Worst;
		};
		const float32 FlatWorst = RadialDot( NEditMesh::BuildRenderMesh( Sphere ) );
		Sphere.SmoothAngle = 40.0f;
		const float32 SmoothWorst = RadialDot( NEditMesh::BuildRenderMesh( Sphere ) );
		C.Check( FlatWorst < 0.995f && SmoothWorst > 0.97f, "smoothing makes a sphere's normals follow the surface (worst agreement with the radius: flat %.3f, smooth %.3f)", FlatWorst, SmoothWorst );
		FEditMesh Hard = NEditMesh::MakeBox( FVector3::One );
		Hard.SmoothAngle = 60.0f;
		bool bAxis = true;
		for ( const FMeshVertex& V : NEditMesh::BuildRenderMesh( Hard ).Vertices ) bAxis = bAxis && ( std::fabs( V.Normal[0] ) > 0.999f || std::fabs( V.Normal[1] ) > 0.999f || std::fabs( V.Normal[2] ) > 0.999f );
		Hard.SmoothAngle = 91.0f;
		bool bMixed = true;
		for ( const FMeshVertex& V : NEditMesh::BuildRenderMesh( Hard ).Vertices ) bMixed = bMixed && Near( std::fabs( V.Normal[0] ), 0.57735f, 0.01f ) && Near( std::fabs( V.Normal[1] ), 0.57735f, 0.01f ) && Near( std::fabs( V.Normal[2] ), 0.57735f, 0.01f );
		Hard.SmoothAngle = 90.0f;
		bool bExact = true;
		for ( const FMeshVertex& V : NEditMesh::BuildRenderMesh( Hard ).Vertices ) bExact = bExact && Near( std::fabs( V.Normal[0] ), 0.57735f, 0.01f ) && Near( std::fabs( V.Normal[1] ), 0.57735f, 0.01f );
		C.Check( bAxis && bMixed && bExact, "a box keeps its sharp edges below the 90-degree limit, and shares the corner normals at and above it" );

		// 画像の座標: 面の向きでいちばん大きい軸に沿って。Tile で割り、Offset を足す
		FEditMeshMaterial Mat;
		Mat.Tile = 2.0f;
		Mat.Offset = FVector2( 0.25f, 0.5f );
		const FVector3 P( 3.0f, 5.0f, 7.0f );
		const auto UV = [&]( const FVector3& N ) { return NEditMesh::ComputeBoxUV( P, N, Mat ); };
		FVector2 T = UV( FVector3::Up );
		C.Check( Near( T.x, 3.0f / 2.0f + 0.25f ) && Near( T.y, -7.0f / 2.0f + 0.5f ), "up: u = x, v = -z (divided by the tile, plus the offset) (%.3f, %.3f)", T.x, T.y );
		T = UV( FVector3::Down );
		C.Check( Near( T.x, -3.0f / 2.0f + 0.25f ) && Near( T.y, -7.0f / 2.0f + 0.5f ), "down: u = -x, v = -z" );
		T = UV( FVector3( 1.0f, 0.1f, 0.0f ) );
		C.Check( Near( T.x, 7.0f / 2.0f + 0.25f ) && Near( T.y, -5.0f / 2.0f + 0.5f ), "facing +x: u = z, v = -y" );
		T = UV( FVector3( -1.0f, 0.0f, 0.1f ) );
		C.Check( Near( T.x, -7.0f / 2.0f + 0.25f ) && Near( T.y, -5.0f / 2.0f + 0.5f ), "facing -x: u = -z, v = -y" );
		T = UV( FVector3( 0.1f, 0.2f, 1.0f ) );
		C.Check( Near( T.x, -3.0f / 2.0f + 0.25f ) && Near( T.y, -5.0f / 2.0f + 0.5f ), "facing +z: u = -x, v = -y (the +x side is on the left, seen from the front)" );
		T = UV( FVector3( 0.1f, 0.2f, -1.0f ) );
		C.Check( Near( T.x, 3.0f / 2.0f + 0.25f ) && Near( T.y, -5.0f / 2.0f + 0.5f ), "facing -z: u = x, v = -y" );
		T = UV( FVector3( 1.0f, 1.0f, 1.0f ) );
		C.Check( Near( T.x, 3.0f / 2.0f + 0.25f ) && Near( T.y, -7.0f / 2.0f + 0.5f ), "a tie between axes picks y first" );
		FEditMeshMaterial Bad;
		Bad.Tile = 0.0f;
		T = NEditMesh::ComputeBoxUV( P, FVector3::Up, Bad );
		C.Check( Near( T.x, 3.0f ) && Near( T.y, -7.0f ), "a tile of 0 is treated as 1" );
		// 面の UV が、頂点の動きに追従する（保存した UV がない）
		FEditMesh Wall = NEditMesh::MakeBox( FVector3( 4.0f, 2.0f, 0.2f ) );
		FEditMeshMaterial Brick;
		Brick.Tile = 4.0f;
		Wall.Materials.Add( Brick );
		float32 MinU = 1.0e9f, MaxU = -1.0e9f;
		for ( const FMeshVertex& V : NEditMesh::BuildRenderMesh( Wall ).Vertices )
		{
			if ( std::fabs( V.Normal[2] ) > 0.99f )
			{
				MinU = std::min( MinU, V.U );
				MaxU = std::max( MaxU, V.U );
			}
		}
		C.Check( Near( MaxU - MinU, 1.0f ), "a 4 m wall with a 4 m tile shows the picture once from end to end (%.3f)", MaxU - MinU );
		// 大きすぎて、法線の長さが無限になる面は、描かない
		FEditMesh Overflow = MakeSquare();
		Overflow.Vertices[2].x = 1.0e30f;
		const FEditMeshBuild OverflowBuild = NEditMesh::BuildRenderMesh( Overflow );
		C.Check( OverflowBuild.Vertices.IsEmpty() && OverflowBuild.Indices.IsEmpty(), "a face so big that its normal overflows is not built" );
		// 滑らかな法線は、面積で重みづけして混ぜる（広い上の面と、狭い横の面が、尾根の 2 つの頂点を共有）
		FEditMesh Ridge;
		for ( const FVector3& V : { FVector3( 0, 0, 0 ), FVector3( 0, 0, 2 ), FVector3( 2, 0, 2 ), FVector3( 2, 0, 0 ), FVector3( 2, -1, 2 ), FVector3( 2, -1, 0 ) } ) Ridge.Vertices.Add( V );
		Ridge.Faces.Add( MakeFace( { 0, 1, 2, 3 } ) );   // 上向き、面積 4
		Ridge.Faces.Add( MakeFace( { 3, 2, 4, 5 } ) );   // +x 向き、面積 2（頂点 2 と 3 を共有）
		Ridge.SmoothAngle = 90.0f;
		const FEditMeshBuild RidgeBuild = NEditMesh::BuildRenderMesh( Ridge );
		C.Check( RidgeBuild.Vertices.Num() == 8 && Near( RidgeBuild.Vertices[3].Normal[0], 0.4472f, 0.01f ) && Near( RidgeBuild.Vertices[3].Normal[1], 0.8944f, 0.01f ) && Near( RidgeBuild.Vertices[3].Normal[2], 0.0f ),
				 "a smooth normal mixes the faces weighted by their area (a wide top and a narrow side: (0.447, 0.894, 0), not the plain average)" );
	}

	//=========================================================================
	// JSON
	//=========================================================================
	void TestJson( FChecker& C )
	{
		C.Group = "mesh-json";
		FEditMesh Mesh = NEditMesh::MakeBox( FVector3( 2.0f, 1.0f, 3.0f ) );
		const NlohmannJson Plain = Mesh.ToJson();
		const FJson P( Plain );
		C.Check( P.size() == 2 && P.contains( "Vertices" ) && P.contains( "Faces" ) && P["Vertices"].size() == 8 && P["Faces"].size() == 6 && P["Faces"][0].size() == 4, "a plain box writes only its vertices and faces" );

		FEditMeshMaterial Wall;
		Wall.Name = "Wall";
		Wall.Color = FColor32( 255, 128, 0, 255 );
		Wall.Texture = "Textures/Brick.png";
		Wall.Tile = 2.5f;
		Wall.Offset = FVector2( 0.25f, 0.5f );
		Wall.bTwoSided = true;
		Mesh.Materials.Add( FEditMeshMaterial() );
		Mesh.Materials.Add( Wall );
		Mesh.Faces[2].Material = 1;
		Mesh.SmoothAngle = 30.0f;
		const NlohmannJson Json = Mesh.ToJson();
		const FJson J( Json );
		C.Check( J["FaceMaterials"].size() == 6 && J["FaceMaterials"][2] == 1 && J["FaceMaterials"][0] == 0 && J["Materials"].size() == 2 && J["Materials"][0].empty() && J["Smooth"] == 30, "materials, per-face material numbers and the smoothing angle are written when used; a default material is {}" );
		const FJson WallJson = J["Materials"][1];
		C.Check( WallJson["Name"] == "Wall" && WallJson["Color"] == "#FF8000" && WallJson["Texture"] == "Textures/Brick.png" && WallJson["Tile"] == 2.5 && WallJson["Offset"][0] == 0.25 && WallJson["Offset"][1] == 0.5 && WallJson["TwoSided"] == true, "a material writes its fields" );

		// 既定と 1 項目だけ違う材質も、その項目が書かれる
		const auto Written = [&]( const auto& Change, const char* Key )
		{
			FEditMeshMaterial Item;
			Change( Item );
			FEditMesh Single = NEditMesh::MakeBox( FVector3::One );
			Single.Materials.Add( Item );
			const NlohmannJson Out = Single.ToJson();
			const FJson OutJson( Out );
			return OutJson["Materials"][0].contains( Key );
		};
		C.Check( Written( []( FEditMeshMaterial& I ) { I.Color.r = 1; }, "Color" ), "a colour that differs only in red is written" );
		C.Check( Written( []( FEditMeshMaterial& I ) { I.Color.g = 1; }, "Color" ), "a colour that differs only in green is written" );
		C.Check( Written( []( FEditMeshMaterial& I ) { I.Color.b = 1; }, "Color" ), "a colour that differs only in blue is written" );
		C.Check( Written( []( FEditMeshMaterial& I ) { I.Offset.x = 1.0f; }, "Offset" ) && Written( []( FEditMeshMaterial& I ) { I.Offset.y = 1.0f; }, "Offset" ), "an offset in x only, or in y only, is written" );
		C.Check( !Written( []( FEditMeshMaterial& ) {}, "Color" ) && !Written( []( FEditMeshMaterial& ) {}, "Offset" ) && !Written( []( FEditMeshMaterial& ) {}, "Tile" ) && !Written( []( FEditMeshMaterial& ) {}, "TwoSided" ), "a default material writes none of them" );
		FEditMesh Lopsided = NEditMesh::MakeBox( FVector3::One );
		Lopsided.Materials.Add( FEditMeshMaterial() );
		Lopsided.Materials.Add( FEditMeshMaterial() );
		Lopsided.Faces[0].Material = 1;   // 最初の面だけ（最後の面は 0）
		const NlohmannJson LopsidedJson = Lopsided.ToJson();
		const FJson LopsidedView( LopsidedJson );
		C.Check( LopsidedView.contains( "FaceMaterials" ) && LopsidedView["FaceMaterials"][0] == 1 && LopsidedView["FaceMaterials"][5] == 0, "the face material numbers are written when only the first face has one" );
		FEditMesh Translucent;
		C.Check( FEditMesh::FromJson( ParseJsonText( "{\"Materials\":[{\"Color\":\"#FF000080\"}]}" ), Translucent ) && Translucent.Materials.Num() == 1 && Translucent.Materials[0].Color.r == 255 && Translucent.Materials[0].Color.a == 255, "the alpha of a colour is dropped (a material colour is opaque)" );

		TArray<FString> Warnings;
		FEditMesh Back;
		C.Check( FEditMesh::FromJson( Json, Back, &Warnings ) && Warnings.IsEmpty() && Back.GetNumVertices() == 8 && Back.GetNumFaces() == 6 && Back.Faces[2].Material == 1 && Back.Materials.Num() == 2 && Back.Materials[1].Texture == "Textures/Brick.png" && Back.Materials[1].bTwoSided
					 && Near( Back.Materials[1].Tile, 2.5f ) && Near( Back.Materials[1].Offset.y, 0.5f ) && Back.Materials[1].Color.g == 128 && Near( Back.SmoothAngle, 30.0f ),
				 "it reads back without warnings, field by field (%d)", Warnings.Num() );
		C.Check( Back.ToJson() == Json && Back.ComputeHash() == Mesh.ComputeHash(), "write -> read -> write is identical, and the hash is the same" );
		FEditMesh Plain2;
		C.Check( FEditMesh::FromJson( Plain, Plain2, &Warnings ) && Plain2.Materials.IsEmpty() && Plain2.Faces[0].Material == 0 && Plain2.SmoothAngle == 0.0f, "missing optional items take their defaults" );

		// 壊れた入力
		FEditMesh Untouched = NEditMesh::MakeBox( FVector3::One );
		const uint64 UntouchedHash = Untouched.ComputeHash();
		C.Check( !FEditMesh::FromJson( ParseJsonText( "[1, 2]" ), Untouched ) && !FEditMesh::FromJson( ParseJsonText( "5" ), Untouched ) && Untouched.ComputeHash() == UntouchedHash, "a JSON that is not an object is refused and leaves the mesh alone" );
		Warnings.Reset();
		FEditMesh Odd;
		C.Check( FEditMesh::FromJson( ParseJsonText( "{\"Vertices\":[[0,0,0],[1,0],\"x\",[1,2,3]],\"Faces\":[[0,1,3],\"y\",[0,1.5,2],[0,3,2]],\"FaceMaterials\":[0,2],\"Materials\":[5,{\"Color\":\"red\",\"Offset\":[1],\"Tile\":4}]}" ), Odd, &Warnings), "a mesh with broken pieces still loads" );
		C.Check( Odd.GetNumVertices() == 4 && Odd.Vertices[1].Equals( FVector3::Zero ) && Odd.Vertices[3].Equals( FVector3( 1, 2, 3 ) ) && Odd.GetNumFaces() == 4 && Odd.Faces[0].Indices.Num() == 3 && Odd.Faces[1].Indices.IsEmpty() && Odd.Faces[0].Material == 0 && Odd.Faces[1].Material == 2 && Odd.Faces[3].Material == 0
					 && Odd.Materials.Num() == 2 && Near( Odd.Materials[1].Tile, 4.0f ) && Odd.Materials[1].Color.r == 200,
				 "broken vertices stay as the origin and broken faces stay empty (so the numbers do not shift); a bad colour is the default; good fields are kept" );
		bool bVertexWarning = false, bFaceWarning = false, bMaterialWarning = false, bColorWarning = false, bCountWarning = false;
		for ( const FString& Warning : Warnings )
		{
			bVertexWarning = bVertexWarning || Warning.Contains( "2 vertex" );
			bFaceWarning = bFaceWarning || Warning.Contains( "2 face" );
			bMaterialWarning = bMaterialWarning || Warning.Contains( "material 0 is not an object" );
			bColorWarning = bColorWarning || Warning.Contains( "bad color" );
			bCountWarning = bCountWarning || Warning.Contains( "FaceMaterials" );
		}
		C.Check( bVertexWarning && bFaceWarning && bMaterialWarning && bColorWarning && bCountWarning, "each problem is reported (%d warnings)", Warnings.Num() );
		FEditMesh Empty;
		Warnings.Reset();
		C.Check( FEditMesh::FromJson( ParseJsonText( "{}" ), Empty, &Warnings ) && Empty.IsEmpty() && Warnings.IsEmpty() && FEditMesh::FromJson( ParseJsonText( "{\"Vertices\":5,\"Faces\":{}}" ), Empty, &Warnings ) && Warnings.Num() == 2, "{} is an empty mesh; wrong types are warned about" );

		// ワールドのオブジェクトとして
		FWorldDefinition World;
		FWorldObject Object;
		Object.Id = 1;
		Object.Name = "Floor";
		Object.Mesh = Mesh;
		World.Objects.Add( Object );
		World.NextId = 2;
		const NlohmannJson WorldJson = World.ToJson();
		const FJson WJ( WorldJson );
		C.Check( WJ["Objects"][0]["Components"].contains( "Mesh" ) && WJ["Objects"][0]["Components"]["Mesh"]["Vertices"].size() == 8, "the Mesh component is written under Components" );
		FWorldDefinition Loaded;
		TArray<FString> LoadWarnings;
		C.Check( FWorldDefinition::FromJson( ParseJsonText( WorldJson.dump().c_str() ), Loaded, &LoadWarnings ) && LoadWarnings.IsEmpty() && Loaded.Objects.Num() == 1 && Loaded.Objects[0].Mesh.IsSet() && Loaded.Objects[0].Mesh.GetValue().ComputeHash() == Mesh.ComputeHash()
					 && Loaded.Objects[0].RawComponents.IsEmpty() && Loaded.ToJson() == WorldJson && Loaded.Objects[0].GetComponentNames().Contains( "Mesh" ),
				 "the world round-trips with the mesh (not as an unknown component) (%d warnings)", LoadWarnings.Num() );
		LoadWarnings.Reset();
		FWorldDefinition Wrong;
		const bool bLoadedWrong = FWorldDefinition::FromJson( ParseJsonText( "{\"Objects\":[{\"Id\":1,\"Components\":{\"Mesh\":5}}]}" ), Wrong, &LoadWarnings );
		const NlohmannJson WrongJson = Wrong.ToJson();
		C.Check( bLoadedWrong && Wrong.Objects.Num() == 1 && !Wrong.Objects[0].Mesh.IsSet() && Wrong.Objects[0].RawComponents.Num() == 1 && FJson( WrongJson )["Objects"][0]["Components"]["Mesh"] == 5 && LoadWarnings.Num() == 1,
				 "a Mesh component that is not an object is kept as it is, with a warning" );
		LoadWarnings.Reset();
		FWorldDefinition Warned;
		FWorldDefinition::FromJson( ParseJsonText( "{\"Objects\":[{\"Id\":3,\"Name\":\"A\",\"Components\":{\"Mesh\":{\"Vertices\":[[0,0]]}}}]}" ), Warned, &LoadWarnings );
		C.Check( LoadWarnings.Num() == 1 && LoadWarnings[0].Contains( "Mesh" ) && LoadWarnings[0].Contains( "'A'" ), "mesh warnings carry the object they came from (%d)", LoadWarnings.Num() );
	}

	//=========================================================================
	// 検証
	//=========================================================================
	void TestValidate( FChecker& C )
	{
		C.Group = "mesh-validate";
		C.Check( NEditMesh::MakeBox( FVector3::One ).Validate().IsEmpty() && NEditMesh::MakeSphere( 1.0f ).Validate().IsEmpty(), "primitives have no issues" );

		const auto Issues = [&]( const auto& Change ) { FEditMesh M = NEditMesh::MakeBox( FVector3::One ); Change( M ); return M.Validate(); };
		TArray<FEditMeshIssue> R = Issues( []( FEditMesh& M ) { M.Faces[1].Indices[2] = 40; } );
		C.Check( CountIssues( R, true, "vertex that does not exist" ) == 1 && R[0].Face == 1, "a vertex number out of range is an error on that face" );
		R = Issues( []( FEditMesh& M ) { M.Faces[2].Indices.SetNum( 2 ); } );
		C.Check( CountIssues( R, true, "at least 3" ) == 1, "a face with 2 vertices is an error" );
		R = Issues( []( FEditMesh& M ) { M.Faces[0].Indices[1] = M.Faces[0].Indices[0]; } );
		C.Check( CountIssues( R, false, "same vertex twice" ) == 1 && CountIssues( R, true ) == 0, "using the same vertex twice is a warning (the face is just not drawn)" );
		R = Issues( []( FEditMesh& M ) { for ( int32 Index : M.Faces[0].Indices ) M.Vertices[Index] = FVector3::Zero; } );
		C.Check( CountIssues( R, false, "no area" ) >= 1 && CountIssues( R, true ) == 0, "a face with no area is a warning" );
		R = Issues( []( FEditMesh& M ) { M.Vertices.Add( FVector3::One ); M.Vertices.Add( FVector3::One ); } );
		C.Check( CountIssues( R, false, "2 vertex(es) are not used" ) == 1, "vertices no face uses are one warning, with the count" );
		R = Issues( []( FEditMesh& M ) { M.Vertices.Add( FVector3::One ); M.Faces[1].Indices[2] = 40; } );
		C.Check( CountIssues( R, false, "not used" ) == 0 && CountIssues( R, true, "does not exist" ) == 1, "with a broken face, unused vertices are not reported (they may belong to it)" );
		R = Issues( []( FEditMesh& M ) { M.Vertices[3].y = std::numeric_limits<float32>::quiet_NaN(); } );
		C.Check( CountIssues( R, true, "not finite" ) == 1, "a vertex that is not a number is an error" );
		R = Issues( []( FEditMesh& M ) { M.Faces[0].Material = -2; } );
		C.Check( CountIssues( R, true, "negative material" ) == 1, "a negative material number is an error" );
		R = Issues( []( FEditMesh& M ) { M.Faces[0].Material = -1; } );
		C.Check( CountIssues( R, true, "negative material" ) == 1, "-1 is a negative material number too" );
		R = Issues( []( FEditMesh& M ) { M.Faces[0].Material = 3; } );
		C.Check( CountIssues( R, false, "does not exist" ) == 1 && CountIssues( R, true ) == 0, "a material number with no material is a warning (the default is used)" );
		R = Issues( []( FEditMesh& M ) { M.Materials.Add( FEditMeshMaterial() ); M.Faces[0].Material = 1; } );
		C.Check( CountIssues( R, false, "does not exist" ) == 1, "...also when there are materials, but not that many" );
		R = Issues( []( FEditMesh& M ) { FEditMeshMaterial Bad; Bad.Tile = 0.0f; M.Materials.Add( Bad ); } );
		C.Check( CountIssues( R, true, "tile length" ) == 1, "a tile length of 0 is an error" );
		R = Issues( []( FEditMesh& M ) { M.SmoothAngle = 200.0f; } );
		C.Check( CountIssues( R, true, "smoothing angle" ) == 1, "a smoothing angle over 180 is an error" );
		R = Issues( []( FEditMesh& M ) { M.Faces.Reset(); } );
		C.Check( CountIssues( R, false, "no faces" ) == 1 && CountIssues( R, true ) == 0, "no faces is a warning" );
		R = Issues( []( FEditMesh& M ) { M.Vertices[0] = FVector3( 0, 0, 0 ); M.Vertices[1] = FVector3( 1, 0, 0 ); M.Vertices[2] = FVector3( 0, 1, 0 ); M.Vertices[3] = FVector3( 1, 1, 1 ); M.Faces[0] = MakeFace( { 0, 1, 2, 3 } ); } );
		C.Check( CountIssues( R, false, "not convex" ) + CountIssues( R, false, "twisted" ) >= 1, "a twisted quad is a warning" );
		FEditMesh Bowtie;
		AddVertex( Bowtie, 0, 0, 0 );
		AddVertex( Bowtie, 1, 0, 1 );
		AddVertex( Bowtie, 1, 0, 0 );
		AddVertex( Bowtie, 0, 0, 1 );
		Bowtie.Faces.Add( MakeFace( { 0, 1, 2, 3 } ) );
		C.Check( CountIssues( Bowtie.Validate(), false, "convex" ) + CountIssues( Bowtie.Validate(), false, "twisted" ) + CountIssues( Bowtie.Validate(), false, "no area" ) >= 1, "a bow-tie face is noticed" );
		// C の字の面（上から見て時計回り）。先頭の頂点（下の腕の先）から扇形に分けると、隙間に重なる三角形ができる
		FEditMesh CShape;
		AddVertex( CShape, 4, 0, 0 );
		AddVertex( CShape, 0, 0, 0 );
		AddVertex( CShape, 0, 0, 4 );
		AddVertex( CShape, 4, 0, 4 );
		AddVertex( CShape, 4, 0, 3 );
		AddVertex( CShape, 1, 0, 3 );
		AddVertex( CShape, 1, 0, 1 );
		AddVertex( CShape, 4, 0, 1 );
		CShape.Faces.Add( MakeFace( { 0, 1, 2, 3, 4, 5, 6, 7 } ) );
		C.Check( Near( CShape.ComputeFaceArea( 0 ), 10.0f ) && CountIssues( CShape.Validate(), false, "convex" ) == 1 && CountIssues( CShape.Validate(), true ) == 0, "a concave face (a C shape) is a warning: the fan would cover the gap" );
		FEditMesh Huge;
		Huge.Vertices.SetNum( FEditMesh::MaxVertices + 1 );
		C.Check( CountIssues( Huge.Validate(), true, "vertices (the limit" ) == 1, "more vertices than the limit is an error" );
		FEditMesh Exact;
		Exact.Vertices.SetNum( FEditMesh::MaxVertices );
		C.Check( CountIssues( Exact.Validate(), true, "vertices (the limit" ) == 0, "exactly the limit is allowed" );

		// ワールドの検証として
		FFakeQuery Assets;
		Assets.Existing.insert( "Textures/Brick.png" );
		const auto Validate = [&]( const FWorldObject& Object )
		{
			FWorldDefinition World;
			World.Objects.Add( Object );
			World.NextId = Object.Id + 1;
			return NWorld::ValidateWorld( World, &Assets );
		};
		FWorldObject Floor;
		Floor.Id = 1;
		Floor.Name = "Floor";
		Floor.Mesh = NEditMesh::MakeBox( FVector3::One );
		C.Check( Validate( Floor ).IsEmpty(), "a world with a good mesh has no issues" );
		FWorldObject Bad = Floor;
		Bad.Mesh->Faces[0].Indices[0] = 99;
		TArray<FWorldIssue> WorldIssues = Validate( Bad );
		bool bBadMesh = false;
		for ( const FWorldIssue& Issue : WorldIssues ) bBadMesh = bBadMesh || ( Issue.Code == EWorldIssueCode::BadMesh && Issue.Severity == EWorldIssueSeverity::Error && Issue.ObjectId == 1 );
		C.Check( bBadMesh && NWorld::HasErrors( WorldIssues ) && FString( ToString( EWorldIssueCode::BadMesh ) ) == "BadMesh", "a mesh error is a BadMesh error on the object" );
		FWorldObject Warned = Floor;
		Warned.Mesh->Vertices.Add( FVector3::One );
		WorldIssues = Validate( Warned );
		C.Check( WorldIssues.Num() == 1 && WorldIssues[0].Severity == EWorldIssueSeverity::Warning && WorldIssues[0].Code == EWorldIssueCode::BadMesh && !NWorld::HasErrors( WorldIssues ), "and a mesh warning is a warning" );
		FWorldObject Textured = Floor;
		FEditMeshMaterial Brick;
		Brick.Texture = "Textures/Brick.png";
		FEditMeshMaterial Lost;
		Lost.Texture = "Textures/Nope.png";
		Textured.Mesh->Materials.Add( Brick );
		Textured.Mesh->Materials.Add( Lost );
		Textured.Mesh->Materials.Add( FEditMeshMaterial() );
		WorldIssues = Validate( Textured );
		C.Check( WorldIssues.Num() == 1 && WorldIssues[0].Code == EWorldIssueCode::MissingAsset && WorldIssues[0].Message.Contains( "Nope.png" ), "a material image that is not there is a missing asset (an empty one is fine)" );

		// メッシュの当たり判定の三角形の元: Collider の Mesh のファイル > Mesh 部品 > Model のファイル
		FWorldObject Solid = Floor;
		FWorldCollider Collider;
		Collider.Shape = EWorldColliderShape::Mesh;
		Solid.Collider = Collider;
		C.Check( NWorld::UsesEditMeshCollider( Solid ) && NWorld::GetColliderMeshPath( Solid ).IsEmpty() && Validate( Solid ).IsEmpty(), "a mesh collider with a Mesh component uses it (no file, no issue)" );
		FWorldModel Model;
		Model.Path = "Textures/Brick.png";
		Solid.Model = Model;
		C.Check( NWorld::UsesEditMeshCollider( Solid ) && NWorld::GetColliderMeshPath( Solid ).IsEmpty(), "the Mesh component comes before the Model" );
		Solid.Collider->Mesh = "Textures/Brick.png";
		C.Check( !NWorld::UsesEditMeshCollider( Solid ) && NWorld::GetColliderMeshPath( Solid ) == "Textures/Brick.png", "and the collider's own file comes before both" );
		FWorldObject NoMesh = Solid;
		NoMesh.Mesh.Reset();
		NoMesh.Collider->Mesh = FString();
		C.Check( !NWorld::UsesEditMeshCollider( NoMesh ) && NWorld::GetColliderMeshPath( NoMesh ) == "Textures/Brick.png", "without the Mesh component, the Model's file is used" );
		FWorldObject Hollow = Floor;
		Hollow.Mesh->Faces.Reset();
		Hollow.Collider = Collider;
		WorldIssues = Validate( Hollow );
		int32 NumHollowWarnings = 0;
		for ( const FWorldIssue& Issue : WorldIssues ) NumHollowWarnings += ( Issue.Code == EWorldIssueCode::BadCollider && Issue.Severity == EWorldIssueSeverity::Warning ) ? 1 : 0;
		C.Check( NumHollowWarnings == 1 && !NWorld::HasErrors( WorldIssues ), "a mesh collider over a Mesh component with no faces is a warning" );
		FWorldObject Empty;
		Empty.Id = 1;
		Empty.Collider = Collider;
		C.Check( NWorld::HasErrors( Validate( Empty ) ) && !NWorld::UsesEditMeshCollider( Empty ), "a mesh collider with nothing to take triangles from is still an error" );
		// 形が Box のコライダーは、Mesh 部品があっても、その三角形を使わない
		FWorldObject BoxCollider = Floor;
		FWorldCollider BoxShape;
		BoxShape.Shape = EWorldColliderShape::Box;
		BoxCollider.Collider = BoxShape;
		C.Check( !NWorld::UsesEditMeshCollider( BoxCollider ) && NWorld::GetColliderMeshPath( BoxCollider ).IsEmpty() && Validate( BoxCollider ).IsEmpty(), "a box collider on an object with a Mesh component does not use the mesh's triangles" );
	}
}

void NWorldTest::RunMeshDataTests( FChecker& C )
{
	TestPrimitives( C );
	TestData( C );
	TestBuild( C );
	TestJson( C );
	TestValidate( C );
}
