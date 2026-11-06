// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Types/TypeDefinition.h"

//=============================================================================
// FBufferRange
//   頂点バッファ・インデックスバッファのうち、描く範囲。FDrawContext::PrimitiveBuffer3D / PrimitiveIndexedBuffer3D に渡す（何も渡さなければ、全部を描く）。
//     Start       インデックスなし（頂点だけ）: 使う最初の頂点の番号。インデックスあり: 使う最初のインデックスの番号
//     Num         使う頂点（インデックスありならインデックス）の数。負なら、最後まで
//     BaseVertex  インデックスありのときだけ使う。各インデックスに足す数（1 つの頂点バッファに、いくつかの形を並べて、形ごとに 0 から数えたインデックスを使い回せる）。負でもよい。インデックスなしのときは 0 にする
//
//   Draw.PrimitiveBuffer3D( Vertices, EDrawPrimitive::Triangles, FBufferRange( 6, 12 ) );                 // 頂点の 6 番目から 12 個
//   Draw.PrimitiveIndexedBuffer3D( Vertices, Indices, EDrawPrimitive::Triangles, FBufferRange( 6, 6, 4 ) );   // インデックスの 6 番目から 6 個。各インデックスに 4 を足す
//=============================================================================
struct FBufferRange
{
	int32 Start = 0;
	int32 Num = -1;
	int32 BaseVertex = 0;

	constexpr FBufferRange() = default;

	constexpr FBufferRange( int32 InStart, int32 InNum, int32 InBaseVertex = 0 )
		: Start( InStart )
		, Num( InNum )
		, BaseVertex( InBaseVertex )
	{
	}
};
