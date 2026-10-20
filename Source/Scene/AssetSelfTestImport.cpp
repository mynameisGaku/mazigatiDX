#include "AssetSelfTestSupport.h"
#include "AssetSelfTestFbx.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <string>

#include "Library/Framework/Animation/ModelAnimator.h"
#include "Library/Framework/Asset/AssetManager.h"
#include "Library/Framework/Asset/DefinitionEditing.h"
#include "Library/Framework/Asset/Import/FbxImporter.h"
#include "Library/Framework/Asset/Import/ImportedScene.h"
#include "Library/Framework/Asset/Import/ModelExporter.h"
#include "Library/Framework/Asset/Import/ModelFormats.h"
#include "Library/Framework/Asset/Import/XFileWriter.h"
#include "Library/Framework/Render/DrawContext.h"
#include "Library/Platform/FileSystem.h"

using namespace NAssetTest;

//=============================================================================
// モデルの取り込み（FBX / OBJ）・.mv1 への出力・アニメーションだけのファイルの確認（偽のバックエンドで。本物の DxLib での確認は CAssetSelfTestScene）
//=============================================================================
namespace
{
	std::vector<uint8> ToBytes( const std::string& Text ) { return std::vector<uint8>( Text.begin(), Text.end() ); }

	bool WriteFbx( const FString& Path, const FFbxSpec& Spec ) { return WriteFileBytes( Path, ToBytes( MakeBarFbx( Spec ) ) ); }

	bool Imports( const std::string& Text, const FString& Name, FImportedScene& Out, FString& Error )
	{
		return NModelImport::ImportFromMemory( Text.data(), Text.size(), Name, FImportOptions(), Out, Error );
	}

	struct FBounds
	{
		FVector3 Min = FVector3( 1.0e9f, 1.0e9f, 1.0e9f );
		FVector3 Max = FVector3( -1.0e9f, -1.0e9f, -1.0e9f );
	};

	FBounds GetBounds( const FImportedScene& Scene )
	{
		FBounds Bounds;
		for ( const FImportedMesh& Mesh : Scene.Meshes )
		{
			for ( const FVector3& P : Mesh.Positions )
			{
				Bounds.Min = FVector3( std::min( Bounds.Min.x, P.x ), std::min( Bounds.Min.y, P.y ), std::min( Bounds.Min.z, P.z ) );
				Bounds.Max = FVector3( std::max( Bounds.Max.x, P.x ), std::max( Bounds.Max.y, P.y ), std::max( Bounds.Max.z, P.z ) );
			}
		}
		return Bounds;
	}

	// 行ベクトル規約（v * M）の積
	FImportMatrix Multiply( const FImportMatrix& A, const FImportMatrix& B )
	{
		FImportMatrix Result;
		for ( int32 Row = 0; Row < 4; ++Row )
		{
			for ( int32 Column = 0; Column < 4; ++Column )
			{
				float32 Sum = 0.0f;
				for ( int32 K = 0; K < 4; ++K ) Sum += A.M[Row * 4 + K] * B.M[K * 4 + Column];
				Result.M[Row * 4 + Column] = Sum;
			}
		}
		return Result;
	}

	FVector3 TransformPoint( const FImportMatrix& M, const FVector3& V )
	{
		return FVector3( V.x * M.M[0] + V.y * M.M[4] + V.z * M.M[8] + M.M[12], V.x * M.M[1] + V.y * M.M[5] + V.z * M.M[9] + M.M[13], V.x * M.M[2] + V.y * M.M[6] + V.z * M.M[10] + M.M[14] );
	}

	// スキンのないメッシュの座標を、ノードのワールド行列で動かした範囲（ADJUST の軸の変換は、ノードの行列に入っていて、頂点は元のまま）
	FBounds GetWorldBounds( const FImportedScene& Scene )
	{
		FBounds Bounds;
		for ( const FImportedMesh& Mesh : Scene.Meshes )
		{
			FImportMatrix World = Scene.Nodes[Mesh.Node].Local;
			for ( int32 Parent = Scene.Nodes[Mesh.Node].Parent; Parent >= 0; Parent = Scene.Nodes[Parent].Parent ) World = Multiply( World, Scene.Nodes[Parent].Local );
			for ( const FVector3& P : Mesh.Positions )
			{
				const FVector3 W = TransformPoint( World, P );
				Bounds.Min = FVector3( std::min( Bounds.Min.x, W.x ), std::min( Bounds.Min.y, W.y ), std::min( Bounds.Min.z, W.z ) );
				Bounds.Max = FVector3( std::max( Bounds.Max.x, W.x ), std::max( Bounds.Max.y, W.y ), std::max( Bounds.Max.z, W.z ) );
			}
		}
		return Bounds;
	}

	bool BoundsNear( const FBounds& Bounds, float32 MinX, float32 MinY, float32 MinZ, float32 MaxX, float32 MaxY, float32 MaxZ, float32 Tolerance = 1.0e-4f )
	{
		return std::fabs( Bounds.Min.x - MinX ) <= Tolerance && std::fabs( Bounds.Min.y - MinY ) <= Tolerance && std::fabs( Bounds.Min.z - MinZ ) <= Tolerance
			   && std::fabs( Bounds.Max.x - MaxX ) <= Tolerance && std::fabs( Bounds.Max.y - MaxY ) <= Tolerance && std::fabs( Bounds.Max.z - MaxZ ) <= Tolerance;
	}

	int32 CountOf( const FString& Text, const char* Needle )
	{
		int32 Count = 0;
		int32 From = 0;
		const FString Pattern( Needle );
		while ( true )
		{
			const int32 Found = Text.Find( Pattern, ESearchCase::CaseSensitive, From );
			if ( Found == INDEX_NONE ) break;
			++Count;
			From = Found + Pattern.Len();
		}
		return Count;
	}

	int32 FindNode( const FImportedScene& Scene, const char* Name )
	{
		for ( int32 i = 0; i < Scene.Nodes.Num(); ++i )
		{
			if ( Scene.Nodes[i].Name == Name ) return i;
		}
		return INDEX_NONE;
	}

	FImportMatrix Translation( float32 X, float32 Y, float32 Z )
	{
		FImportMatrix M;
		M.M[12] = X;
		M.M[13] = Y;
		M.M[14] = Z;
		return M;
	}

	//=========================================================================
	// 形式の判定・骨の照らし合わせ
	//=========================================================================
	void TestModelFormats( FChecker& C )
	{
		C.Group = "model-formats";
		C.Check( NModelFormat::Classify( "a.MV1" ) == EModelFileKind::MV1 && NModelFormat::IsMV1( "dir/b.mv1" ) && !NModelFormat::IsMV1( "b.fbx" ), ".mv1 is recognised (any case)" );
		C.Check( NModelFormat::Classify( "x.fbx" ) == EModelFileKind::Imported && NModelFormat::Classify( "X.FBX" ) == EModelFileKind::Imported && NModelFormat::Classify( "b.obj" ) == EModelFileKind::Imported,
				 ".fbx / .obj are imported" );
		bool bNative = true;
		for ( const char* Name : { "c.x", "d.pmx", "e.PMD", "f.mqo", "g.mqoz", "h.vmd" } ) bNative = bNative && NModelFormat::Classify( Name ) == EModelFileKind::DxLibNative;
		C.Check( bNative, ".x / .pmd / .pmx / .mqo / .mqoz / .vmd are DxLib's own formats" );
		C.Check( NModelFormat::Classify( "i.png" ) == EModelFileKind::Unknown && NModelFormat::Classify( "noext" ) == EModelFileKind::Unknown && NModelFormat::Classify( "dir.v1/file" ) == EModelFileKind::Unknown
					 && NModelFormat::Classify( "" ) == EModelFileKind::Unknown,
				 "other files (and a dot in a folder name) are unknown" );
		C.Check( NModelFormat::CanConvertToMV1( "a.fbx" ) && NModelFormat::CanConvertToMV1( "a.pmx" ) && !NModelFormat::CanConvertToMV1( "a.mv1" ) && !NModelFormat::CanConvertToMV1( "a.png" ),
				 "everything but .mv1 itself and unknown files can be converted" );

		C.Check( NModelFormat::GetMV1Path( "Models/Robot.fbx" ) == "Models/Robot.mv1", "the .mv1 path replaces the extension" );
		C.Check( NModelFormat::GetMV1Path( "a/b.c/robot" ) == "a/b.c/robot.mv1", "a file without an extension gets .mv1 (a dot in a folder name is not an extension)" );
		C.Check( NModelFormat::GetMV1Path( "C:\\x\\Robot.PMX" ) == "C:\\x\\Robot.mv1", "the rest of the path keeps its case and separators" );

		const TArray<FString>& Convertible = NModelFormat::GetConvertibleExtensions();
		const TArray<FString>& All = NModelFormat::GetAllModelExtensions();
		C.Check( Convertible.Contains( ".fbx" ) && Convertible.Contains( ".x" ) && Convertible.Contains( ".vmd" ) && !Convertible.Contains( ".mv1" ) && All.Contains( ".mv1" ) && All.Contains( ".fbx" ),
				 "the extension lists" );

		FFileTime Older, Newer;
		Older.Year = 2026; Older.Month = 10; Older.Day = 17; Older.Hour = 23; Older.Minute = 59; Older.Second = 59;
		Newer.Year = 2026; Newer.Month = 10; Newer.Day = 18;   // 日付をまたぐ
		C.Check( NModelFormat::ToSortKey( Older ) < NModelFormat::ToSortKey( Newer ), "file times compare across a day boundary" );
		FFileTime NextYear = Newer;
		NextYear.Year = 2027;
		C.Check( NModelFormat::ToSortKey( Newer ) < NModelFormat::ToSortKey( NextYear ), "and across a year" );
		C.Check( NModelFormat::IsOutputUpToDate( Older, Older ) && NModelFormat::IsOutputUpToDate( Older, Newer ) && !NModelFormat::IsOutputUpToDate( Newer, Older ), "an output is up to date when it is not older than the source" );
		C.Check( !NModelFormat::IsOutputUpToDate( FFileTime(), Newer ) && !NModelFormat::IsOutputUpToDate( Older, FFileTime() ), "an unknown time is never up to date" );

		// 骨の照らし合わせ
		const TArray<FString> Model { "Hips", "Spine", "Head" };
		FBoneMatch Match = NModelCompat::MatchBones( Model, TArray<FString> { "Hips", "Spine", "Arm", "Leg" } );
		C.Check( Match.NumAnimBones == 4 && Match.NumMatched == 2 && Match.Missing.Num() == 2 && Match.Missing[0] == "Arm" && !Match.IsPoor(), "bones match by name (2 of 4 is not poor)" );
		C.Near( Match.GetRatio(), 0.5, 1.0e-6, "match ratio" );
		C.Check( NModelCompat::MatchBones( Model, TArray<FString> { "X", "Y", "Z" } ).IsPoor(), "no common bone is poor" );
		C.Check( NModelCompat::MatchBones( Model, TArray<FString>() ).GetRatio() == 1.0f && !NModelCompat::MatchBones( Model, TArray<FString>() ).IsPoor(), "nothing to compare is not poor" );
		C.Check( NModelCompat::MatchBones( Model, TArray<FString> { "hips" } ).NumMatched == 0, "bone names are case-sensitive" );
		TArray<FString> Many;
		for ( int32 i = 0; i < 20; ++i ) Many.Add( FString::Printf( "Missing%d", i ) );
		const FBoneMatch ManyMatch = NModelCompat::MatchBones( Model, Many );
		C.Check( ManyMatch.NumAnimBones == 20 && ManyMatch.Missing.Num() == NModelCompat::MaxMissingNames, "the list of missing names is capped" );
		C.Check( NModelCompat::MatchBones( Model, TArray<FString> { "Hips", "Hips" } ).NumMatched == 2, "a repeated animation bone counts each time" );
	}

