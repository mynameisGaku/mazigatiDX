// SPDX-License-Identifier: MIT
#ifndef FRAMEWORK_CORE_INTERFACES_RENDER_MESHGEOMETRYCHUNK_H
#define FRAMEWORK_CORE_INTERFACES_RENDER_MESHGEOMETRYCHUNK_H

#include "Library/Core/Collection/Array.h"
#include "Library/Core/Collection/ArrayView.h"
#include "Library/Core/Interfaces/Render/DrawMaterialMesh.h"

// 三角形順を保ち、描画上限内に分けた頂点とindexを所有する。
struct FMeshGeometryChunk
{
	// このchunkで使う頂点のコピー。
	TArray<FMeshVertex> Vertices;
	// Verticesを参照するローカルindex列。
	TArray<uint32> Indices;
};

namespace NMeshGeometry
{
	// 全三角形を上限内へ分割し、成功時Outを置換、失敗時Outを保つ。
	// Vertices: 元頂点の読み取り専用配列。
	// Indices: 元頂点を指す三角形index列。
	// Out: 成功時だけ完成したchunk列へ置き換える出力。
	bool BuildChunks( TArrayView<const FMeshVertex> Vertices, TArrayView<const uint32> Indices, TArray<FMeshGeometryChunk>& Out );
}

#endif
