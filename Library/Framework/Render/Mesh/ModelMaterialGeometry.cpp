// SPDX-License-Identifier: MIT
#include "ModelMaterialGeometry.h"

#include "Library/Core/Collection/Map.h"
#include "Library/Core/Types/Numeric.h"
#include "Library/Core/Types/Utility.h"

bool NModelMaterialGeometry::Build( const FStaticModelSurfaceData& Surface, int32 MaterialIndex, FModelMaterialGeometry& Out )
{
	if ( MaterialIndex < 0 || MaterialIndex >= Surface.Materials.Num() || Surface.Vertices.IsEmpty() || Surface.Indices.IsEmpty() || Surface.Indices.Num() % 3 != 0 || Surface.Sections.IsEmpty() )
	{
		return false;
	}
	// snapshot全材質の色値と借用handleを検証するインデックス。
	for ( int32 MaterialArrayIndex = 0; MaterialArrayIndex < Surface.Materials.Num(); ++MaterialArrayIndex )
	{
		// snapshotに保存された元材質の拡散色と借用画像handle。
		const FStaticModelSurfaceMaterial& Material = Surface.Materials[MaterialArrayIndex];
		if ( Material.BorrowedDiffuseTexture < -1 || !NMath::IsFinite( Material.Diffuse[0] ) || !NMath::IsFinite( Material.Diffuse[1] ) || !NMath::IsFinite( Material.Diffuse[2] ) || !NMath::IsFinite( Material.Diffuse[3] ) )
		{
			return false;
		}
	}
	// 全source vertexの有限性を調べる配列位置。
	for ( int32 VertexIndex = 0; VertexIndex < Surface.Vertices.Num(); ++VertexIndex )
	{
		// geometry資源へコピーする頂点の位置・法線・UV。
		const FMeshVertex& Vertex = Surface.Vertices[VertexIndex];
		if ( !NMath::IsFinite( Vertex.Position[0] ) || !NMath::IsFinite( Vertex.Position[1] ) || !NMath::IsFinite( Vertex.Position[2] ) || !NMath::IsFinite( Vertex.Normal[0] ) || !NMath::IsFinite( Vertex.Normal[1] ) || !NMath::IsFinite( Vertex.Normal[2] ) || !NMath::IsFinite( Vertex.U ) || !NMath::IsFinite( Vertex.V ) )
		{
			return false;
		}
	}
	// 全source indexの範囲を調べる配列位置。
	for ( int32 Index = 0; Index < Surface.Indices.Num(); ++Index )
	{
		// 元頂点配列の範囲を超えないindex。
		if ( Surface.Indices[Index] >= static_cast<uint32>( Surface.Vertices.Num() ) )
		{
			return false;
		}
	}

	// 全index範囲を隙間なく覆うべき次section開始位置。
	int64 ExpectedFirstIndex = 0;
	// 一つのsource meshが持つframeとmaterialの対応を記録する型。
	struct FSourceMapping
	{
		// 元meshを含むframe。
		int32 Frame = -1;
		// 元meshが参照する材質。
		int32 Material = -1;
	};
	// 同じsource mesh IDの異なる対応付けを拒否する表。
	TMap<int32, FSourceMapping> SourceMappings;
	// 選択材質を持つmeshをsource順で組み立てる配列。
	TArray<FModelMaterialGeometry::FMesh> BuiltMeshes;
	for ( int32 SectionIndex = 0; SectionIndex < Surface.Sections.Num(); ++SectionIndex )
	{
		// 元のframe・mesh・材質を示す連続section。
		const FStaticModelSurfaceSection& Section = Surface.Sections[SectionIndex];
		// 現section直後の終端をoverflow安全に計算した位置。
		const int64 SectionEnd = static_cast<int64>( Section.FirstIndex ) + Section.NumIndices;
		if ( Section.FirstIndex < 0 || Section.NumIndices <= 0 || Section.NumIndices % 3 != 0 || Section.FirstIndex != ExpectedFirstIndex || SectionEnd > Surface.Indices.Num() || Section.MaterialIndex < 0 || Section.MaterialIndex >= Surface.Materials.Num() || Section.SourceFrameIndex < 0 || Section.SourceMeshIndex < 0 )
		{
			return false;
		}
		ExpectedFirstIndex = SectionEnd;
		// 既出meshが同じframe・材質を保つか調べる対応。
		if ( FSourceMapping* ExistingMapping = SourceMappings.Find( Section.SourceMeshIndex ) )
		{
			if ( ExistingMapping->Frame != Section.SourceFrameIndex || ExistingMapping->Material != Section.MaterialIndex )
			{
				return false;
			}
		}
		else
		{
			// 初めて現れたmeshのframe・材質対応。
			FSourceMapping NewMapping;
			NewMapping.Frame = Section.SourceFrameIndex;
			NewMapping.Material = Section.MaterialIndex;
			SourceMappings.Add( Section.SourceMeshIndex, NewMapping );
		}
		if ( Section.MaterialIndex != MaterialIndex )
		{
			continue;
		}

		// 同じsource meshが複数runに分かれた場合も一つへ集約する位置。
		int32 MeshPosition = INDEX_NONE;
		// 既存の集約meshを探す位置。
		for ( int32 MeshIndex = 0; MeshIndex < BuiltMeshes.Num(); ++MeshIndex )
		{
			if ( BuiltMeshes[MeshIndex].SourceMeshIndex == Section.SourceMeshIndex )
			{
				MeshPosition = MeshIndex;
				break;
			}
		}
		if ( MeshPosition == INDEX_NONE )
		{
			// 選択材質を持つsource meshの新しい集約先。
			FModelMaterialGeometry::FMesh NewMesh;
			NewMesh.SourceMeshIndex = Section.SourceMeshIndex;
			NewMesh.SourceFrameIndex = Section.SourceFrameIndex;
			NewMesh.MaterialIndex = Section.MaterialIndex;
			MeshPosition = BuiltMeshes.Add( MoveTemp( NewMesh ) );
		}
		// 同じsource meshの連続runを足す集約先。
		FModelMaterialGeometry::FMesh& Mesh = BuiltMeshes[MeshPosition];
		if ( Mesh.SourceFrameIndex != Section.SourceFrameIndex || Mesh.MaterialIndex != Section.MaterialIndex )
		{
			return false;
		}

		// section内のindexだけを、元vertex順と三角形向きを保って分割する。
		TArray<FMeshGeometryChunk> SectionChunks;
		// 元sectionだけを参照する三角形indexウィンドウ。
		const TArrayView<const uint32> SectionIndices( Surface.Indices.GetData() + Section.FirstIndex, Section.NumIndices );
		if ( !NMeshGeometry::BuildChunks( Surface.Vertices, SectionIndices, SectionChunks ) )
		{
			return false;
		}
		// 完成済みsection chunkを順にmesh資源へ移すインデックス。
		for ( int32 ChunkIndex = 0; ChunkIndex < SectionChunks.Num(); ++ChunkIndex )
		{
			Mesh.Chunks.Add( MoveTemp( SectionChunks[ChunkIndex] ) );
		}
	}
	if ( ExpectedFirstIndex != Surface.Indices.Num() || BuiltMeshes.IsEmpty() )
	{
		return false;
	}
	Out.Meshes = MoveTemp( BuiltMeshes );
	return true;
}
