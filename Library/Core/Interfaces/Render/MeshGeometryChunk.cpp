// SPDX-License-Identifier: NOASSERTION
#include "MeshGeometryChunk.h"

#include <utility>

#include "Library/Core/Collection/Map.h"
#include "Library/Core/Types/Utility.h"

bool NMeshGeometry::BuildChunks( TArrayView<const FMeshVertex> Vertices, TArrayView<const uint32> Indices, TArray<FMeshGeometryChunk>& Out )
{
	if ( Vertices.Num() <= 0 || Vertices.GetData() == nullptr || Indices.Num() <= 0 || Indices.GetData() == nullptr || Indices.Num() % 3 != 0 ) return false;
	for ( int32 Index = 0; Index < Indices.Num(); ++Index )
	{
		// 元頂点配列の範囲を超えていないか調べるindex。
		if ( Indices[Index] >= static_cast<uint32>( Vertices.Num() ) ) return false;
	}

	// 完成するまで呼出元へ見せない作業用chunk列。
	TArray<FMeshGeometryChunk> Built;
	// 現在chunkに使った元頂点だけを記録する逆引き表。
	TMap<uint32, int32> SourceToChunk;
	// 現在の三角形で必要な異なる元頂点。
	TArray<uint32> PendingSources;
	PendingSources.Reserve( 3 );
	// 上限内へ追加中のmesh部分。
	FMeshGeometryChunk Current;
	Current.Vertices.Reserve( FDrawMaterialMeshData::MaxVertices );
	Current.Indices.Reserve( FDrawMaterialMeshData::MaxIndices );

	// 元三角形を並び順どおりにchunkへ配る開始index。
	for ( int32 Triangle = 0; Triangle < Indices.Num(); Triangle += 3 )
	{
		PendingSources.Empty();
		for ( int32 Corner = 0; Corner < 3; ++Corner )
		{
			// 三角形の現在cornerが参照する元頂点。
			const uint32 SourceIndex = Indices[Triangle + Corner];
			if ( SourceToChunk.Find( SourceIndex ) == nullptr && !PendingSources.Contains( SourceIndex ) ) PendingSources.Add( SourceIndex );
		}

		if ( Current.Indices.Num() + 3 > FDrawMaterialMeshData::MaxIndices || Current.Vertices.Num() + PendingSources.Num() > FDrawMaterialMeshData::MaxVertices )
		{
			Built.Add( MoveTemp( Current ) );
			SourceToChunk.Reset();
			Current = FMeshGeometryChunk();
			Current.Vertices.Reserve( FDrawMaterialMeshData::MaxVertices );
			Current.Indices.Reserve( FDrawMaterialMeshData::MaxIndices );
			PendingSources.Empty();
			for ( int32 Corner = 0; Corner < 3; ++Corner )
			{
				// 切替後のchunkで重複を除いた元頂点。
				const uint32 SourceIndex = Indices[Triangle + Corner];
				if ( !PendingSources.Contains( SourceIndex ) ) PendingSources.Add( SourceIndex );
			}
		}

		for ( int32 Corner = 0; Corner < 3; ++Corner )
		{
			// 元三角形のcornerが指す頂点。
			const uint32 SourceIndex = Indices[Triangle + Corner];
			int32* ChunkIndex = SourceToChunk.Find( SourceIndex );
			if ( ChunkIndex == nullptr )
			{
				const int32 NewChunkIndex = Current.Vertices.Num();
				Current.Vertices.Add( Vertices[static_cast<int32>( SourceIndex )] );
				SourceToChunk.Add( SourceIndex, NewChunkIndex );
				ChunkIndex = SourceToChunk.Find( SourceIndex );
			}
			Current.Indices.Add( static_cast<uint32>( *ChunkIndex ) );
		}
	}

	if ( !Current.Indices.IsEmpty() ) Built.Add( MoveTemp( Current ) );
	Out = MoveTemp( Built );
	return true;
}
