#include "ImportedScene.h"

#include <algorithm>
#include <cmath>

#include "Library/Core/Collection/Set.h"

bool FImportMatrix::NearlyEquals( const FImportMatrix& Other, float32 Tolerance ) const
{
	for ( int32 i = 0; i < 16; ++i )
	{
		if ( std::fabs( M[i] - Other.M[i] ) > Tolerance ) return false;
	}
	return true;
}

FImportMatrix NImportedScene::MirrorZ( const FImportMatrix& Matrix )
{
	// M'[i][j] = s_i * M[i][j] * s_j（s = 1, 1, -1, 1）。Z の行と列がちょうど片方だけ交わる要素の符号が変わる
	static constexpr float32 Sign[4] = { 1.0f, 1.0f, -1.0f, 1.0f };
	FImportMatrix Result;
	for ( int32 Row = 0; Row < 4; ++Row )
	{
		for ( int32 Column = 0; Column < 4; ++Column ) Result.M[Row * 4 + Column] = Sign[Row] * Matrix.M[Row * 4 + Column] * Sign[Column];
	}
	return Result;
}

void NImportedScene::ConvertToLeftHanded( FImportedScene& Scene )
{
	for ( FImportedNode& Node : Scene.Nodes ) Node.Local = MirrorZ( Node.Local );

	for ( FImportedMesh& Mesh : Scene.Meshes )
	{
		for ( FVector3& Position : Mesh.Positions ) Position.z = -Position.z;
		for ( FVector3& Normal : Mesh.Normals ) Normal.z = -Normal.z;
		// 鏡に映すと、三角形の表裏が逆になる。順番を入れ替えて、表のままにする
		for ( int32 Index = 0; Index + 2 < Mesh.Indices.Num(); Index += 3 ) std::swap( Mesh.Indices[Index + 1], Mesh.Indices[Index + 2] );
		for ( FImportedCluster& Cluster : Mesh.Clusters ) Cluster.Offset = MirrorZ( Cluster.Offset );
	}

	for ( FImportedAnimation& Animation : Scene.Animations )
	{
		for ( FImportedTrack& Track : Animation.Tracks )
		{
			for ( FImportMatrix& Key : Track.Keys ) Key = MirrorZ( Key );
		}
	}
}

void NImportedScene::LimitInfluences( TArray<FImportInfluence>& Influences, int32 Max )
{
	Influences.RemoveAll( []( const FImportInfluence& Influence ) { return !( Influence.Weight > 0.0f ); } );
	std::stable_sort( Influences.begin(), Influences.end(), []( const FImportInfluence& A, const FImportInfluence& B ) { return A.Weight > B.Weight; } );
	if ( Influences.Num() > Max ) Influences.SetNum( Max );

	float64 Sum = 0.0;
	for ( const FImportInfluence& Influence : Influences ) Sum += Influence.Weight;
	if ( Sum <= 0.0 ) return;
	for ( FImportInfluence& Influence : Influences ) Influence.Weight = static_cast<float32>( Influence.Weight / Sum );
}

FString NImportedScene::SanitizeName( const FString& Name, const FString& Fallback )
{
	FString Result;
	Result.Reserve( Name.Len() );
	for ( int32 Index = 0; Index < Name.Len(); ++Index )
	{
		const unsigned char Ch = static_cast<unsigned char>( Name[Index] );
		// 空白・X ファイルの区切りや注釈に使われる文字・制御文字は '_'。それ以外（':' '.' '|' や日本語を含む）はそのまま
		const bool bUnsafe = Ch <= 0x20 || Ch == 0x7F || Ch == '{' || Ch == '}' || Ch == ';' || Ch == ',' || Ch == '"' || Ch == '<' || Ch == '>' || Ch == '#' || Ch == '/';
		Result += bUnsafe ? '_' : static_cast<char>( Ch );
	}
	return Result.IsEmpty() ? Fallback : Result;
}

TArray<FString> NImportedScene::MakeFrameNames( const FImportedScene& Scene )
{
	TArray<FString> Names;
	TSet<FString> Used;
	for ( int32 Index = 0; Index < Scene.Nodes.Num(); ++Index )
	{
		const FString Base = SanitizeName( Scene.Nodes[Index].Name, FString::Printf( "Node%d", Index ) );
		FString Candidate = Base;
		for ( int32 Suffix = 2; Used.Contains( Candidate ); ++Suffix ) Candidate = FString::Printf( "%s_%d", *Base, Suffix );
		Used.Add( Candidate );
		Names.Add( Candidate );
	}
	return Names;
}