	//=========================================================================
	// 中間データの変換
	//=========================================================================
	FImportedScene MakeTinyScene()
	{
		FImportedScene Scene;
		FImportedNode Root;
		Root.Name = "Root";
		Root.Local = Translation( 1.0f, 2.0f, 3.0f );
		Scene.Nodes.Add( Root );
		FImportedNode Bone;
		Bone.Name = "Bone";
		Bone.Parent = 0;
		const float32 S = std::sin( 0.5f ), Cs = std::cos( 0.5f );
		Bone.Local.M[5] = Cs; Bone.Local.M[6] = S; Bone.Local.M[9] = -S; Bone.Local.M[10] = Cs;   // X まわりの回転
		Scene.Nodes.Add( Bone );

		FImportedMesh Mesh;
		Mesh.Name = "Tri";
		Mesh.Node = 0;
		Mesh.Positions = { FVector3( 0.0f, 0.0f, 1.0f ), FVector3( 1.0f, 0.0f, 2.0f ), FVector3( 0.0f, 1.0f, 3.0f ) };
		Mesh.Normals = { FVector3( -1.0f, -2.0f, 1.0f ), FVector3( -1.0f, -2.0f, 1.0f ), FVector3( -1.0f, -2.0f, 1.0f ) };
		Mesh.Indices = { 0, 1, 2 };
		Mesh.TriangleMaterials = { -1 };
		FImportedCluster Cluster;
		Cluster.BoneNode = 1;
		Cluster.Offset = Translation( 0.0f, 0.0f, 5.0f );
		Cluster.Vertices = { 0, 1, 2 };
		Cluster.Weights = { 1.0f, 1.0f, 1.0f };
		Mesh.Clusters.Add( Cluster );
		Scene.Meshes.Add( Mesh );

		FImportedAnimation Animation;
		Animation.Name = "Move";
		Animation.NumKeys = 2;
		FImportedTrack Track;
		Track.Node = 1;
		Track.Keys = { FImportMatrix(), Translation( 0.0f, 0.0f, 7.0f ) };
		Animation.Tracks.Add( Track );
		Scene.Animations.Add( Animation );
		return Scene;
	}

	FVector3 FaceNormal( const FImportedMesh& Mesh, int32 Triangle )
	{
		const FVector3& A = Mesh.Positions[Mesh.Indices[Triangle * 3]];
		const FVector3& B = Mesh.Positions[Mesh.Indices[Triangle * 3 + 1]];
		const FVector3& P = Mesh.Positions[Mesh.Indices[Triangle * 3 + 2]];
		const FVector3 E1 = B - A, E2 = P - B;
		return FVector3( E1.y * E2.z - E1.z * E2.y, E1.z * E2.x - E1.x * E2.z, E1.x * E2.y - E1.y * E2.x );
	}

	void TestImportedScene( FChecker& C )
	{
		C.Group = "imported-scene";

		// 行列を Z で鏡に映す
		const FImportMatrix T = NImportedScene::MirrorZ( Translation( 1.0f, 2.0f, 3.0f ) );
		C.Check( T.M[12] == 1.0f && T.M[13] == 2.0f && T.M[14] == -3.0f && T.M[0] == 1.0f && T.M[5] == 1.0f && T.M[10] == 1.0f, "mirroring negates the translation's Z and keeps the rest" );
		FImportMatrix RotZ;
		RotZ.M[0] = 0.0f; RotZ.M[1] = 1.0f; RotZ.M[4] = -1.0f; RotZ.M[5] = 0.0f;
		C.Check( NImportedScene::MirrorZ( RotZ ).NearlyEquals( RotZ ), "a rotation around Z is the same after mirroring Z" );
		FImportMatrix RotX;
		RotX.M[5] = 0.0f; RotX.M[6] = 1.0f; RotX.M[9] = -1.0f; RotX.M[10] = 0.0f;
		const FImportMatrix MirroredX = NImportedScene::MirrorZ( RotX );
		C.Check( MirroredX.M[6] == -1.0f && NImportedScene::MirrorZ( MirroredX ).NearlyEquals( RotX ) && MirroredX.M[9] == 1.0f, "a rotation around X turns the other way after mirroring Z" );
		C.Check( NImportedScene::MirrorZ( NImportedScene::MirrorZ( Translation( 4.0f, 5.0f, 6.0f ) ) ).NearlyEquals( Translation( 4.0f, 5.0f, 6.0f ) ), "mirroring twice returns the original" );

		// シーン全体
		FImportedScene Scene = MakeTinyScene();
		const FImportedScene Before = Scene;
		C.Check( NImportedScene::Validate( Scene ).IsEmpty(), "the sample scene is valid" );
		NImportedScene::ConvertToLeftHanded( Scene );
		const FImportedMesh& Mesh = Scene.Meshes[0];
		C.Check( Mesh.Positions.Num() == 3 && Mesh.Positions[0].z == -1.0f && Mesh.Positions[1].z == -2.0f && Mesh.Positions[2].z == -3.0f && Mesh.Positions[1].x == 1.0f && Mesh.Positions[2].y == 1.0f,
				 "positions: only Z changes sign" );
		C.Check( Mesh.Normals[0].z == -1.0f && Mesh.Normals[0].x == -1.0f && Mesh.Normals[0].y == -2.0f, "normals: only Z changes sign" );
		C.Check( Mesh.Indices[0] == 0 && Mesh.Indices[1] == 2 && Mesh.Indices[2] == 1, "the triangle's winding is reversed" );
		// 巻き方から求めた面の向きが、鏡に映した元の向きと同じ（表のまま）
		const FVector3 Original = FaceNormal( Before.Meshes[0], 0 );
		const FVector3 Converted = FaceNormal( Mesh, 0 );
		C.Check( Converted.x == Original.x && Converted.y == Original.y && Converted.z == -Original.z, "the face still faces the mirrored way (outside stays outside)" );
		C.Check( Scene.Nodes[0].Local.M[14] == -3.0f && Scene.Nodes[0].Local.M[12] == 1.0f, "node transforms are mirrored" );
		C.Check( Scene.Nodes[1].Local.M[6] == -std::sin( 0.5f ) && Scene.Nodes[1].Local.M[9] == std::sin( 0.5f ) && Scene.Nodes[1].Local.M[5] == std::cos( 0.5f ), "a node's rotation is mirrored" );
		C.Check( Mesh.Clusters[0].Offset.M[14] == -5.0f, "skin offsets are mirrored" );
		C.Check( Scene.Animations[0].Tracks[0].Keys[1].M[14] == -7.0f && Scene.Animations[0].Tracks[0].Keys[0].NearlyEquals( FImportMatrix() ), "animation keys are mirrored" );
		C.Check( Mesh.Clusters[0].Vertices == Before.Meshes[0].Clusters[0].Vertices && Mesh.TriangleMaterials == Before.Meshes[0].TriangleMaterials, "vertex numbers and materials are untouched" );

		// 1 頂点に影響する骨
		TArray<FImportInfluence> Influences;
		for ( int32 i = 0; i < 6; ++i ) Influences.Add( FImportInfluence { i, static_cast<float32>( 6 - i ) } );   // 重み 6, 5, 4, 3, 2, 1
		NImportedScene::LimitInfluences( Influences );
		float32 Sum = 0.0f;
		for ( const FImportInfluence& Influence : Influences ) Sum += Influence.Weight;
		C.Check( Influences.Num() == 4 && Influences[0].Cluster == 0 && Influences[3].Cluster == 3 && std::fabs( Sum - 1.0f ) < 1.0e-5f, "the 4 strongest bones are kept and the weights sum to 1" );
		C.Near( Influences[0].Weight, 6.0 / 18.0, 1.0e-5, "the weights keep their proportions" );
		TArray<FImportInfluence> Half { FImportInfluence { 0, 0.25f }, FImportInfluence { 1, 0.25f } };
		NImportedScene::LimitInfluences( Half );
		C.Check( Half.Num() == 2 && std::fabs( Half[0].Weight - 0.5f ) < 1.0e-5f, "weights that sum to less than 1 are scaled up" );
		TArray<FImportInfluence> Zeros { FImportInfluence { 0, 0.0f }, FImportInfluence { 1, -1.0f } };
		NImportedScene::LimitInfluences( Zeros );
		C.Check( Zeros.IsEmpty(), "zero and negative weights are dropped" );
		TArray<FImportInfluence> Ties { FImportInfluence { 7, 1.0f }, FImportInfluence { 3, 1.0f }, FImportInfluence { 5, 1.0f } };
		NImportedScene::LimitInfluences( Ties, 2 );
		C.Check( Ties.Num() == 2 && Ties[0].Cluster == 7 && Ties[1].Cluster == 3, "equal weights keep their order (the result does not depend on the sort)" );

		// 名前
		C.Check( NImportedScene::SanitizeName( "mixamorig:Hips", "x" ) == "mixamorig:Hips" && NImportedScene::SanitizeName( "bone.001", "x" ) == "bone.001" && NImportedScene::SanitizeName( "A|B", "x" ) == "A|B",
				 "':' '.' '|' are kept (DxLib reads them)" );
		C.Check( NImportedScene::SanitizeName( "Left Arm", "x" ) == "Left_Arm", "a space is replaced (DxLib cuts the name at a space)" );
		C.Check( NImportedScene::SanitizeName( "a;b{c}d,e\"f<g>h#i/j\tk", "x" ) == "a_b_c_d_e_f_g_h_i_j_k", "separators and comment characters are replaced" );
		C.Check( NImportedScene::SanitizeName( "", "Fallback" ) == "Fallback", "an empty name uses the fallback" );
		C.Check( NImportedScene::SanitizeName( "\x8d\x82 1", "x" ) == "\x8d\x82_1", "Shift-JIS characters are kept" );
		FImportedScene Names;
		for ( const char* Name : { "Bone", "Bone", "Bone_2", "Bone", "", "Left Arm", "Left_Arm" } )
		{
			FImportedNode Node;
			Node.Name = Name;
			Names.Nodes.Add( Node );
		}
		const TArray<FString> Frames = NImportedScene::MakeFrameNames( Names );
		C.Check( Frames.Num() == 7 && Frames[0] == "Bone" && Frames[1] == "Bone_2" && Frames[2] == "Bone_2_2" && Frames[3] == "Bone_3" && Frames[4] == "Node4" && Frames[5] == "Left_Arm" && Frames[6] == "Left_Arm_2",
				 "frame names are unique, even when a generated name collides (%s %s %s %s %s %s %s)", *Frames[0], *Frames[1], *Frames[2], *Frames[3], *Frames[4], *Frames[5], *Frames[6] );

		// 構造の確認
		FImportedScene Bad = MakeTinyScene();
		Bad.Nodes[1].Parent = 5;
		Bad.Meshes[0].Normals.Add( FVector3() );
		Bad.Meshes[0].Indices[0] = 9;
		Bad.Meshes[0].TriangleMaterials[0] = 3;
		Bad.Meshes[0].Clusters[0].Weights.Add( 1.0f );
		Bad.Animations[0].Tracks[0].Keys.Add( FImportMatrix() );
		const TArray<FString> Problems = NImportedScene::Validate( Bad );
		const auto Mentions = [&]( const char* Text ) { return Problems.ContainsByPredicate( [Text]( const FString& Problem ) { return Problem.Contains( Text ); } ); };
		C.Check( Mentions( "invalid parent" ) && Mentions( "normal(s)" ) && Mentions( "out of range" ) && Mentions( "refers to the material" ) && Mentions( "weights" ) && Mentions( "expected" ),
				 "validation finds every kind of structural problem (%d problem(s))", Problems.Num() );
	}

