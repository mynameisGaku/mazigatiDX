#include "XFileWriter.h"

#include <algorithm>
#include <cstdio>
#include <string>

#include "Library/Core/Collection/Set.h"

namespace
{
	// 文字列を溜めていく入れ物（printf で数を書く）
	class FTextBuilder
	{
	public:
		void Line( const char* Text, int32 Indent )
		{
			Indentation( Indent );
			Out += Text;
			Out += "\r\n";
		}
		void Indentation( int32 Indent ) { Out.append( static_cast<size_t>( Indent ) * 2, ' ' ); }
		void Raw( const char* Text ) { Out += Text; }
		void Raw( const std::string& Text ) { Out += Text; }
		void Newline() { Out += "\r\n"; }

		void Float( float32 Value )
		{
			char Buffer[40];
			std::snprintf( Buffer, sizeof( Buffer ), "%.8g", static_cast<double>( Value ) );
			Out += Buffer;
		}
		void Int( int32 Value )
		{
			char Buffer[16];
			std::snprintf( Buffer, sizeof( Buffer ), "%d", Value );
			Out += Buffer;
		}

		// 16 個の数（カンマ区切り）。行列 1 つ分
		void Matrix( const FImportMatrix& Matrix )
		{
			for ( int32 i = 0; i < 16; ++i )
			{
				if ( i > 0 ) Out += ',';
				Float( Matrix.M[i] );
			}
		}

		std::string Out;
	};

	void WriteFrameTransform( FTextBuilder& T, const FImportMatrix& Matrix, int32 Indent )
	{
		T.Indentation( Indent );
		T.Raw( "FrameTransformMatrix { " );
		T.Matrix( Matrix );
		T.Raw( ";; }" );
		T.Newline();
	}

	void WriteMaterial( FTextBuilder& T, const FImportedMaterial* Material, const FString& TexturePath, int32 Indent )
	{
		static const FImportedMaterial Default;
		const FImportedMaterial& M = Material != nullptr ? *Material : Default;
		T.Line( "Material {", Indent );
		T.Indentation( Indent + 1 );
		for ( int32 i = 0; i < 4; ++i )
		{
			T.Float( M.Diffuse[i] );
			T.Raw( ";" );
		}
		T.Raw( ";" );
		T.Newline();
		T.Indentation( Indent + 1 );
		T.Float( M.Power );
		T.Raw( ";" );
		T.Newline();
		T.Indentation( Indent + 1 );
		for ( int32 i = 0; i < 3; ++i )
		{
			T.Float( M.Specular[i] );
			T.Raw( ";" );
		}
		T.Raw( ";" );
		T.Newline();
		T.Indentation( Indent + 1 );
		for ( int32 i = 0; i < 3; ++i )
		{
			T.Float( M.Emissive[i] );
			T.Raw( ";" );
		}
		T.Raw( ";" );
		T.Newline();
		if ( !TexturePath.IsEmpty() )
		{
			T.Indentation( Indent + 1 );
			T.Raw( "TextureFilename { \"" );
			T.Raw( *TexturePath );
			T.Raw( "\"; }" );
			T.Newline();
		}
		T.Line( "}", Indent );
	}

