// SPDX-License-Identifier: NOASSERTION
#include "DxLibAssetBackend.h"

#include <cmath>
#include <limits>
#include <utility>

#include "Library/Core/Collection/Map.h"
#include "Library/Core/Types/Numeric.h"
#include "Library/Core/Types/Utility.h"

void CDxLibAssetBackend::Deinitialize()
{
	ModelMaterialGeometries.Empty();
	NextModelMaterialGeometry = 1;
	CAppSubsystem::Deinitialize();
}

int32 CDxLibAssetBackend::CreateModelMaterialGeometry( const FStaticModelSurfaceData& Surface, int32 MaterialIndex )
{
	if ( MaterialIndex < 0 || MaterialIndex >= Surface.Materials.Num() || Surface.Vertices.IsEmpty() || Surface.Indices.IsEmpty() || Surface.Indices.Num() % 3 != 0 || Surface.Sections.IsEmpty() ) return -1;
	// snapshot全材質の色値と借用handleを検証する添字。
	for ( int32 MaterialArrayIndex = 0; MaterialArrayIndex < Surface.Materials.Num(); ++MaterialArrayIndex )
	{
		// snapshotに保存された元材質の拡散色と借用画像handle。
		const FStaticModelSurfaceMaterial& Material = Surface.Materials[MaterialArrayIndex];
		if ( Material.BorrowedDiffuseTexture < -1 || !NMath::IsFinite( Material.Diffuse[0] ) || !NMath::IsFinite( Material.Diffuse[1] ) || !NMath::IsFinite( Material.Diffuse[2] ) || !NMath::IsFinite( Material.Diffuse[3] ) ) return -1;
	}
	// 全source vertexの有限性を調べる配列位置。
	for ( int32 VertexIndex = 0; VertexIndex < Surface.Vertices.Num(); ++VertexIndex )
	{
		// geometry資源へコピーする頂点の位置・法線・UV。
		const FMeshVertex& Vertex = Surface.Vertices[VertexIndex];
		if ( !NMath::IsFinite( Vertex.Position[0] ) || !NMath::IsFinite( Vertex.Position[1] ) || !NMath::IsFinite( Vertex.Position[2] ) || !NMath::IsFinite( Vertex.Normal[0] ) || !NMath::IsFinite( Vertex.Normal[1] ) || !NMath::IsFinite( Vertex.Normal[2] ) || !NMath::IsFinite( Vertex.U ) || !NMath::IsFinite( Vertex.V ) ) return -1;
	}
	// 全source indexの範囲を調べる配列位置。
	for ( int32 Index = 0; Index < Surface.Indices.Num(); ++Index )
	{
		// 元頂点配列の範囲を超えないindex。
		if ( Surface.Indices[Index] >= static_cast<uint32>( Surface.Vertices.Num() ) ) return -1;
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
	TArray<FDxLibModelMaterialGeometry::FMesh> BuiltMeshes;
	for ( int32 SectionIndex = 0; SectionIndex < Surface.Sections.Num(); ++SectionIndex )
	{
		// 元のframe・mesh・材質を示す連続section。
		const FStaticModelSurfaceSection& Section = Surface.Sections[SectionIndex];
		// 現section直後の終端をoverflow安全に計算した位置。
		const int64 SectionEnd = static_cast<int64>( Section.FirstIndex ) + Section.NumIndices;
		if ( Section.FirstIndex < 0 || Section.NumIndices <= 0 || Section.NumIndices % 3 != 0 || Section.FirstIndex != ExpectedFirstIndex || SectionEnd > Surface.Indices.Num() || Section.MaterialIndex < 0 || Section.MaterialIndex >= Surface.Materials.Num() || Section.SourceFrameIndex < 0 || Section.SourceMeshIndex < 0 ) return -1;
		ExpectedFirstIndex = SectionEnd;
		// 既出meshが同じframe・材質を保つか調べる対応。
		if ( FSourceMapping* ExistingMapping = SourceMappings.Find( Section.SourceMeshIndex ) )
		{
			if ( ExistingMapping->Frame != Section.SourceFrameIndex || ExistingMapping->Material != Section.MaterialIndex ) return -1;
		}
		else
		{
			// 初めて現れたmeshのframe・材質対応。
			FSourceMapping NewMapping;
			NewMapping.Frame = Section.SourceFrameIndex;
			NewMapping.Material = Section.MaterialIndex;
			SourceMappings.Add( Section.SourceMeshIndex, NewMapping );
		}
		if ( Section.MaterialIndex != MaterialIndex ) continue;

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
			FDxLibModelMaterialGeometry::FMesh NewMesh;
			NewMesh.SourceMeshIndex = Section.SourceMeshIndex;
			NewMesh.SourceFrameIndex = Section.SourceFrameIndex;
			NewMesh.MaterialIndex = Section.MaterialIndex;
			MeshPosition = BuiltMeshes.Add( MoveTemp( NewMesh ) );
		}
		// 同じsource meshの連続runを足す集約先。
		FDxLibModelMaterialGeometry::FMesh& Mesh = BuiltMeshes[MeshPosition];
		if ( Mesh.SourceFrameIndex != Section.SourceFrameIndex || Mesh.MaterialIndex != Section.MaterialIndex ) return -1;

		// section内のindexだけを、元vertex順と三角形向きを保って分割する。
		TArray<FMeshGeometryChunk> SectionChunks;
		// 元sectionだけを参照する三角形index窓。
		const TArrayView<const uint32> SectionIndices( Surface.Indices.GetData() + Section.FirstIndex, Section.NumIndices );
		if ( !NMeshGeometry::BuildChunks( Surface.Vertices, SectionIndices, SectionChunks ) ) return -1;
		// 完成済みsection chunkを順にmesh資源へ移す添字。
		for ( int32 ChunkIndex = 0; ChunkIndex < SectionChunks.Num(); ++ChunkIndex ) Mesh.Chunks.Add( MoveTemp( SectionChunks[ChunkIndex] ) );
	}
	if ( ExpectedFirstIndex != Surface.Indices.Num() || BuiltMeshes.IsEmpty() || NextModelMaterialGeometry <= 0 ) return -1;

	// 登録前に完成させ、失敗時はbackend mapを変更しないimmutable geometry。
	// const共有化前に完成配列を格納する一時所有者。
	TSharedPtr<FDxLibModelMaterialGeometry> MutableGeometry = MakeShared<FDxLibModelMaterialGeometry>();
	MutableGeometry->Meshes = MoveTemp( BuiltMeshes );
	// map登録後に変更しない共有参照。
	TSharedPtr<const FDxLibModelMaterialGeometry> Geometry = MutableGeometry;
	// 登録済みhandleとの衝突を避ける番号。
	const int32 Handle = NextModelMaterialGeometry;
	if ( ModelMaterialGeometries.Contains( Handle ) ) return -1;
	ModelMaterialGeometries.Add( Handle, MoveTemp( Geometry ) );
	NextModelMaterialGeometry = Handle == TNumericLimits<int32>::Max() ? -1 : Handle + 1;
	return Handle;
}

void CDxLibAssetBackend::DeleteModelMaterialGeometry( int32 Geometry )
{
	if ( Geometry >= 0 ) ModelMaterialGeometries.Remove( Geometry );
}

const FDxLibModelMaterialGeometry* CDxLibAssetBackend::FindModelMaterialGeometry( int32 Geometry ) const
{
	// handleから読み取る不変geometry共有参照。
	const TSharedPtr<const FDxLibModelMaterialGeometry>* Found = ModelMaterialGeometries.Find( Geometry );
	return Found != nullptr ? Found->Get() : nullptr;
}

int32 CDxLibAssetBackend::GetNumModelMaterialGeometries() const
{
	return ModelMaterialGeometries.Num();
}
