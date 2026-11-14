// SPDX-License-Identifier: MIT
#pragma once
#include "Library/Core/Render/BufferResourceHandle.h"
#include "Library/Core/Render/GeometryIndexFormat.h"

// GPU上のFGeometryVertex列と、省略可能な頂点番号列の描画範囲。頂点は射影後の同次座標。
struct FGeometryBufferDraw
{
	FBufferResourceHandle VertexBuffer; // 同じ描画キューの所有先が登録した頂点バッファ。
	uint64 VertexOffset = 0; // 頂点列の先頭バイト。4の倍数。
	int32 VertexCount = 0; // 参照できる頂点数。1〜65536。
	FBufferResourceHandle IndexBuffer; // 無効値なら頂点を順番に描く。
	uint64 IndexOffset = 0; // 頂点番号の先頭バイト。整数幅の倍数。
	int32 IndexCount = 0; // 頂点番号の数。使用時は1〜196608、未使用なら0。
	EGeometryIndexFormat IndexFormat = EGeometryIndexFormat::UInt32; // 頂点番号の整数幅。
};