	void WriteMesh( FTextBuilder& T, const FImportedScene& Scene, const FImportedMesh& Mesh, const TArray<FString>& FrameNames, const TArray<FString>& TexturePaths, int32 Indent )
	{
		const int32 NumVertices = Mesh.Positions.Num();
		const int32 NumTriangles = Mesh.GetNumTriangles();

		T.Line( "Mesh {", Indent );
		const int32 Inner = Indent + 1;

		// 頂点
		T.Indentation( Inner );
		T.Int( NumVertices );
		T.Raw( ";" );
		T.Newline();
		for ( int32 i = 0; i < NumVertices; ++i )
		{
			T.Indentation( Inner + 1 );
			T.Float( Mesh.Positions[i].x );
			T.Raw( ";" );
			T.Float( Mesh.Positions[i].y );
			T.Raw( ";" );
			T.Float( Mesh.Positions[i].z );
			T.Raw( i + 1 < NumVertices ? ";," : ";;" );
			T.Newline();
		}

		// 面（三角形）
		T.Indentation( Inner );
		T.Int( NumTriangles );
		T.Raw( ";" );
		T.Newline();
		const auto WriteFaces = [&]( int32 FaceIndent )
		{
			for ( int32 i = 0; i < NumTriangles; ++i )
			{
				T.Indentation( FaceIndent );
				T.Raw( "3;" );
				T.Int( Mesh.Indices[i * 3] );
				T.Raw( "," );
				T.Int( Mesh.Indices[i * 3 + 1] );
				T.Raw( "," );
				T.Int( Mesh.Indices[i * 3 + 2] );
				T.Raw( i + 1 < NumTriangles ? ";," : ";;" );
				T.Newline();
			}
		};
		WriteFaces( Inner + 1 );

		// 法線（頂点ごとで、面の頂点の番号はメッシュの面と同じ）
		T.Line( "MeshNormals {", Inner );
		T.Indentation( Inner + 1 );
		T.Int( NumVertices );
		T.Raw( ";" );
		T.Newline();
		for ( int32 i = 0; i < NumVertices; ++i )
		{
			T.Indentation( Inner + 2 );
			T.Float( Mesh.Normals[i].x );
			T.Raw( ";" );
			T.Float( Mesh.Normals[i].y );
			T.Raw( ";" );
			T.Float( Mesh.Normals[i].z );
			T.Raw( i + 1 < NumVertices ? ";," : ";;" );
			T.Newline();
		}
		T.Indentation( Inner + 1 );
		T.Int( NumTriangles );
		T.Raw( ";" );
		T.Newline();
		WriteFaces( Inner + 2 );
		T.Line( "}", Inner );

		// UV
		if ( !Mesh.UVs.IsEmpty() )
		{
			T.Line( "MeshTextureCoords {", Inner );
			T.Indentation( Inner + 1 );
			T.Int( NumVertices );
			T.Raw( ";" );
			T.Newline();
			for ( int32 i = 0; i < NumVertices; ++i )
			{
				T.Indentation( Inner + 2 );
				T.Float( Mesh.UVs[i].x );
				T.Raw( ";" );
				T.Float( Mesh.UVs[i].y );
				T.Raw( i + 1 < NumVertices ? ";," : ";;" );
				T.Newline();
			}
			T.Line( "}", Inner );
		}

		// マテリアル: このメッシュで使うものだけを、使われた順に並べ直す
		TArray<int32> Used;
		for ( const int32 Material : Mesh.TriangleMaterials ) Used.AddUnique( Material );
		if ( Used.IsEmpty() ) Used.Add( -1 );
		T.Line( "MeshMaterialList {", Inner );
		T.Indentation( Inner + 1 );
		T.Int( Used.Num() );
		T.Raw( ";" );
		T.Newline();
		T.Indentation( Inner + 1 );
		T.Int( NumTriangles );
		T.Raw( ";" );
		T.Newline();
		T.Indentation( Inner + 2 );
		for ( int32 i = 0; i < NumTriangles; ++i )
		{
			if ( i > 0 ) T.Raw( "," );
			T.Int( Used.Find( Mesh.TriangleMaterials.IsValidIndex( i ) ? Mesh.TriangleMaterials[i] : -1 ) );
		}
		T.Raw( ";" );
		T.Newline();
		for ( const int32 Material : Used )
		{
			const FImportedMaterial* Source = Material >= 0 ? &Scene.Materials[Material] : nullptr;
			FString TexturePath;
			if ( Source != nullptr && Source->Texture >= 0 && Scene.Textures.IsValidIndex( Source->Texture ) )
			{
				TexturePath = TexturePaths.IsValidIndex( Source->Texture ) ? TexturePaths[Source->Texture] : Scene.Textures[Source->Texture].File;
			}
			WriteMaterial( T, Source, TexturePath, Inner + 1 );
		}
		T.Line( "}", Inner );

		// スキン
		if ( Mesh.IsSkinned() )
		{
			TArray<int32> PerVertex;
			PerVertex.SetNum( NumVertices, 0 );
			int32 NumBones = 0;
			for ( const FImportedCluster& Cluster : Mesh.Clusters )
			{
				if ( Cluster.Vertices.IsEmpty() ) continue;
				++NumBones;
				for ( const int32 Vertex : Cluster.Vertices ) ++PerVertex[Vertex];
			}
			int32 MaxPerVertex = 0;
			for ( const int32 Count : PerVertex ) MaxPerVertex = std::max( MaxPerVertex, Count );

			T.Line( "XSkinMeshHeader {", Inner );
			T.Indentation( Inner + 1 );
			T.Int( MaxPerVertex );
			T.Raw( "; " );
			T.Int( MaxPerVertex * 3 );
			T.Raw( "; " );
			T.Int( NumBones );
			T.Raw( ";" );
			T.Newline();
			T.Line( "}", Inner );

			for ( const FImportedCluster& Cluster : Mesh.Clusters )
			{
				if ( Cluster.Vertices.IsEmpty() ) continue;
				T.Line( "SkinWeights {", Inner );
				T.Indentation( Inner + 1 );
				T.Raw( "\"" );
				T.Raw( *FrameNames[Cluster.BoneNode] );
				T.Raw( "\";" );
				T.Newline();
				T.Indentation( Inner + 1 );
				T.Int( Cluster.Vertices.Num() );
				T.Raw( ";" );
				T.Newline();
				T.Indentation( Inner + 2 );
				for ( int32 i = 0; i < Cluster.Vertices.Num(); ++i )
				{
					if ( i > 0 ) T.Raw( "," );
					T.Int( Cluster.Vertices[i] );
				}
				T.Raw( ";" );
				T.Newline();
				T.Indentation( Inner + 2 );
				for ( int32 i = 0; i < Cluster.Weights.Num(); ++i )
				{
					if ( i > 0 ) T.Raw( "," );
					T.Float( Cluster.Weights[i] );
				}
				T.Raw( ";" );
				T.Newline();
				T.Indentation( Inner + 2 );
				T.Matrix( Cluster.Offset );
				T.Raw( ";;" );
				T.Newline();
				T.Line( "}", Inner );
			}
		}
		T.Line( "}", Indent );
	}