	//=========================================================================
	// .x への書き出し
	//=========================================================================
	void TestXFileWriter( FChecker& C )
	{
		C.Group = "x-file-writer";

		FImportedScene Scene;
		const char* NodeNames[] = { "Root", "Left Arm", "Left Arm" };
		const int32 Parents[] = { -1, 0, 0 };
		for ( int32 i = 0; i < 3; ++i )
		{
			FImportedNode Node;
			Node.Name = NodeNames[i];
			Node.Parent = Parents[i];
			Node.Local = Translation( static_cast<float32>( i ), 0.0f, 0.0f );
			Scene.Nodes.Add( Node );
		}
		FImportedTexture Texture;
		Texture.File = "tex/a.png";
		Scene.Textures.Add( Texture );
		FImportedMaterial Material;
		Material.Name = "Mat";
		Material.Diffuse[0] = 0.25f;
		Material.Texture = 0;
		Scene.Materials.Add( Material );

		FImportedMesh Plain;
		Plain.Name = "Plain";
		Plain.Node = 0;
		Plain.Positions = { FVector3( 0, 0, 0 ), FVector3( 1, 0, 0 ), FVector3( 0, 1, 0 ) };
		Plain.Normals = { FVector3( 0, 0, 1 ), FVector3( 0, 0, 1 ), FVector3( 0, 0, 1 ) };
		Plain.UVs = { FVector2( 0, 0 ), FVector2( 1, 0 ), FVector2( 0, 1 ) };
		Plain.Indices = { 0, 1, 2 };
		Plain.TriangleMaterials = { 0 };
		Scene.Meshes.Add( Plain );

		FImportedMesh Skinned = Plain;
		Skinned.Name = "Arm";
		Skinned.Node = 1;
		Skinned.UVs.Empty();
		Skinned.TriangleMaterials = { -1 };
		FImportedCluster Cluster;
		Cluster.BoneNode = 2;
		Cluster.Vertices = { 0, 1, 2 };
		Cluster.Weights = { 1.0f, 0.5f, 0.25f };
		Skinned.Clusters.Add( Cluster );
		Scene.Meshes.Add( Skinned );

		FImportedAnimation Animation;
		Animation.Name = "Wave Take";
		Animation.NumKeys = 2;
		FImportedTrack Track;
		Track.Node = 1;
		Track.Keys = { Translation( 0, 0, 0 ), Translation( 5, 0, 0 ) };
		Animation.Tracks.Add( Track );
		Scene.Animations.Add( Animation );
		FImportedAnimation Empty;
		Empty.Name = "Nothing";
		Empty.NumKeys = 2;
		Scene.Animations.Add( Empty );

		C.Check( NImportedScene::Validate( Scene ).IsEmpty(), "the sample scene is valid" );
		const TArray<FString> TexturePaths { "images/a.png" };
		const FString Text = NXFile::Write( Scene, TexturePaths );
		C.Check( Text.StartsWith( "xof 0303txt 0032" ), "the file starts with the text .x header" );
		C.Check( NXFile::Write( Scene, TexturePaths ) == Text, "the same scene gives the same text" );
		C.Check( CountOf( Text, "{" ) == CountOf( Text, "}" ), "braces are balanced (%d / %d)", CountOf( Text, "{" ), CountOf( Text, "}" ) );
		C.Check( Text.Contains( "Frame Root {" ) && Text.Contains( "Frame Left_Arm {" ) && Text.Contains( "Frame Left_Arm_2 {" ) && !Text.Contains( "Left Arm" ), "frames use the sanitized unique names" );
		C.Check( CountOf( Text, "Mesh {" ) == 2 && CountOf( Text, "MeshNormals {" ) == 2 && CountOf( Text, "MeshTextureCoords {" ) == 1, "both meshes are written (UVs only where the mesh has them)" );
		C.Check( Text.Contains( "TextureFilename { \"images/a.png\"; }" ) && !Text.Contains( "tex/a.png" ), "the texture path comes from the paths given to the writer" );
		C.Check( Text.Contains( "3;0,1,2;;" ), "faces are written as 3 vertex indices in order" );
		C.Check( Text.Contains( "Frame Arm_Skin {" ) && Text.Contains( "XSkinMeshHeader {" ) && CountOf( Text, "SkinWeights {" ) == 1 && Text.Contains( "\"Left_Arm_2\";" ),
				 "a skinned mesh gets its own frame, and its weights name the bone's frame" );
		const int32 SkinFrame = Text.Find( "Frame Arm_Skin {" );
		C.Check( SkinFrame != INDEX_NONE && Text.Mid( SkinFrame, 120 ).Contains( "FrameTransformMatrix { 1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1;; }" ), "the skinned mesh's frame has the identity matrix" );
		C.Check( Text.Contains( "AnimationSet Wave_Take {" ) && Text.Contains( "{ Left_Arm }" ) && Text.Contains( "AnimationKey {" ) && !Text.Contains( "Nothing" ) && CountOf( Text, "AnimationSet" ) == 1,
				 "the animation with tracks is written; the one without tracks is not" );
		C.Check( Text.Contains( "1;16;1,0,0,0,0,1,0,0,0,0,1,0,5,0,0,1;;;" ), "the last key is written as a matrix with the translation in the 4th row" );
		C.Check( Text.Contains( "Material {" ) && CountOf( Text, "Material {" ) == 2, "each mesh writes the materials it uses (the default one for -1)" );
	}

