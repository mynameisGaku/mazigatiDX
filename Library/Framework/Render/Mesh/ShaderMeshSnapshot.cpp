// SPDX-License-Identifier: MIT
#include "ShaderMeshSnapshot.h"
#include "Library/Framework/Render/Draw/DrawContext.h"

namespace
{
	constexpr int32 MaxVertices = 262144; // 一つの共有形状が受け取る最大頂点数。
	constexpr int32 MaxIndices = 786432; // 保持する三角形数を制限するための添字数。
	constexpr int32 MaxPartVertices = 2500; // 既存の命令と両実行先で扱える一部分の頂点数。
	constexpr int32 MaxPartIndices = 18000; // 頂点を再利用しても一部分の添字が際限なく増えないようにする。
	static_assert( FDrawPrimitiveShader3DData::GetCommandWords( MaxPartVertices, MaxPartIndices ) <= NDrawCommand::MaxWords );

	// 個数を検査してから入力を参照する。
	template<typename TIndex>
	bool ValidInput( const FShaderVertex3D* Vertices, int32 NumVertices, const TIndex* Indices, int32 NumIndices )
	{
		return Vertices && Indices && NumVertices > 0 && NumVertices <= MaxVertices && NumIndices > 0 && NumIndices <= MaxIndices && NumIndices % 3 == 0;
	}

	// 既存の頂点検査を通し、一部分を所有する命令として追加する。
	bool WritePart( TArray<TArray<uint32>>& Out, TArrayView<const FShaderVertex3D> Vertices, TArrayView<const uint16> Indices )
	{
		FDrawBuffer Buffer; // 一部分の頂点と添字を一度だけ直列化する。
		FDrawContext Draw( Buffer ); // 既存のシェーダー描画命令を記録する。
		if ( !Draw.PrimitiveIndexedShader3D( Vertices.GetData(), Vertices.Num(), Indices.GetData(), Indices.Num(), EDrawPrimitive::Triangles ) ) { return false; }
		Out.Add( MoveTemp( Buffer.Words ) );
		return true;
	}

	// 元の三角形順で分け、各部分で使う頂点だけを16bit添字に対応付ける。
	template<typename TIndex>
	bool WriteParts( TArray<TArray<uint32>>& Out, TArrayView<const FShaderVertex3D> Vertices, TArrayView<const TIndex> Indices )
	{
		for ( const FShaderVertex3D& Vertex : Vertices )
		{
			if ( !Vertex.IsValid() ) { return false; }
		}
		TArray<int32> LocalBySource; // 元の頂点番号から現在の部分内の番号を引く。
		LocalBySource.SetNum( Vertices.Num(), INDEX_NONE );
		TArray<uint32> Sources; // 部分を閉じたときに対応を解除する元の番号。
		TArray<FShaderVertex3D> LocalVertices; // 現在の部分が実際に使う頂点。
		TArray<uint16> LocalIndices; // 現在の部分の三角形順の添字。
		const auto Flush = [&]()
		{
			if ( !WritePart( Out, LocalVertices, LocalIndices ) ) { return false; }
			for ( const uint32 Source : Sources ) { LocalBySource[Source] = INDEX_NONE; }
			Sources.Reset();
			LocalVertices.Reset();
			LocalIndices.Reset();
			return true;
		};
		for ( int32 First = 0; First < Indices.Num(); First += 3 )
		{
			if ( LocalVertices.Num() > MaxPartVertices - 3 || LocalIndices.Num() > MaxPartIndices - 3 )
			{
				if ( !Flush() ) { return false; }
			}
			for ( int32 Corner = 0; Corner < 3; ++Corner )
			{
				const uint32 Source = Indices[First + Corner]; // 元の頂点を読む前に検査する。
				if ( Source >= static_cast<uint32>( Vertices.Num() ) ) { return false; }
				int32& Local = LocalBySource[Source]; // 同じ部分での最初の登場時にだけ頂点を追加する。
				if ( Local == INDEX_NONE )
				{
					Local = LocalVertices.Add( Vertices[Source] );
					Sources.Add( Source );
				}
				LocalIndices.Add( static_cast<uint16>( Local ) );
			}
		}
		return LocalIndices.IsEmpty() || Flush();
	}
}

FShaderMeshSnapshot FShaderMeshSnapshot::Create( const FShaderVertex3D* Vertices, int32 NumVertices, const uint16* Indices, int32 NumIndices )
{
	FShaderMeshSnapshot Result; // 検査済みの全ての部分だけを公開する。
	if ( NumVertices > 65536 || !ValidInput( Vertices, NumVertices, Indices, NumIndices ) ) { return Result; }
	TArray<TArray<uint32>> Data; // 完成前の描画命令。
	const bool bFits = FDrawPrimitiveShader3DData::GetCommandWords( NumVertices, NumIndices ) <= NDrawCommand::MaxWords; // 小さい従来の形状は同じ命令を保つ。
	const bool bWritten = bFits ? WritePart( Data, { Vertices, NumVertices }, { Indices, NumIndices } ) : WriteParts( Data, TArrayView<const FShaderVertex3D>( Vertices, NumVertices ), TArrayView<const uint16>( Indices, NumIndices ) );
	if ( bWritten ) { Result.Parts = MakeShared<TArray<TArray<uint32>>>( MoveTemp( Data ) ); }
	return Result;
}

FShaderMeshSnapshot FShaderMeshSnapshot::Create( TArrayView<const FShaderVertex3D> Vertices, TArrayView<const uint32> Indices )
{
	FShaderMeshSnapshot Result; // 後ろの三角形が不正なら、先に作った部分も破棄する。
	if ( !ValidInput( Vertices.GetData(), Vertices.Num(), Indices.GetData(), Indices.Num() ) ) { return Result; }
	TArray<TArray<uint32>> Data; // 公開まではこの作成元だけが変更する。
	if ( WriteParts( Data, Vertices, Indices ) ) { Result.Parts = MakeShared<TArray<TArray<uint32>>>( MoveTemp( Data ) ); }
	return Result;
}

bool FShaderMeshSnapshot::Resolve( const FDrawSpan& Span, FDrawCommandView& InOut, int32 PartIndex )
{
	if ( InOut.Type != EDrawCommand::SharedShaderMesh3D || InOut.Flags != 0 || InOut.NumDataWords != 0 || !Span.ShaderMesh || PartIndex < 0 || PartIndex >= Span.ShaderMesh->GetNumParts() ) { return false; }
	const TArray<uint32>& MeshWords = ( *Span.ShaderMesh->Parts )[PartIndex]; // 呼び出し中はSpanの所有元が保持する。
	InOut = { EDrawCommand::PrimitiveShader3D, 0, MeshWords.GetData() + 1, MeshWords.Num() - 1 };
	return true;
}