TArray<FString> NImportedScene::Validate( const FImportedScene& Scene )
{
	TArray<FString> Problems;
	const int32 NumNodes = Scene.Nodes.Num();
	for ( int32 Index = 0; Index < NumNodes; ++Index )
	{
		const int32 Parent = Scene.Nodes[Index].Parent;
		if ( Parent < -1 || Parent >= NumNodes || Parent == Index ) Problems.Add( FString::Printf( "node %d has the invalid parent %d", Index, Parent ) );
	}
	for ( int32 MeshIndex = 0; MeshIndex < Scene.Meshes.Num(); ++MeshIndex )
	{
		const FImportedMesh& Mesh = Scene.Meshes[MeshIndex];
		const int32 NumVertices = Mesh.Positions.Num();
		if ( Mesh.Node < 0 || Mesh.Node >= NumNodes ) Problems.Add( FString::Printf( "mesh %d has the invalid node %d", MeshIndex, Mesh.Node ) );
		if ( Mesh.Normals.Num() != NumVertices ) Problems.Add( FString::Printf( "mesh %d has %d normal(s) for %d vertices", MeshIndex, Mesh.Normals.Num(), NumVertices ) );
		if ( !Mesh.UVs.IsEmpty() && Mesh.UVs.Num() != NumVertices ) Problems.Add( FString::Printf( "mesh %d has %d UV(s) for %d vertices", MeshIndex, Mesh.UVs.Num(), NumVertices ) );
		if ( Mesh.Indices.Num() % 3 != 0 ) Problems.Add( FString::Printf( "mesh %d has %d indices (not a multiple of 3)", MeshIndex, Mesh.Indices.Num() ) );
		if ( Mesh.TriangleMaterials.Num() != Mesh.GetNumTriangles() ) Problems.Add( FString::Printf( "mesh %d has %d material(s) for %d triangles", MeshIndex, Mesh.TriangleMaterials.Num(), Mesh.GetNumTriangles() ) );
		for ( const int32 Index : Mesh.Indices )
		{
			if ( Index < 0 || Index >= NumVertices )
			{
				Problems.Add( FString::Printf( "mesh %d has the vertex index %d out of range (%d vertices)", MeshIndex, Index, NumVertices ) );
				break;
			}
		}
		for ( const int32 Material : Mesh.TriangleMaterials )
		{
			if ( Material < -1 || Material >= Scene.Materials.Num() )
			{
				Problems.Add( FString::Printf( "mesh %d refers to the material %d (there are %d)", MeshIndex, Material, Scene.Materials.Num() ) );
				break;
			}
		}
		for ( const FImportedCluster& Cluster : Mesh.Clusters )
		{
			if ( Cluster.BoneNode < 0 || Cluster.BoneNode >= NumNodes ) Problems.Add( FString::Printf( "mesh %d has a cluster with the invalid bone %d", MeshIndex, Cluster.BoneNode ) );
			if ( Cluster.Vertices.Num() != Cluster.Weights.Num() ) Problems.Add( FString::Printf( "mesh %d has a cluster with %d vertices and %d weights", MeshIndex, Cluster.Vertices.Num(), Cluster.Weights.Num() ) );
			for ( const int32 Vertex : Cluster.Vertices )
			{
				if ( Vertex < 0 || Vertex >= NumVertices )
				{
					Problems.Add( FString::Printf( "mesh %d has a cluster vertex %d out of range", MeshIndex, Vertex ) );
					break;
				}
			}
		}
	}
	for ( int32 Index = 0; Index < Scene.Materials.Num(); ++Index )
	{
		const int32 Texture = Scene.Materials[Index].Texture;
		if ( Texture < -1 || Texture >= Scene.Textures.Num() ) Problems.Add( FString::Printf( "material %d refers to the texture %d (there are %d)", Index, Texture, Scene.Textures.Num() ) );
	}
	for ( const FImportedAnimation& Animation : Scene.Animations )
	{
		if ( Animation.NumKeys <= 0 || !( Animation.Fps > 0.0f ) ) Problems.Add( FString::Printf( "animation '%s' has %d key(s) at %.2f fps", *Animation.Name, Animation.NumKeys, Animation.Fps ) );
		for ( const FImportedTrack& Track : Animation.Tracks )
		{
			if ( Track.Node < 0 || Track.Node >= NumNodes ) Problems.Add( FString::Printf( "animation '%s' has a track for the invalid node %d", *Animation.Name, Track.Node ) );
			if ( Track.Keys.Num() != Animation.NumKeys ) Problems.Add( FString::Printf( "animation '%s' has a track with %d key(s) (expected %d)", *Animation.Name, Track.Keys.Num(), Animation.NumKeys ) );
		}
	}
	return Problems;
}