	//=========================================================================
	// FBX / OBJ の読み込み（座標・スキン・アニメーション）
	//=========================================================================
	void TestFbxImport( FChecker& C )
	{
		C.Group = "fbx-import";

		FImportedScene Scene;
		FString Error;
		C.Check( Imports( MakeBarFbx( FFbxSpec() ), "C:/x/bar.fbx", Scene, Error ), "the generated FBX is read (%s)", *Error );
		if ( Scene.Nodes.IsEmpty() ) return;

		const int32 Bar = FindNode( Scene, "Bar" ), Bone1 = FindNode( Scene, "Bone1" ), Bone2 = FindNode( Scene, "Bone2" );
		C.Check( Scene.Nodes.Num() == 3 && Bar != INDEX_NONE && Bone1 != INDEX_NONE && Bone2 != INDEX_NONE, "the nodes are Bar, Bone1 and Bone2 (%d node(s))", Scene.Nodes.Num() );
		C.Check( Bone2 != INDEX_NONE && Scene.Nodes[Bone2].Parent == Bone1 && Scene.Nodes[Bone1].Parent == -1 && Scene.Nodes[Bar].Parent == -1, "the hierarchy is kept (Bone2 is a child of Bone1)" );
		C.Check( Bone2 != INDEX_NONE && Scene.Nodes[Bone2].Local.M[13] == 1.0f && Scene.Nodes[Bone2].Local.M[12] == 0.0f, "Bone2's local translation is (0, 1, 0)" );

		C.Check( Scene.Meshes.Num() == 1 && Scene.Meshes[0].Node == Bar, "one mesh, on the Bar node" );
		if ( Scene.Meshes.IsEmpty() ) return;
		const FImportedMesh& Mesh = Scene.Meshes[0];
		C.Check( Mesh.GetNumTriangles() == 20 && Mesh.Positions.Num() == 40 && Mesh.Normals.Num() == 40 && Mesh.UVs.Num() == 40, "10 quads become 20 triangles; corners with different normals / UVs are separate vertices (%d tris, %d vertices)",
				 Mesh.GetNumTriangles(), Mesh.Positions.Num() );
		C.Check( BoundsNear( GetBounds( Scene ), -0.5f, 0.0f, -0.5f, 0.5f, 2.0f, 1.5f ), "the bounds are the prism's (right-handed, as in the file)" );
		bool bOutward = true;
		for ( int32 Triangle = 0; Triangle < Mesh.GetNumTriangles(); ++Triangle )
		{
			// 巻き方から求めた面の向きが、外向き（柱の中心から外へ）
			const FVector3 N = FaceNormal( Mesh, Triangle );
			const FVector3 Centroid = ( Mesh.Positions[Mesh.Indices[Triangle * 3]] + Mesh.Positions[Mesh.Indices[Triangle * 3 + 1]] + Mesh.Positions[Mesh.Indices[Triangle * 3 + 2]] ) * ( 1.0f / 3.0f );
			bOutward = bOutward && N.x * Centroid.x + N.y * ( Centroid.y - 1.0f ) + N.z * ( Centroid.z - 0.5f ) > 0.0f;
		}
		C.Check( bOutward, "every triangle faces outward by its winding (the quads were split without flipping)" );
		bool bFlippedUV = true;
		for ( const FVector2& UV : Mesh.UVs )
		{
			// ファイルの V は 0 / 0.25（下が 0）。DirectX は上が 0 なので、1 / 0.75 になる
			bFlippedUV = bFlippedUV && ( UV.x == 0.0f || UV.x == 1.0f ) && ( std::fabs( UV.y - 1.0f ) < 1.0e-5f || std::fabs( UV.y - 0.75f ) < 1.0e-5f );
		}
		C.Check( bFlippedUV, "UVs are flipped vertically (V = 1 - V)" );

		// マテリアルと画像
		C.Check( Scene.Materials.Num() == 1 && std::fabs( Scene.Materials[0].Diffuse[0] - 0.25f ) < 1.0e-5f && std::fabs( Scene.Materials[0].Diffuse[2] - 0.75f ) < 1.0e-5f, "the material's diffuse colour" );
		C.Check( Scene.Textures.Num() == 1 && Scene.Textures[0].File == "sheet.bmp" && Scene.Materials[0].Texture == 0 && Scene.Textures[0].EmbeddedData.IsEmpty(), "the diffuse texture is referenced by file name" );
		bool bAllMaterial0 = true;
		for ( const int32 Material : Mesh.TriangleMaterials ) bAllMaterial0 = bAllMaterial0 && Material == 0;
		C.Check( bAllMaterial0, "every triangle uses the material" );

		// スキン
		C.Check( Mesh.Clusters.Num() == 2, "two clusters (bones)" );
		if ( Mesh.Clusters.Num() == 2 )
		{
			TArray<float32> Totals;
			Totals.SetNum( Mesh.Positions.Num(), 0.0f );
			for ( const FImportedCluster& Cluster : Mesh.Clusters )
				for ( int32 i = 0; i < Cluster.Vertices.Num(); ++i ) Totals[Cluster.Vertices[i]] += Cluster.Weights[i];
			bool bNormalised = true;
			for ( const float32 Total : Totals ) bNormalised = bNormalised && std::fabs( Total - 1.0f ) < 1.0e-4f;
			C.Check( bNormalised, "the weights of every vertex sum to 1" );
			const FImportedCluster* ForBone1 = Mesh.Clusters[0].BoneNode == Bone1 ? &Mesh.Clusters[0] : &Mesh.Clusters[1];
			const FImportedCluster* ForBone2 = Mesh.Clusters[0].BoneNode == Bone2 ? &Mesh.Clusters[0] : &Mesh.Clusters[1];
			C.Check( ForBone1->BoneNode == Bone1 && ForBone2->BoneNode == Bone2, "the clusters point at Bone1 and Bone2" );
			C.Check( ForBone1->Offset.NearlyEquals( FImportMatrix() ), "Bone1's offset (mesh -> bone at bind time) is the identity" );
			C.Check( ForBone2->Offset.NearlyEquals( Translation( 0.0f, -1.0f, 0.0f ) ), "Bone2's offset moves the mesh down by 1 (Bone2 binds at y = 1): diag %.3f %.3f %.3f, translation %.3f %.3f %.3f", ForBone2->Offset.M[0], ForBone2->Offset.M[5],
					 ForBone2->Offset.M[10], ForBone2->Offset.M[12], ForBone2->Offset.M[13], ForBone2->Offset.M[14] );
			// 下の輪（y = 0）は Bone1 だけ、上の輪（y = 2）は Bone2 だけ
			bool bRings = true;
			for ( const FImportedCluster& Cluster : Mesh.Clusters )
			{
				for ( int32 i = 0; i < Cluster.Vertices.Num(); ++i )
				{
					const float32 Y = Mesh.Positions[Cluster.Vertices[i]].y;
					if ( Y == 0.0f && !( Cluster.BoneNode == Bone1 && Cluster.Weights[i] == 1.0f ) ) bRings = false;
					if ( Y == 2.0f && !( Cluster.BoneNode == Bone2 && Cluster.Weights[i] == 1.0f ) ) bRings = false;
					if ( Y == 1.0f && std::fabs( Cluster.Weights[i] - 0.5f ) > 1.0e-5f ) bRings = false;
				}
			}
			C.Check( bRings, "bottom ring -> Bone1, top ring -> Bone2, middle ring half and half" );
		}

		// アニメーション
		C.Check( Scene.Animations.Num() == 1 && Scene.Animations[0].Name == "Wave" && Scene.Animations[0].NumKeys == 31 && Scene.Animations[0].Fps == 30.0f, "one animation 'Wave': 1 second at 30 fps is 31 keys" );
		if ( Scene.Animations.Num() == 1 && Scene.Animations[0].Tracks.Num() == 1 )
		{
			const FImportedTrack& Track = Scene.Animations[0].Tracks[0];
			C.Check( Track.Node == Bone2 && Track.Keys.Num() == 31, "only Bone2 moves (Bone1 and Bar have no track)" );
			C.Check( Track.Keys[0].NearlyEquals( Scene.Nodes[Bone2].Local, 1.0e-4f ), "the first key is the rest pose" );
			const FImportMatrix& Last = Track.Keys[30];
			C.Check( std::fabs( Last.M[0] ) < 1.0e-4f && std::fabs( Last.M[1] - 1.0f ) < 1.0e-4f && std::fabs( Last.M[4] + 1.0f ) < 1.0e-4f && std::fabs( Last.M[13] - 1.0f ) < 1.0e-4f,
					 "the last key is a 90 degree turn around Z, still at (0, 1, 0)" );
			const FImportMatrix& Middle = Track.Keys[15];
			C.Check( std::fabs( Middle.M[0] - 0.70710678f ) < 2.0e-3f && std::fabs( Middle.M[1] - 0.70710678f ) < 2.0e-3f, "the middle key is 45 degrees (the curve is symmetric)" );
		}
		else
		{
			C.Check( false, "the animation has exactly one track" );
		}

		// 左手系にしたあと
		FImportedScene Converted = Scene;
		NImportedScene::ConvertToLeftHanded( Converted );
		C.Check( BoundsNear( GetBounds( Converted ), -0.5f, 0.0f, -1.5f, 0.5f, 2.0f, 0.5f ), "after conversion the bounds are mirrored in Z (-1.5 .. 0.5)" );
		C.Check( NImportedScene::Validate( Converted ).IsEmpty(), "the converted scene is still valid" );

		// 上の軸が Z の FBX は、Y が上に直る
		FFbxSpec ZUp;
		ZUp.bSkeleton = false;
		ZUp.bAnimation = false;
		ZUp.bZUp = true;
		FImportedScene FromZUp;
		C.Check( Imports( MakeBarFbx( ZUp ), "C:/x/barz.fbx", FromZUp, Error ), "a Z-up FBX is read (%s)", *Error );
		const FBounds ZBounds = GetWorldBounds( FromZUp );
		C.Check( BoundsNear( ZBounds, -0.5f, 0.0f, -0.5f, 0.5f, 2.0f, 1.5f, 2.0e-3f ), "a Z-up file ends up with the same bounds as the Y-up one: (%.3f %.3f %.3f) - (%.3f %.3f %.3f)", ZBounds.Min.x, ZBounds.Min.y, ZBounds.Min.z,
				 ZBounds.Max.x, ZBounds.Max.y, ZBounds.Max.z );

		// メッシュのない FBX（モーション専用）
		FFbxSpec MotionOnly;
		MotionOnly.bMesh = false;
		FImportedScene Motion;
		C.Check( Imports( MakeBarFbx( MotionOnly ), "C:/x/wave.fbx", Motion, Error ), "an FBX without a mesh is read (%s)", *Error );
		C.Check( Motion.Meshes.IsEmpty() && Motion.Nodes.Num() == 2 && Motion.Animations.Num() == 1 && Motion.Animations[0].Tracks.Num() == 1 && !Motion.HasMesh(), "it has the skeleton and the animation, and no mesh" );

		// OBJ
		FImportedScene Obj;
		const std::string ObjText = MakeBoxObj( "box.mtl" );
		C.Check( Imports( ObjText, "C:/x/box.obj", Obj, Error ), "an OBJ is read (%s)", *Error );
		C.Check( Obj.Meshes.Num() == 1 && Obj.Meshes[0].GetNumTriangles() == 12, "6 quads become 12 triangles" );
		C.Check( BoundsNear( GetBounds( Obj ), -1.0f, 0.0f, -1.0f, 1.0f, 2.0f, 2.0f ), "the OBJ's bounds" );
		bool bObjOutward = !Obj.Meshes.IsEmpty();
		for ( int32 Triangle = 0; bObjOutward && Triangle < Obj.Meshes[0].GetNumTriangles(); ++Triangle )
		{
			const FImportedMesh& M = Obj.Meshes[0];
			const FVector3 N = FaceNormal( M, Triangle );
			const FVector3 Centroid = ( M.Positions[M.Indices[Triangle * 3]] + M.Positions[M.Indices[Triangle * 3 + 1]] + M.Positions[M.Indices[Triangle * 3 + 2]] ) * ( 1.0f / 3.0f );
			bObjOutward = N.x * Centroid.x + N.y * ( Centroid.y - 1.0f ) + N.z * ( Centroid.z - 0.5f ) > 0.0f;
		}
		C.Check( bObjOutward, "the OBJ's triangles face outward" );

		// 壊れたデータ・空のデータ
		C.Check( !Imports( "this is not a model", "C:/x/broken.fbx", Scene, Error ) && !Error.IsEmpty(), "a broken file is refused with a reason" );
		C.Check( !NModelImport::ImportFromMemory( nullptr, 0, "C:/x/empty.fbx", FImportOptions(), Scene, Error ) && !Error.IsEmpty(), "an empty file is refused with a reason" );
		FImportOptions NoAnimation;
		NoAnimation.bImportAnimations = false;
		const std::string Text = MakeBarFbx( FFbxSpec() );
		FImportedScene Silent;
		NModelImport::ImportFromMemory( Text.data(), Text.size(), "C:/x/bar.fbx", NoAnimation, Silent, Error );
		C.Check( Silent.Animations.IsEmpty() && !Silent.Meshes.IsEmpty(), "animations can be skipped" );
		FImportOptions Slow;
		Slow.AnimFps = 10.0f;
		FImportedScene TenFps;
		NModelImport::ImportFromMemory( Text.data(), Text.size(), "C:/x/bar.fbx", Slow, TenFps, Error );
		C.Check( TenFps.Animations.Num() == 1 && TenFps.Animations[0].NumKeys == 11 && TenFps.Animations[0].Fps == 10.0f, "the sampling rate is an option (10 fps -> 11 keys)" );
	}
}

