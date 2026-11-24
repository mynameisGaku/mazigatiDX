// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Collection/Array.h"
#include "Library/Framework/Asset/Model/StaticModelSurfaceData.h"
#include "Library/Framework/Render/Mesh/MeshGeometryChunk.h"

// 選択材質を持つ全meshの不変描画形状（モデル材質効果が、元のモデルから選んだ材質の面だけを描くために使う）。バックエンドに依らない。
struct FModelMaterialGeometry
{
	// 一つの元meshが持つ材質選択・frame対応と分割済み形状。
	struct FMesh
	{
		// モデル内で除外・表示状態を調べるmesh番号。
		int32 SourceMeshIndex = -1;
		// モデル内で親表示状態をたどるframe番号。
		int32 SourceFrameIndex = -1;
		// 実行時に照合する元材質番号。
		int32 MaterialIndex = -1;
		// 元材質に両面描画が指定されているか。
		bool bDoubleSided = false;
		// 上限内へ分けた元meshの三角形。
		TArray<FMeshGeometryChunk> Chunks;
	};

	// Surface 順を保った選択材質mesh。
	TArray<FMesh> Meshes;
};

namespace NModelMaterialGeometry
{
	// Surface（頂点・index・材質範囲）から、MaterialIndex を使う面だけの形状を作る。値が不正（範囲・非有限値・section の抜け・meshの対応違い）や、
	// 選択材質の面が無いときは false（Out は変えない）。
	bool Build( const FStaticModelSurfaceData& Surface, int32 MaterialIndex, FModelMaterialGeometry& Out );
}