	void WriteNode( FTextBuilder& T, const FImportedScene& Scene, int32 NodeIndex, const TArray<FString>& FrameNames, const TArray<FString>& TexturePaths,
					const TArray<TArray<int32>>& Children, int32 Indent )
	{
		T.Indentation( Indent );
		T.Raw( "Frame " );
		T.Raw( *FrameNames[NodeIndex] );
		T.Raw( " {" );
		T.Newline();
		WriteFrameTransform( T, Scene.Nodes[NodeIndex].Local, Indent + 1 );
		for ( const FImportedMesh& Mesh : Scene.Meshes )
		{
			if ( Mesh.Node == NodeIndex && !Mesh.IsSkinned() ) WriteMesh( T, Scene, Mesh, FrameNames, TexturePaths, Indent + 1 );
		}
		for ( const int32 Child : Children[NodeIndex] ) WriteNode( T, Scene, Child, FrameNames, TexturePaths, Children, Indent + 1 );
		T.Line( "}", Indent );
	}
}

FString NXFile::Write( const FImportedScene& Scene, const TArray<FString>& TexturePaths )
{
	const TArray<FString> FrameNames = NImportedScene::MakeFrameNames( Scene );

	TArray<TArray<int32>> Children;
	Children.SetNum( Scene.Nodes.Num() );
	TArray<int32> Roots;
	for ( int32 Index = 0; Index < Scene.Nodes.Num(); ++Index )
	{
		const int32 Parent = Scene.Nodes[Index].Parent;
		if ( Parent >= 0 && Parent < Scene.Nodes.Num() ) Children[Parent].Add( Index );
		else Roots.Add( Index );
	}

	FTextBuilder T;
	T.Raw( "xof 0303txt 0032\r\n" );
	T.Raw( "Header { 1; 0; 1; }\r\n" );

	for ( const int32 Root : Roots ) WriteNode( T, Scene, Root, FrameNames, TexturePaths, Children, 0 );

	// スキンのあるメッシュ: 単位行列のフレームに入れる（骨の動きだけで動く）
	TSet<FString> UsedNames;
	for ( const FString& Name : FrameNames ) UsedNames.Add( Name );
	int32 SkinIndex = 0;
	for ( const FImportedMesh& Mesh : Scene.Meshes )
	{
		if ( !Mesh.IsSkinned() ) continue;
		FString Name = NImportedScene::SanitizeName( Mesh.Name, "Skin" ) + "_Skin";
		for ( int32 Suffix = 2; UsedNames.Contains( Name ); ++Suffix ) Name = FString::Printf( "%s_Skin%d", *NImportedScene::SanitizeName( Mesh.Name, "Skin" ), Suffix );
		UsedNames.Add( Name );
		++SkinIndex;

		T.Raw( "Frame " );
		T.Raw( *Name );
		T.Raw( " {\r\n" );
		WriteFrameTransform( T, FImportMatrix(), 1 );
		WriteMesh( T, Scene, Mesh, FrameNames, TexturePaths, 1 );
		T.Raw( "}\r\n" );
	}

	// アニメーション
	TSet<FString> UsedAnimNames;
	int32 AnimIndex = 0;
	for ( const FImportedAnimation& Animation : Scene.Animations )
	{
		if ( Animation.Tracks.IsEmpty() || Animation.NumKeys <= 0 ) continue;
		const FString Base = NImportedScene::SanitizeName( Animation.Name, FString::Printf( "Anim%d", AnimIndex ) );
		FString Name = Base;
		for ( int32 Suffix = 2; UsedAnimNames.Contains( Name ); ++Suffix ) Name = FString::Printf( "%s_%d", *Base, Suffix );
		UsedAnimNames.Add( Name );
		++AnimIndex;

		T.Raw( "AnimationSet " );
		T.Raw( *Name );
		T.Raw( " {\r\n" );
		for ( const FImportedTrack& Track : Animation.Tracks )
		{
			T.Line( "Animation {", 1 );
			T.Indentation( 2 );
			T.Raw( "{ " );
			T.Raw( *FrameNames[Track.Node] );
			T.Raw( " }" );
			T.Newline();
			T.Line( "AnimationKey {", 2 );
			T.Line( "4;", 3 );
			T.Indentation( 3 );
			T.Int( Track.Keys.Num() );
			T.Raw( ";" );
			T.Newline();
			for ( int32 Key = 0; Key < Track.Keys.Num(); ++Key )
			{
				T.Indentation( 3 );
				T.Int( Key );
				T.Raw( ";16;" );
				T.Matrix( Track.Keys[Key] );
				T.Raw( Key + 1 < Track.Keys.Num() ? ";;," : ";;;" );
				T.Newline();
			}
			T.Line( "}", 2 );
			T.Line( "}", 1 );
		}
		T.Raw( "}\r\n" );
	}
	(void)SkinIndex;

	return FString( T.Out.c_str(), static_cast<int32>( T.Out.size() ) );
}