//=============================================================================
// 純粋な確認をまとめて
//=============================================================================
namespace NAssetTest
{
	void RunImportPureTests( FChecker& C )
	{
		TestModelFormats( C );
		TestImportedScene( C );
		TestXFileWriter( C );
		TestFbxImport( C );
	}
}

//=============================================================================
// 出力（FBX / OBJ / DxLib 対応形式 -> .mv1）・マネージャーでの読み込み・アニメーションだけのファイル
//=============================================================================
namespace
{
	std::vector<FDrawModelAnimData> CollectModelLayers( const FModelAnimator& Animator )
	{
		FDrawBuffer Buffer;
		FDrawContext Draw( Buffer );
		Animator.Record( Draw, FMatrix4x4::Identity );
		FCommandLog Log;
		Log.Execute( FDrawSpan { Buffer.Words.data(), static_cast<int32>( Buffer.Words.size() ) } );
		std::vector<FDrawModelAnimData> Layers;
		Log.ForEach( [&]( const FDrawCommandView& Command )
		{
			if ( Command.Type == EDrawCommand::ModelAnim ) Layers.push_back( Command.Read<FDrawModelAnimData>() );
		} );
		return Layers;
	}

	// 最初の整数（"Mesh {" の次の頂点の数）
	int32 ReadIntAfter( const std::string& Text, const char* Marker )
	{
		const size_t At = Text.find( Marker );
		if ( At == std::string::npos ) return -1;
		return std::atoi( Text.c_str() + At + std::strlen( Marker ) );
	}

	bool Ends( const FString& Text, const char* Suffix ) { return Text.EndsWith( Suffix ); }

	void TestRelativePath( FChecker& C )
	{
		C.Group = "relative-path";
		C.Check( NModelExport::MakeRelativePath( "C:/a/b", "C:/a/b/tex.png" ) == "tex.png", "a file in the same folder" );
		C.Check( NModelExport::MakeRelativePath( "C:/a/b", "C:/a/c/tex.png" ) == "../c/tex.png", "a file in a sibling folder" );
		C.Check( NModelExport::MakeRelativePath( "C:/a/b/c", "C:/a/tex.png" ) == "../../tex.png", "a file two levels up" );
		C.Check( NModelExport::MakeRelativePath( "C:/a/b", "C:/a/b/sub/dir/tex.png" ) == "sub/dir/tex.png", "a file in a subfolder" );
		C.Check( NModelExport::MakeRelativePath( "C:/a/b", "D:/x/tex.png" ) == "D:/x/tex.png", "another drive stays absolute" );
		C.Check( NModelExport::MakeRelativePath( "c:\\A\\B", "C:/a/b/t.png" ) == "t.png", "case and separators do not matter" );
	}

