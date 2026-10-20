#pragma once
#include "Library/Core/Collection/ArrayView.h"
#include "Library/Core/Interfaces/Render/MeshTypes.h"
#include "Library/Core/Memory/SharedPtr.h"

//=============================================================================
// CMeshBuffer
//   描画用の頂点バッファとインデックスバッファ（編集メッシュを描くためのもの。ファイルから読むアセットではないので、CAssetManager のキャッシュには入らない）。
//   持っている間は、バッファが有効。消えるとき、バッファを解放する（メインスレッド以外なら、次の更新で）。描画コマンド（FDrawContext::Mesh3D）は、そのハンドルを使う。
//
//   TSharedPtr<CMeshBuffer> Buffer = CMeshBuffer::Create( Build.Vertices, Build.Indices );   // バックエンドが動いていない・作れなければ、nullptr
//=============================================================================
class CMeshBuffer
{
public:
	// 頂点 3 つ以上・インデックス 3 つ以上（3 の倍数）で、範囲内のとき作る
	static TSharedPtr<CMeshBuffer> Create( TArrayView<const FMeshVertex> Vertices, TArrayView<const uint32> Indices );

	~CMeshBuffer();
	CMeshBuffer( const CMeshBuffer& ) = delete;
	CMeshBuffer& operator=( const CMeshBuffer& ) = delete;

	int32 GetVertexBuffer() const { return VertexBuffer; }
	int32 GetIndexBuffer() const { return IndexBuffer; }
	int32 GetNumVertices() const { return NumVertices; }
	int32 GetNumIndices() const { return NumIndices; }

private:
	CMeshBuffer( int32 InVertexBuffer, int32 InIndexBuffer, int32 InNumVertices, int32 InNumIndices )
		: VertexBuffer( InVertexBuffer ), IndexBuffer( InIndexBuffer ), NumVertices( InNumVertices ), NumIndices( InNumIndices ) {}

	int32 VertexBuffer = -1;
	int32 IndexBuffer = -1;
	int32 NumVertices = 0;
	int32 NumIndices = 0;
};
