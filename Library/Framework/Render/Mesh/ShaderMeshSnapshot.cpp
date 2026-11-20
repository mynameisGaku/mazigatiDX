// SPDX-License-Identifier: MIT
#include "ShaderMeshSnapshot.h"
#include "Library/Framework/Render/Draw/DrawContext.h"

FShaderMeshSnapshot FShaderMeshSnapshot::Create( const FShaderVertex3D* Vertices, int32 NumVertices, const uint16* Indices, int32 NumIndices )
{
	FShaderMeshSnapshot Result; // 検査を通過した命令だけを所有する。
	constexpr int32 VertexWords = sizeof( FDrawShaderVertex3D ) / 4; // 一つの頂点が占める長さ。
	if ( NumVertices <= 0 || NumVertices > NDrawCommand::MaxWords / VertexWords || NumIndices <= 0 || NumIndices > NDrawCommand::MaxWords * 2 ) { return Result; }
	FDrawBuffer Buffer; // 既存の頂点と添字の検査および記録形式を使う。
	FDrawContext Draw( Buffer ); // この形状専用の記録。
	if ( Draw.PrimitiveIndexedShader3D( Vertices, NumVertices, Indices, NumIndices, EDrawPrimitive::Triangles ) )
	{
		Result.Words = MakeShared<TArray<uint32>>( MoveTemp( Buffer.Words ) );
	}
	return Result;
}

bool FShaderMeshSnapshot::Resolve( const FDrawSpan& Span, FDrawCommandView& InOut )
{
	if ( InOut.Type != EDrawCommand::SharedShaderMesh3D || InOut.Flags != 0 || InOut.NumDataWords != 0 || !Span.ShaderMesh || !Span.ShaderMesh->IsValid() ) { return false; }
	const TArray<uint32>& MeshWords = *Span.ShaderMesh->Words; // 呼出し中はSpanの所有元が保持する。
	InOut = { EDrawCommand::PrimitiveShader3D, 0, MeshWords.GetData() + 1, MeshWords.Num() - 1 };
	return true;
}