	void TestModelExport( FChecker& C, FFakeAssetBackend& Fake, const FString& Dir )
	{
		C.Group = "model-export";
		const FString Folder = Dir / "export";
		CreateDirectoryA( *Folder, nullptr );
		Fake.SavedModels.clear();
		Fake.LoadedModelPaths.clear();

		// DxLib が読める形式: そのまま読んで保存
		WriteFileText( Folder / "m1.pmx", "x" );
		Fake.NextModelData = { { "Idle", "Run" }, { 60.0f, 30.0f } };
		FModelExportResult R = NModelExport::Export( Fake, Folder / "m1.pmx" );
		C.Check( R.bSuccess && !R.bSkippedUpToDate && !R.bImported && R.NumMeshes == 1 && R.NumAnimations == 2 && R.SavedType == EModelSaveType::Normal && Ends( R.OutputPath, "export/m1.mv1" ),
				 "a DxLib format is exported next to the source (%s)", *R.Error );
		C.Check( Fake.SavedModels.size() == 1 && Fake.SavedModels[0].Options.Type == EModelSaveType::Normal && Fake.SavedModels[0].Options.bCompress && Ends( Fake.SavedModels[0].Path, "export/m1.mv1" ),
				 "the backend saved it with normal, compressed settings" );
		C.Check( NFileSystem::FileExists( Folder / "m1.mv1" ) && R.OutputBytes == 7, "the output file exists and its size is recorded" );
		C.Check( Fake.Models.empty(), "the temporary model handle is released" );
		C.Check( R.GetSummary().Contains( "mesh and animations" ) && R.GetSummary().Contains( "2 animation(s)" ), "the summary describes the result (%s)", *R.GetSummary() );
		C.Check( Fake.LoadedModelPaths.size() == 1 && Ends( Fake.LoadedModelPaths[0], "m1.pmx" ), "the source itself was loaded (no temporary file for a DxLib format)" );

		// 最新なら何もしない。bForce で作り直す。元が新しくなったら作り直す
		R = NModelExport::Export( Fake, Folder / "m1.pmx" );
		C.Check( R.bSuccess && R.bSkippedUpToDate && Fake.SavedModels.size() == 1 && Fake.LoadedModelPaths.size() == 1, "an up-to-date output is left alone (nothing is loaded or saved)" );
		FModelExportOptions Force;
		Force.bForce = true;
		R = NModelExport::Export( Fake, Folder / "m1.pmx", Force );
		C.Check( R.bSuccess && !R.bSkippedUpToDate && Fake.SavedModels.size() == 2, "bForce exports again" );
		SetLastWriteOffset( Folder / "m1.pmx", 30 );
		R = NModelExport::Export( Fake, Folder / "m1.pmx" );
		C.Check( R.bSuccess && !R.bSkippedUpToDate && Fake.SavedModels.size() == 3, "a source newer than its output is exported again" );
		SetLastWriteOffset( Folder / "m1.mv1", 120 );
		R = NModelExport::Export( Fake, Folder / "m1.pmx" );
		C.Check( R.bSkippedUpToDate && Fake.SavedModels.size() == 3, "and left alone once the output is newer again" );

		// メッシュのないファイルは、アニメーションだけの .mv1 にする
		WriteFileText( Folder / "motion.vmd", "x" );
		Fake.NextModelData = { { "Walk" }, { 30.0f } };
		Fake.NextModelData.MeshCount = 0;
		R = NModelExport::Export( Fake, Folder / "motion.vmd" );
		C.Check( R.bSuccess && R.NumMeshes == 0 && R.SavedType == EModelSaveType::AnimOnly && Fake.SavedModels.back().Options.Type == EModelSaveType::AnimOnly, "a file without a mesh is saved as animations only" );
		C.Check( R.GetSummary().Contains( "animations only" ), "and the summary says so" );

		// 何も入っていない・指定が合わない
		WriteFileText( Folder / "empty.pmx", "x" );
		Fake.NextModelData = { {}, {} };
		Fake.NextModelData.MeshCount = 0;
		const size_t NumSaved = Fake.SavedModels.size();
		R = NModelExport::Export( Fake, Folder / "empty.pmx" );
		C.Check( !R.bSuccess && R.Error.Contains( "neither" ) && Fake.SavedModels.size() == NumSaved && !NFileSystem::FileExists( Folder / "empty.mv1" ) && Fake.Models.empty(),
				 "a file with neither a mesh nor an animation fails, saves nothing, and releases the handle (%s)", *R.Error );

		Fake.NextModelData = { { "Walk" }, { 30.0f } };
		Fake.NextModelData.MeshCount = 0;
		FModelExportOptions MeshOnly;
		MeshOnly.bAutoSaveType = false;
		MeshOnly.SaveType = EModelSaveType::MeshOnly;
		MeshOnly.bForce = true;
		R = NModelExport::Export( Fake, Folder / "motion.vmd", MeshOnly );
		C.Check( !R.bSuccess && R.Error.Contains( "no mesh" ), "asking for the mesh of a file without one fails (%s)", *R.Error );
		Fake.NextModelData = { {}, {} };
		FModelExportOptions AnimOnly;
		AnimOnly.bAutoSaveType = false;
		AnimOnly.SaveType = EModelSaveType::AnimOnly;
		AnimOnly.bForce = true;
		R = NModelExport::Export( Fake, Folder / "m1.pmx", AnimOnly );
		C.Check( !R.bSuccess && R.Error.Contains( "no animations" ), "asking for the animations of a file without any fails (%s)", *R.Error );
		C.Check( Fake.Models.empty(), "failures release the handle" );

		// 出力先・保存の指定
		Fake.NextModelData = { { "Idle" }, { 30.0f } };
		FModelExportOptions Custom;
		Custom.OutputPath = Folder / "sub/dir/out.mv1";
		Custom.bAutoSaveType = false;
		Custom.SaveType = EModelSaveType::MeshOnly;
		Custom.bCompress = false;
		Custom.bForce = true;
		R = NModelExport::Export( Fake, Folder / "m1.pmx", Custom );
		C.Check( R.bSuccess && Ends( R.OutputPath, "sub/dir/out.mv1" ) && NFileSystem::FileExists( Folder / "sub/dir/out.mv1" ), "the output path is honoured and its folders are created (%s)", *R.Error );
		C.Check( Fake.SavedModels.back().Options.Type == EModelSaveType::MeshOnly && !Fake.SavedModels.back().Options.bCompress, "the save type and the compression setting reach the backend" );

		// 失敗
		C.Check( !NModelExport::Export( Fake, Folder / "missing.pmx" ).bSuccess && NModelExport::Export( Fake, Folder / "missing.pmx" ).Error.Contains( "does not exist" ), "a missing source fails" );
		C.Check( NModelExport::Export( Fake, Folder / "m1.mv1" ).Error.Contains( "already" ), "a .mv1 source is refused" );
		WriteFileText( Folder / "pic.png", "x" );
		C.Check( NModelExport::Export( Fake, Folder / "pic.png" ).Error.Contains( "not supported" ), "an unsupported format is refused" );
		Fake.bSaveFails = true;
		Force.OutputPath = Folder / "fails.mv1";
		R = NModelExport::Export( Fake, Folder / "m1.pmx", Force );
		C.Check( !R.bSuccess && R.Error.Contains( "could not save" ) && Fake.Models.empty(), "a failing backend is reported and the handle released" );
		Fake.bSaveFails = false;
		Fake.SaveWarning = "unused animations";
		Force.OutputPath = Folder / "warn.mv1";
		R = NModelExport::Export( Fake, Folder / "m1.pmx", Force );
		C.Check( R.bSuccess && R.Warning.Contains( "unused animations" ), "the backend's warning is passed on" );
		Fake.SaveWarning.Empty();

		// FBX: 取り込んで .x にして読み込み、保存する
		const FString FbxFolder = Folder / "fbx";
		CreateDirectoryA( *FbxFolder, nullptr );
		WriteFbx( FbxFolder / "bar.fbx", FFbxSpec() );
		WriteFileText( FbxFolder / "sheet.bmp", "x" );
		Fake.NextModelData = { { "Wave" }, { 30.0f } };
		Fake.bCaptureLoadedFiles = true;
		Fake.LoadedFileContents.clear();
		FModelExportOptions Default;
		R = NModelExport::Export( Fake, FbxFolder / "bar.fbx", Default );
		C.Check( R.bSuccess && R.bImported && Ends( R.OutputPath, "fbx/bar.mv1" ) && R.Warning.IsEmpty(), "an FBX is imported and exported (%s / %s)", *R.Error, *R.Warning );
		const FString TemporaryX = Fake.LoadedModelPaths.empty() ? FString() : Fake.LoadedModelPaths.back();
		C.Check( Ends( TemporaryX, "bar.import.tmp.x" ) && !NFileSystem::FileExists( TemporaryX ), "DxLib was given a temporary .x, which is deleted afterwards" );
		const std::string XText = Fake.LoadedFileContents.count( *TemporaryX ) > 0 ? Fake.LoadedFileContents[*TemporaryX] : std::string();
		const FString X( XText.c_str(), static_cast<int32>( XText.size() ) );
		C.Check( X.StartsWith( "xof 0303txt 0032" ) && X.Contains( "Frame Bar {" ) && X.Contains( "Frame Bone1 {" ) && X.Contains( "Frame Bone2 {" ) && X.Contains( "AnimationSet Wave {" ) && X.Contains( "\"Bone1\";" ) && X.Contains( "\"Bone2\";" ),
				 "the temporary .x holds the frames, the skin and the animation" );
		C.Check( ReadIntAfter( XText, "Mesh {" ) == 40 && X.Contains( "TextureFilename { \"sheet.bmp\"; }" ), "the mesh has 40 vertices and the texture path is relative to the output" );
		C.Check( !X.Contains( "Frame Root" ), "the scene's root is not written" );
		// 左手系: 最後の頂点のあとの値で z が反転していること（元の z は -0.5 .. 1.5）
		C.Check( X.Contains( ";0.5;" ) || X.Contains( ";-0.5;" ), "mirrored coordinates are written" );

		// 画像が無ければ警告。出力先が別のフォルダなら、画像のパスは相対
		DeleteFileA( *( FbxFolder / "sheet.bmp" ) );
		Default.bForce = true;
		R = NModelExport::Export( Fake, FbxFolder / "bar.fbx", Default );
		C.Check( R.bSuccess && R.Warning.Contains( "was not found" ), "a missing texture is a warning, not a failure" );
		WriteFileText( FbxFolder / "sheet.bmp", "x" );
		Default.OutputPath = Folder / "outdir/bar.mv1";
		R = NModelExport::Export( Fake, FbxFolder / "bar.fbx", Default );
		const FString Moved = Fake.LoadedModelPaths.back();
		const std::string MovedText = Fake.LoadedFileContents[*Moved];
		C.Check( R.bSuccess && Ends( Moved, "outdir/bar.import.tmp.x" ) && FString( MovedText.c_str(), static_cast<int32>( MovedText.size() ) ).Contains( "TextureFilename { \"../fbx/sheet.bmp\"; }" ),
				 "with another output folder the texture path is made relative to it (%s)", *R.Error );

		// 壊れた FBX
		WriteFileText( FbxFolder / "broken.fbx", "this is not a model" );
		R = NModelExport::Export( Fake, FbxFolder / "broken.fbx" );
		C.Check( !R.bSuccess && R.Error.Contains( "could not read" ) && !NFileSystem::FileExists( FbxFolder / "broken.mv1" ) && Fake.Models.empty(), "a broken FBX fails with a reason and leaves nothing (%s)", *R.Error );

		// OBJ とマテリアルファイル
		WriteFileText( FbxFolder / "box.obj", MakeBoxObj( "box.mtl" ).c_str() );
		WriteFileText( FbxFolder / "box.mtl", "newmtl BoxMaterial\nKd 0.25 0.5 0.75\nmap_Kd sheet.bmp\n" );
		R = NModelExport::Export( Fake, FbxFolder / "box.obj" );
		const std::string ObjX = Fake.LoadedFileContents[*Fake.LoadedModelPaths.back()];
		C.Check( R.bSuccess && R.bImported && FString( ObjX.c_str(), static_cast<int32>( ObjX.size() ) ).Contains( "TextureFilename { \"sheet.bmp\"; }" ), "an OBJ with its .mtl is exported, with the texture from the material (%s)", *R.Error );

		// メッシュのない FBX（動きだけ）は、アニメーションだけの .mv1
		FFbxSpec MotionOnly;
		MotionOnly.bMesh = false;
		WriteFbx( FbxFolder / "wave.fbx", MotionOnly );
		Fake.NextModelData = { { "Wave" }, { 30.0f } };
		Fake.NextModelData.MeshCount = 0;
		R = NModelExport::Export( Fake, FbxFolder / "wave.fbx" );
		C.Check( R.bSuccess && R.SavedType == EModelSaveType::AnimOnly, "an FBX with only a skeleton and an animation becomes an animations-only .mv1" );
		Fake.bCaptureLoadedFiles = false;
		Fake.NextModelData = FFakeAssetBackend::FModelData();
	}

