// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Collection/ArrayView.h"
#include "Library/Core/Interfaces/Render/IVertexBuffer.h"
#include "Library/Core/Memory/SharedPtr.h"
#include "Library/Framework/Render/Vertex3D.h"

//=============================================================================
// CVertexBuffer
//   GPU に置いた、頂点（FVertex3D）の並び（DxLib の頂点バッファ）。一度作って、何度も描く。大きな形や、毎フレーム同じ形を描くときに、頂点を毎回記録しなくて済む。
//   大きさ（頂点の数）は、作ったあと変わらない。中身は、SetData で書き換えられる（アニメーションする形）。
//   ファイルから読むアセットではないので、CAssetManager のキャッシュには入らない。持っている間は有効で、消えるとき、解放する（メインスレッド以外なら、次の更新で）。
//   作るのも、書き換えるのも、メインスレッドだけ（DxLib がスレッドセーフでないため）。それ以外のスレッドからは、nullptr / false を返す。
//   描くには、FDrawContext::PrimitiveBuffer3D / PrimitiveIndexedBuffer3D に渡す。記録してから描かれるまで、バッファを手放さないこと。
//   記録したあと、描かれる前に書き換えると、書き換えたあとの中身で描かれる。
//
//   TSharedPtr<CVertexBuffer> Buffer = CVertexBuffer::Create( Vertices );        // 頂点の並びから作る（作れなければ nullptr）
//   Draw.PrimitiveBuffer3D( Buffer, EDrawPrimitive::Triangles );                 // 全部を三角形として描く
//   Buffer->SetData( 10, ChangedVertices );                                      // 10 番目から書き換える
//=============================================================================
class CVertexBuffer final : public IVertexBuffer
{
public:
	// 作れる頂点の数の上限（DxLib は、これより大きくも作れるが、メモリを使い切らないよう、ここで止める）
	static constexpr int32 MaxVertices = 1 << 24;

	// 頂点 NumVertices 個の空のバッファを作る。中身は 0 で埋まっていて、何も描かれない。NumVertices が 1 未満か MaxVertices を超える・メインスレッドでない・作れないときは、nullptr
	static TSharedPtr<CVertexBuffer> Create( int32 NumVertices );

	// 頂点の並びから、バッファを作って、中身を書く。頂点が 0 個・MaxVertices を超える・数値が有限でない頂点がある・メインスレッドでない・作れないときは、nullptr
	static TSharedPtr<CVertexBuffer> Create( TArrayView<const FVertex3D> Vertices );

	~CVertexBuffer() override;
	CVertexBuffer( const CVertexBuffer& ) = delete;
	CVertexBuffer& operator=( const CVertexBuffer& ) = delete;

	// StartIndex 番目から、Vertices を書く。範囲がバッファに収まらない（StartIndex が負・Vertices が 0 個・終わりがはみ出す）・数値が有限でない頂点がある・メインスレッドでないときは、何も書かず false
	bool SetData( int32 StartIndex, TArrayView<const FVertex3D> Vertices );

	// 頂点の数（作ったときの大きさ）
	int32 GetNumVertices() const { return NumVertices; }

	FVertexBufferHandle GetVertexBufferHandle() const override;

private:
	CVertexBuffer( int32 InVertexBuffer, int32 InNumVertices )
		: VertexBuffer( InVertexBuffer )
		, NumVertices( InNumVertices )
	{
	}

	int32 VertexBuffer = -1;
	int32 NumVertices = 0;
};
