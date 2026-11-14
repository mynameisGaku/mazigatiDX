// SPDX-License-Identifier: MIT
#include "StaticModelBuilder.h"
#include "Library/Core/Memory/Memory.h"
#include "Library/Core/Types/Numeric.h"

namespace
{
	// GPUへ渡す前に数値を検査する。
	bool Finite( const FVector3& Value )
	{
		return NMath::IsFinite( Value.x ) && NMath::IsFinite( Value.y ) && NMath::IsFinite( Value.z );
	}
	// 範囲を保ってRGBA8へ変換する。
	uint8 ColorByte( float32 Value )
	{
		return static_cast<uint8>( NMath::Clamp( Value, 0.0f, 1.0f ) * 255 + 0.5f );
	}
}

bool FStaticModelBuilder::Build( const FImportedScene& Scene, FStaticModelDefinition& Out, FString& Error )
{
	if ( Scene.Nodes.Num() > 65536 || Scene.Meshes.IsEmpty() || Scene.Meshes.Num() > 4096 || Scene.Materials.Num() > 4096 )
	{
		Error = "Unsupported animated scene or invalid static scene size";
		return false;
	}
	for ( const auto& Animation : Scene.Animations )
	{
		if ( !Animation.Tracks.IsEmpty() )
		{
			Error = "Animated frame tracks are not supported by static models";
			return false;
		}
	}
	TArray<FMatrix4x4> Local, Global; // すべての親子関係を先に解決する。
	Local.SetNum( Scene.Nodes.Num() );
	Global.SetNum( Scene.Nodes.Num() );
	TArray<uint8> Status; // 0は未訪問、1は現在の鎖、2は解決済み。
	Status.SetNum( Scene.Nodes.Num() );
	for ( int32 Index = 0; Index < Scene.Nodes.Num(); ++Index )
	{
		const auto& Node = Scene.Nodes[Index]; // 親の番号は配列順を要求しない。
		if ( Node.Parent < -1 || Node.Parent >= Scene.Nodes.Num() )
		{
			Error = "Invalid parent frame";
			return false;
		}
		FMemory::Memcpy( Local[Index].m, Node.Local.M, sizeof( Node.Local.M ) );
		for ( float32 Value : Node.Local.M )
		{
			if ( !NMath::IsFinite( Value ) )
			{
				Error = "Non-finite frame transform";
				return false;
			}
		}
		if ( Local[Index].m[0][3] != 0 || Local[Index].m[1][3] != 0 || Local[Index].m[2][3] != 0 || Local[Index].m[3][3] != 1 )
		{
			Error = "Static frames require affine transforms";
			return false;
		}
	}
	for ( int32 Index = 0; Index < Scene.Nodes.Num(); ++Index )
	{
		TArray<int32> Chain; // 再帰を使わず、親を先に解決する。
		int32 Current = Index; // 現在調べている親。
		while ( Current >= 0 && Status[Current] != 2 )
		{
			if ( Status[Current] == 1 )
			{
				Error = "Cyclic frame hierarchy";
				return false;
			}
			Status[Current] = 1;
			Chain.Add( Current );
			Current = Scene.Nodes[Current].Parent;
		}
		while ( !Chain.IsEmpty() )
		{
			const int32 Frame = Chain.Pop(); // 親から順に確定する。
			const int32 Parent = Scene.Nodes[Frame].Parent; // 解決済みの親。
			Global[Frame] = Parent < 0 ? Local[Frame] : Local[Frame] * Global[Parent];
			Status[Frame] = 2;
		}
	}
	FStaticModelDefinition Result; // 成功するまで呼び出し側の値を変えない。
	Result.Nodes = Scene.Nodes;
	Result.NumMeshes = Scene.Meshes.Num();
	Result.Structure.Frames.SetNum( Scene.Nodes.Num() );
	for ( int32 Index = 0; Index < Scene.Nodes.Num(); ++Index )
	{
		auto& Frame = Result.Structure.Frames[Index]; // 元のフレーム番号を保つ。
		Frame.Name = Scene.Nodes[Index].Name;
		Frame.Parent = Scene.Nodes[Index].Parent;
		if ( Frame.Parent >= 0 )
		{
			Result.Structure.Frames[Frame.Parent].Children.Add( Index );
		}
	}
	Result.Structure.Meshes.SetNum( Scene.Meshes.Num() );
	Result.Structure.Materials.SetNum( Scene.Materials.Num() + 1 );
	Result.Structure.Materials.Last().Name = "Default";
	Result.Structure.Textures.SetNum( Scene.Textures.Num() );
	for ( int32 Index = 0; Index < Scene.Textures.Num(); ++Index )
	{
		Result.Structure.Textures[Index].Name = Scene.Textures[Index].File;
		Result.Structure.Textures[Index].ColorFilePath = Scene.Textures[Index].File;
	}
	Result.Surface.Materials.SetNum( Scene.Materials.Num() + 1 ); // 最後は材質未指定時の白。
	for ( int32 Material = 0; Material < Scene.Materials.Num(); ++Material )
	{
		const auto& Input = Scene.Materials[Material]; // 元の材質メタデータ。
		auto& Info = Result.Structure.Materials[Material]; // 値を変えずに写す。
		Info.Name = Input.Name;
		Info.DiffuseTexture = Input.Texture;
		Info.Colors.Diffuse = FColor( Input.Diffuse[0], Input.Diffuse[1], Input.Diffuse[2], Input.Diffuse[3] );
		Info.Colors.Specular = FColor( Input.Specular[0], Input.Specular[1], Input.Specular[2], 1 );
		Info.Colors.Emissive = FColor( Input.Emissive[0], Input.Emissive[1], Input.Emissive[2], 1 );
		Info.Colors.SpecularPower = Input.Power;
		if ( !NMath::IsFinite( Input.Power ) || Input.Texture < -1 || Input.Texture >= Scene.Textures.Num() )
		{
			Error = "Invalid static material metadata";
			return false;
		}
		for ( int32 Channel = 0; Channel < 3; ++Channel )
		{
			if ( !NMath::IsFinite( Input.Specular[Channel] ) || !NMath::IsFinite( Input.Emissive[Channel] ) )
			{
				Error = "Non-finite static material metadata";
				return false;
			}
		}
		for ( int32 Channel = 0; Channel < 4; ++Channel )
		{
			const float32 Value = Scene.Materials[Material].Diffuse[Channel]; // 既定の拡散色。
			if ( !NMath::IsFinite( Value ) )
			{
				Error = "Non-finite material color";
				return false;
			}
			Result.Surface.Materials[Material].Diffuse[Channel] = Value;
		}
	}
	bool bHasBounds = false; // 最初の頂点を外接箱の基準にする。
	for ( int32 MeshIndex = 0; MeshIndex < Scene.Meshes.Num(); ++MeshIndex )
	{
		const auto& Mesh = Scene.Meshes[MeshIndex]; // 元の三角形順を保つ。
		if ( Mesh.IsSkinned() || Mesh.bHasUnsupportedSkinning || Mesh.NumDiscardedSkinInfluences > 0 || Mesh.bHasBlendShapes || Mesh.bHasVertexCache || Mesh.Node < -1 || Mesh.Node >= Global.Num() || Mesh.Positions.IsEmpty() || Mesh.Normals.Num() != Mesh.Positions.Num()
			|| ( !Mesh.UVs.IsEmpty() && Mesh.UVs.Num() != Mesh.Positions.Num() ) || Mesh.Indices.IsEmpty() || Mesh.Indices.Num() % 3 != 0
			|| ( !Mesh.TriangleMaterials.IsEmpty() && Mesh.TriangleMaterials.Num() != Mesh.Indices.Num() / 3 )
			|| static_cast<int64>( Result.Surface.Vertices.Num() ) + Mesh.Positions.Num() > 262144 || static_cast<int64>( Result.Surface.Indices.Num() ) + Mesh.Indices.Num() > 786432 )
		{
			Error = "Unsupported skin or invalid static mesh";
			return false;
		}
		auto& MeshInfo = Result.Structure.Meshes[MeshIndex]; // 元のメッシュ番号を保つ。
		MeshInfo.Frame = Mesh.Node;
		MeshInfo.VertexCount = Mesh.Indices.Num(); // 材質色を確定して三角形順に展開する。
		MeshInfo.TriangleCount = Mesh.Indices.Num() / 3;
		if ( Mesh.Node >= 0 )
		{
			Result.Structure.Frames[Mesh.Node].Meshes.Add( MeshIndex );
		}
		const FMatrix4x4 Transform = Mesh.Node < 0 ? FMatrix4x4() : Global[Mesh.Node]; // メッシュのフレームを適用する。
		FMatrix4x4 Inverse; // 非一様な拡大でも法線を逆転置で変換する。
		if ( !Transform.TryInverse( Inverse ) )
		{
			Error = "Singular static mesh transform";
			return false;
		}
		const int32 BaseVertex = Result.Surface.Vertices.Num(); // 全体の頂点列での開始。
		for ( int32 Vertex = 0; Vertex < Mesh.Positions.Num(); ++Vertex )
		{
			const FVector3 Position = Transform.TransformPoint( Mesh.Positions[Vertex] ); // モデル座標の位置。
			const auto& InputNormal = Mesh.Normals[Vertex]; // フレーム座標の法線。
			FVector3 Normal( InputNormal.x * Inverse.m[0][0] + InputNormal.y * Inverse.m[0][1] + InputNormal.z * Inverse.m[0][2],
				InputNormal.x * Inverse.m[1][0] + InputNormal.y * Inverse.m[1][1] + InputNormal.z * Inverse.m[1][2],
				InputNormal.x * Inverse.m[2][0] + InputNormal.y * Inverse.m[2][1] + InputNormal.z * Inverse.m[2][2] ); // 行ベクトルでの逆転置。
			const FVector2 UV = Mesh.UVs.IsEmpty() ? FVector2() : Mesh.UVs[Vertex]; // 未指定は原点。
			if ( !Finite( Position ) || !Finite( Normal ) || !NMath::IsFinite( UV.x ) || !NMath::IsFinite( UV.y ) )
			{
				Error = "Non-finite static vertex";
				return false;
			}
			Normal = Normal.Normalized();
			FMeshVertex Value; // 色は三角形の材質ごとに後で確定する。
			Value.Position[0] = Position.x;
			Value.Position[1] = Position.y;
			Value.Position[2] = Position.z;
			Value.Normal[0] = Normal.x;
			Value.Normal[1] = Normal.y;
			Value.Normal[2] = Normal.z;
			Value.U = UV.x;
			Value.V = UV.y;
			Result.Surface.Vertices.Add( Value );
			MeshInfo.BoundsMin = Vertex == 0 ? Position : FVector3( NMath::Min( MeshInfo.BoundsMin.x, Position.x ), NMath::Min( MeshInfo.BoundsMin.y, Position.y ), NMath::Min( MeshInfo.BoundsMin.z, Position.z ) );
			MeshInfo.BoundsMax = Vertex == 0 ? Position : FVector3( NMath::Max( MeshInfo.BoundsMax.x, Position.x ), NMath::Max( MeshInfo.BoundsMax.y, Position.y ), NMath::Max( MeshInfo.BoundsMax.z, Position.z ) );
			Result.BoundsMin = bHasBounds ? FVector3( NMath::Min( Result.BoundsMin.x, Position.x ), NMath::Min( Result.BoundsMin.y, Position.y ), NMath::Min( Result.BoundsMin.z, Position.z ) ) : Position;
			Result.BoundsMax = bHasBounds ? FVector3( NMath::Max( Result.BoundsMax.x, Position.x ), NMath::Max( Result.BoundsMax.y, Position.y ), NMath::Max( Result.BoundsMax.z, Position.z ) ) : Position;
			bHasBounds = true;
		}
		for ( int32 Triangle = 0; Triangle < Mesh.Indices.Num() / 3; ++Triangle )
		{
			int32 Material = Mesh.TriangleMaterials.IsEmpty() ? -1 : Mesh.TriangleMaterials[Triangle]; // -1は既定材質。
			if ( Material < -1 || Material >= Scene.Materials.Num() )
			{
				Error = "Invalid static material index";
				return false;
			}
			Material = Material < 0 ? Scene.Materials.Num() : Material;
			MeshInfo.Material = Triangle == 0 ? Material : MeshInfo.Material == Material ? Material : -1;
			if ( Result.Surface.Sections.IsEmpty() || Result.Surface.Sections.Last().MaterialIndex != Material || Result.Surface.Sections.Last().SourceMeshIndex != MeshIndex )
			{
				FStaticModelSurfaceSection Section; // 連続した材質範囲だけをまとめる。
				Section.FirstIndex = Result.Surface.Indices.Num();
				Section.MaterialIndex = Material;
				Section.SourceMeshIndex = MeshIndex;
				Section.SourceFrameIndex = Mesh.Node;
				Result.Surface.Sections.Add( Section );
			}
			if ( Result.Surface.Sections.Num() > 4096 )
			{
				Error = "Too many static material sections";
				return false;
			}
			for ( int32 Corner = 0; Corner < 3; ++Corner )
			{
				const int32 Index = Mesh.Indices[Triangle * 3 + Corner]; // 元メッシュ内の頂点参照。
				if ( Index < 0 || Index >= Mesh.Positions.Num() )
				{
					Error = "Invalid static triangle index";
					return false;
				}
				Result.Surface.Indices.Add( static_cast<uint32>( BaseVertex + Index ) );
			}
			Result.Surface.Sections.Last().NumIndices += 3;
		}
	}
	// 材質ごとに頂点を分け、reference色に拡散色を一度だけ含める。
	TArray<FMeshVertex> Colored; // 三角形順に展開した頂点。
	Colored.Reserve( Result.Surface.Indices.Num() );
	for ( const auto& Section : Result.Surface.Sections )
	{
		FModelTriangleListInfo List; // 連続した描画範囲を構造にも記録する。
		List.Material = Section.MaterialIndex;
		List.PolygonCount = Section.NumIndices / 3;
		List.VertexCount = Section.NumIndices;
		Result.Structure.TriangleLists.Add( List );
		const auto& Material = Result.Surface.Materials[Section.MaterialIndex]; // 同じ範囲の拡散色。
		for ( int32 Index = Section.FirstIndex; Index < Section.FirstIndex + Section.NumIndices; ++Index )
		{
			FMeshVertex Value = Result.Surface.Vertices[Result.Surface.Indices[Index]]; // 色を確定した頂点。
			for ( int32 Channel = 0; Channel < 4; ++Channel )
			{
				Value.Color[Channel] = ColorByte( Material.Diffuse[Channel] );
			}
			Result.Surface.Indices[Index] = static_cast<uint32>( Colored.Add( Value ) );
		}
	}
	Result.Surface.Vertices = MoveTemp( Colored );
	Out = MoveTemp( Result );
	Error = FString();
	return true;
}