	void TestImportedModelsInManager( FChecker& C, CAssetManager& Assets, FFakeAssetBackend& Fake, const FString& Root )
	{
		C.Group = "model-manager";
		Fake.SavedModels.clear();
		Fake.LoadedModelPaths.clear();
		WriteFbx( Root / "a.fbx", FFbxSpec() );
		WriteFileText( Root / "sheet.bmp", "x" );
		Fake.NextModelData = { { "Wave" }, { 30.0f } };

		const auto ReleaseAll = [&]()
		{
			Assets.Tick( 0.0f );
			Assets.ReleaseUnused();
		};

		// 初めての LoadModel: .mv1 を作って、それを読む
		TSharedPtr<CModel> Model = Assets.LoadModel( "a.fbx" );
		C.Check( Model.IsValid() && Ends( Model->GetPath(), "a.fbx" ), "an FBX can be loaded through LoadModel" );
		C.Check( Fake.SavedModels.size() == 1 && Fake.LoadedModelPaths.size() == 2 && Ends( Fake.LoadedModelPaths[0], "a.import.tmp.x" ) && Ends( Fake.LoadedModelPaths[1], "a.mv1" ),
				 "it was converted (a temporary .x was read and saved) and then the .mv1 was loaded (%d saved, %d loaded)", static_cast<int32>( Fake.SavedModels.size() ), static_cast<int32>( Fake.LoadedModelPaths.size() ) );
		C.Check( NFileSystem::FileExists( Root / "a.mv1" ) && Model.IsValid() && Model->GetFileBytes() == 7, "the .mv1 exists next to the source, and the model's file size is the .mv1's" );
		C.Check( Assets.LoadModel( "a.fbx" ) == Model, "the same FBX is shared" );

		// 2 回目: 最新の .mv1 をそのまま読む
		Model.Reset();
		ReleaseAll();
		const size_t NumLoaded = Fake.LoadedModelPaths.size();
		Model = Assets.LoadModel( "a.fbx" );
		C.Check( Model.IsValid() && Fake.SavedModels.size() == 1 && Fake.LoadedModelPaths.size() == NumLoaded + 1 && Ends( Fake.LoadedModelPaths.back(), "a.mv1" ), "the second load uses the up-to-date .mv1 without converting" );

		// 元が新しくなったら、作り直す
		Model.Reset();
		ReleaseAll();
		SetLastWriteOffset( Root / "a.fbx", 60 );
		Model = Assets.LoadModel( "a.fbx" );
		C.Check( Model.IsValid() && Fake.SavedModels.size() == 2, "a newer source is converted again" );

		// 自動の取り込みを切ると、変換しない
		Model.Reset();
		ReleaseAll();
		DeleteFileA( *( Root / "a.mv1" ) );
		Assets.SetAutoImportModels( false );
		C.Check( !Assets.GetAutoImportModels(), "automatic import can be turned off" );
		C.Check( Assets.LoadModel( "a.fbx" ) == nullptr && Fake.SavedModels.size() == 2, "without a .mv1 and without automatic import, loading fails and nothing is converted" );
		C.Check( Assets.ExportModelAsMV1( "a.fbx" ).bSuccess && Fake.SavedModels.size() == 3, "ExportModelAsMV1 still converts on request" );
		SetLastWriteOffset( Root / "a.fbx", 300 );   // .mv1 が古くなる
		Model = Assets.LoadModel( "a.fbx" );
		C.Check( Model.IsValid() && Ends( Fake.LoadedModelPaths.back(), "a.mv1" ) && Fake.SavedModels.size() == 3, "a stale .mv1 is used (with a warning) when automatic import is off" );
		Model.Reset();
		ReleaseAll();
		Assets.SetAutoImportModels( true );

		// 元のファイルが無くて .mv1 だけ（配布版）
		const FString Hidden = Root / "a_source.fbx";
		MoveFileA( *( Root / "a.fbx" ), *Hidden );
		Model = Assets.LoadModel( "a.fbx" );
		C.Check( Model.IsValid() && Ends( Model->GetPath(), "a.mv1" ), "without the source, the .mv1 is loaded" );
		Model.Reset();
		ReleaseAll();
		MoveFileA( *Hidden, *( Root / "a.fbx" ) );

		// 変換できない FBX は、DxLib にそのまま読ませてみる
		WriteFileText( Root / "bad.fbx", "garbage" );
		Model = Assets.LoadModel( "bad.fbx" );
		C.Check( Model.IsValid() && Ends( Fake.LoadedModelPaths.back(), "bad.fbx" ) && !NFileSystem::FileExists( Root / "bad.mv1" ), "an FBX that cannot be converted is handed to DxLib as it is" );
		Model.Reset();
		ReleaseAll();

		// DxLib が読める形式: 最新の .mv1 が隣にあればそちらを読む
		WriteFileText( Root / "n.x", "x" );
		WriteFileText( Root / "n.mv1", "x" );
		SetLastWriteOffset( Root / "n.mv1", 60 );
		Model = Assets.LoadModel( "n.x" );
		C.Check( Model.IsValid() && Ends( Fake.LoadedModelPaths.back(), "n.mv1" ), "a DxLib format loads its fresh .mv1 instead" );
		Model.Reset();
		ReleaseAll();
		Assets.SetPreferConvertedModels( false );
		Model = Assets.LoadModel( "n.x" );
		C.Check( Model.IsValid() && Ends( Fake.LoadedModelPaths.back(), "n.x" ) && !Assets.GetPreferConvertedModels(), "and loads the source itself when that preference is off" );
		Model.Reset();
		ReleaseAll();
		Assets.SetPreferConvertedModels( true );
		WriteFileText( Root / "n2.x", "x" );
		const size_t SavedBefore = Fake.SavedModels.size();
		Model = Assets.LoadModel( "n2.x" );
		C.Check( Model.IsValid() && Ends( Fake.LoadedModelPaths.back(), "n2.x" ) && Fake.SavedModels.size() == SavedBefore && !NFileSystem::FileExists( Root / "n2.mv1" ), "a DxLib format without a .mv1 is loaded directly and nothing is written" );
		Model.Reset();
		ReleaseAll();

		// 一覧
		Assets.ExportModelAsMV1( "a.fbx", [] { FModelExportOptions O; O.bForce = true; return O; }() );
		SetLastWriteOffset( Root / "a.mv1", 600 );
		TArray<FModelSourceInfo> Sources = Assets.ScanModelSources();
		const auto Find = [&]( const char* Path ) -> const FModelSourceInfo*
		{
			for ( const FModelSourceInfo& Info : Sources )
			{
				if ( Info.Path == Path ) return &Info;
			}
			return nullptr;
		};
		const FModelSourceInfo* InfoA = Find( "a.fbx" );
		const FModelSourceInfo* InfoBad = Find( "bad.fbx" );
		const FModelSourceInfo* InfoN = Find( "n.x" );
		const FModelSourceInfo* InfoN2 = Find( "n2.x" );
		C.Check( InfoA != nullptr && InfoBad != nullptr && InfoN != nullptr && InfoN2 != nullptr && Find( "n.mv1" ) == nullptr && Find( "sheet.bmp" ) == nullptr, "the scan lists the convertible sources (not .mv1 or other files)" );
		C.Check( InfoA != nullptr && InfoA->Status == EModelExportStatus::UpToDate && InfoA->Kind == EModelFileKind::Imported && InfoA->OutputPath == "a.mv1" && InfoA->OutputBytes == 7 && InfoA->SourceBytes > 100, "a.fbx is up to date" );
		C.Check( InfoBad != nullptr && InfoBad->Status == EModelExportStatus::Missing && InfoBad->OutputBytes == 0, "bad.fbx has no .mv1" );
		C.Check( InfoN != nullptr && InfoN->Status == EModelExportStatus::UpToDate && InfoN->Kind == EModelFileKind::DxLibNative && InfoN2 != nullptr && InfoN2->Status == EModelExportStatus::Missing, "n.x is up to date, n2.x is missing" );
		SetLastWriteOffset( Root / "n.x", 900 );
		Sources = Assets.ScanModelSources();
		C.Check( Find( "n.x" ) != nullptr && Find( "n.x" )->Status == EModelExportStatus::Stale, "a source newer than its .mv1 is stale" );
		SetLastWriteOffset( Root / "n.x", 0 );

		// まとめて出力: 無い・古いものだけ
		const FModelBatchExportResult Batch = Assets.ExportAllModels();
		C.Check( Batch.NumExported == 1 && Batch.NumFailed == 1 && Batch.NumSkipped == 0 && Batch.Items.Num() == 2, "ExportAllModels converts what is missing (n2.x) and reports what fails (bad.fbx): %s", *Batch.GetSummary() );
		C.Check( NFileSystem::FileExists( Root / "n2.mv1" ), "n2.mv1 was written" );
		const FModelBatchExportResult Forced = Assets.ExportAllModels( true );
		C.Check( Forced.NumExported == 3 && Forced.NumFailed == 1, "with bForce everything is converted again: %s", *Forced.GetSummary() );

		// 定義が使っているモデルだけ
		SetLastWriteOffset( Root / "a.mv1", 600 );
		SetLastWriteOffset( Root / "n.mv1", 600 );
		SetLastWriteOffset( Root / "n2.mv1", 600 );
		FModelAnimationDefinition Definition;
		Definition.ModelPath = "a.fbx";
		Definition.PreviewModelPath = "a.fbx";
		for ( const char* Source : { "n.x", "d.mv1", "", "n2.x", "n.x" } )
		{
			FModelClipDef Clip;
			Clip.Name = FString::Printf( "Clip%d", Definition.Clips.Num() );
			Clip.SourcePath = Source;
			Definition.Clips.Add( Clip );
		}
		FModelBatchExportResult Used = Assets.ExportModelsUsedBy( Definition );
		C.Check( Used.Items.Num() == 3 && Used.NumSkipped == 3 && Used.NumExported == 0, "ExportModelsUsedBy looks at each distinct convertible file once (.mv1 and empty are ignored): %s", *Used.GetSummary() );
		Used = Assets.ExportModelsUsedBy( Definition, true );
		C.Check( Used.NumExported == 3, "and exports them all with bForce: %s", *Used.GetSummary() );

		// 失敗・出力先
		const FModelExportResult Missing = Assets.ExportModelAsMV1( "nope.fbx" );
		C.Check( !Missing.bSuccess && Missing.Error.Contains( "not found" ), "a missing file is reported" );
		FModelExportOptions Out;
		Out.OutputPath = "out/n_copy.mv1";
		Out.bForce = true;
		const FModelExportResult Copy = Assets.ExportModelAsMV1( "n.x", Out );
		C.Check( Copy.bSuccess && NFileSystem::FileExists( Root / "out/n_copy.mv1" ), "a relative output path is placed under the root folder" );
	}

	void TestAnimationOnlyModels( FChecker& C, CAssetManager& Assets, FFakeAssetBackend& Fake, const FString& Root )
	{
		C.Group = "animation-only";
		WriteFileText( Root / "hero.mv1", "x" );
		WriteFileText( Root / "motion.mv1", "x" );

		Fake.NextModelData = { { "Idle" }, { 30.0f } };
		Fake.NextModelData.FrameNames = { "Hips", "Spine", "Head" };
		TSharedPtr<CModel> Hero = Assets.LoadModel( "hero.mv1" );
		Fake.NextModelData = { { "Walk", "Run" }, { 30.0f, 20.0f } };
		Fake.NextModelData.MeshCount = 0;
		Fake.NextModelData.FrameNames = { "Hips", "Spine", "Arm" };
		TSharedPtr<CModel> Motion = Assets.LoadModel( "motion.mv1" );
		Fake.NextModelData = FFakeAssetBackend::FModelData();
		if ( !Hero.IsValid() || !Motion.IsValid() )
		{
			C.Check( false, "the models load" );
			return;
		}

		C.Check( Hero->HasMesh() && Hero->GetMeshCount() == 1 && !Hero->IsAnimationOnly() && Hero->Describe().Contains( "mesh(es)" ), "a model with a mesh" );
		C.Check( !Motion->HasMesh() && Motion->GetMeshCount() == 0 && Motion->IsAnimationOnly() && Motion->Describe().StartsWith( "animation only" ) && Motion->Describe().Contains( "3 bone(s)" ),
				 "a file without a mesh is animation only (%s)", *Motion->Describe() );
		C.Check( Motion->GetBoneNames().Num() == 3 && Motion->GetBoneNames()[2] == "Arm" && Hero->GetBoneNames()[0] == "Hips", "bone names are listed" );

		const size_t NumModels = Fake.Models.size();
		C.Check( Motion->CreateInstance() == nullptr && Fake.Models.size() == NumModels, "an instance of an animation-only file cannot be created (and no handle is made)" );
		C.Check( Hero->CreateInstance().IsValid(), "a model with a mesh can" );

		const FBoneMatch ToHero = Hero->CompareBones( *Motion );
		C.Check( ToHero.NumAnimBones == 3 && ToHero.NumMatched == 2 && ToHero.Missing.Num() == 1 && ToHero.Missing[0] == "Arm" && !ToHero.IsPoor(), "the motion's bones are compared with the model's (Arm is missing)" );

		// モデルを持たない集（モーション専用）
		FModelAnimationDefinition Pack;
		Pack.UnitsPerSecond = 30.0f;
		FModelClipDef Walk, Run, Idle;
		Walk.Name = "Walk";
		Walk.SourcePath = "motion.mv1";
		Walk.AnimName = "Walk";
		Run.Name = "Run";
		Run.SourcePath = "motion.mv1";
		Run.AnimIndex = 1;
		Idle.Name = "Idle";   // ファイルなし: モデルを持たない集では、キャラクターに適用するときだけ使える
		Pack.Clips = { Walk, Run, Idle };
		TSharedPtr<CModelAnimationAsset> Asset = Assets.CreateModelAnimations( Pack, "pack" );
		C.Check( Asset.IsValid() && !Asset->GetModel().IsValid() && Asset->IsReady() && Asset->GetClipSource( 0 ) == Motion, "a set without a model is created; its clips use their own files" );
		if ( !Asset.IsValid() ) return;

		FModelAnimator Bare = Asset->CreateAnimator();
		C.Check( !Bare.Instance.IsValid() && Bare.HasClip( "Walk" ) && Bare.HasClip( "Run" ) && !Bare.HasClip( "Idle" ), "its own animator has the clips but no model (and skips the clip without a file)" );
		C.Near( Bare.GetClipLength( "Walk" ), 1.0, 1.0e-4, "Walk is 30 units at 30 per second" );
		C.Near( Bare.GetClipLength( "Run" ), 20.0 / 30.0, 1.0e-4, "Run is 20 units" );

		const int32 NumAttach = Fake.NumAttachCalls;
		FModelAnimator ForHero = Asset->CreateAnimatorFor( Hero );
		C.Check( ForHero.Instance.IsValid() && ForHero.HasClip( "Walk" ) && ForHero.HasClip( "Idle" ), "applied to a character, the clip without a file uses the character itself" );
		C.Near( ForHero.GetClipLength( "Idle" ), 1.0, 1.0e-4, "Idle comes from the character (30 units)" );
		ForHero.Play( "Walk", 0.0f );
		ForHero.Update( 0.1f );
		const std::vector<FDrawModelAnimData> Layers = CollectModelLayers( ForHero );
		C.Check( Layers.size() == 1 && Fake.NumAttachCalls > NumAttach, "playing a motion from the animation-only file attaches it to the character's instance" );
		C.Check( Asset->CreateAnimatorFor( nullptr ).Instance == nullptr, "no character gives an animator without a model" );

		// ファイルへの保存・読み込み
		Pack.PreviewModelPath = "hero.mv1";
		C.Check( Assets.SaveModelAnimationDefinition( "pack.modelanim.json", Pack ), "a set without a model can be saved" );
		const NlohmannJson Json = Pack.ToJson();
		C.Check( !Json.contains( "Model" ) && Json["PreviewModel"] == "hero.mv1", "the JSON has no \"Model\", but keeps the preview model" );
		FModelAnimationDefinition Back;
		C.Check( FModelAnimationDefinition::FromJson( NlohmannJson::parse( Json.dump() ), Back ) && Back.ModelPath.IsEmpty() && Back.PreviewModelPath == "hero.mv1" && Back.Clips.Num() == 3, "and it reads back" );
		TSharedPtr<CModelAnimationAsset> Loaded = Assets.LoadModelAnimations( "pack.modelanim.json" );
		C.Check( Loaded.IsValid() && !Loaded->GetModel().IsValid() && Loaded->GetDefinition().Clips.Num() == 3, "a definition file without a model loads" );

		// 画面に出せないファイルを Model にした定義
		FModelAnimationDefinition Wrong;
		Wrong.ModelPath = "motion.mv1";
		Wrong.Clips = { Walk };
		TSharedPtr<CModelAnimationAsset> WrongAsset = Assets.CreateModelAnimations( Wrong, "wrong" );
		C.Check( WrongAsset.IsValid(), "an animation-only file as the model still creates the asset (with a warning)" );
		C.Check( WrongAsset.IsValid() && !WrongAsset->CreateAnimator().Instance.IsValid() && WrongAsset->CreateAnimator().HasClip( "Walk" ), "but it has no model to draw; the clips still work" );

		// 編集
		TModelAnimationDocument Doc;
		Doc.Reset( Pack );
		NModelAnimationEdit::SetPreviewModelPath( Doc, "other.mv1" );
		C.Check( Doc.Get().PreviewModelPath == "other.mv1" && Doc.IsDirty(), "the preview model can be changed" );
		Doc.Undo();
		C.Check( Doc.Get().PreviewModelPath == "hero.mv1", "and undone" );
		FModelClipDef Added;
		Added.Name = "Added";
		const int32 NumBefore = Doc.Get().Clips.Num();
		const int32 First = NModelAnimationEdit::AddClips( Doc, TArray<FModelClipDef> { Added, Added, Walk } );
		C.Check( First == NumBefore && Doc.Get().Clips.Num() == NumBefore + 3, "several clips are added at once" );
		TArray<FString> Names;
		for ( const FModelClipDef& Clip : Doc.Get().Clips ) Names.AddUnique( Clip.Name );
		C.Check( Names.Num() == Doc.Get().Clips.Num(), "and their names are made unique" );
		Doc.Undo();
		C.Check( Doc.Get().Clips.Num() == NumBefore, "one undo removes all of them" );
		C.Check( Doc.CanRedo(), "after the undo there is something to redo" );
		C.Check( NModelAnimationEdit::AddClips( Doc, TArray<FModelClipDef>() ) == INDEX_NONE && Doc.CanRedo(), "adding nothing does nothing (the history is untouched)" );
	}
}

void NAssetTest::RunImportLogicTests( FChecker& C, CAssetManager& Assets, FFakeAssetBackend& Fake, const FString& Dir )
{
	RunImportPureTests( C );
	TestRelativePath( C );
	TestModelExport( C, Fake, Dir );

	const FString Root = Dir / "imp";
	CreateDirectoryA( *Root, nullptr );
	Assets.SetRootDirectory( Root );
	TestImportedModelsInManager( C, Assets, Fake, Root );
	TestAnimationOnlyModels( C, Assets, Fake, Root );
	Assets.SetAutoImportModels( true );
	Assets.SetPreferConvertedModels( true );
	Assets.SetRootDirectory( Dir );
}
