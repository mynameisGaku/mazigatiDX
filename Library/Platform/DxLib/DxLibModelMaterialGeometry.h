// SPDX-License-Identifier: NOASSERTION
#ifndef LIBRARY_PLATFORM_DXLIB_DXLIBMODELMATERIALGEOMETRY_H
#define LIBRARY_PLATFORM_DXLIB_DXLIBMODELMATERIALGEOMETRY_H

#include "Library/Core/Collection/Array.h"
#include "Library/Core/Interfaces/Render/MeshGeometryChunk.h"

// 選択材質を持つ全meshの不変描画形状。
struct FDxLibModelMaterialGeometry
{
	// 一つの元meshが持つ材質選択・frame対応と分割済み形状。
	struct FMesh
	{
		// nativeモデル内で除外・表示状態を調べるmesh番号。
		int32 SourceMeshIndex = -1;
		// nativeモデル内で親表示状態をたどるframe番号。
		int32 SourceFrameIndex = -1;
		// 実行時に照合する元材質番号。
		int32 MaterialIndex = -1;
		// 上限内へ分けた元meshの三角形。
		TArray<FMeshGeometryChunk> Chunks;
	};

	// Snapshot順を保った選択材質mesh。
	TArray<FMesh> Meshes;
};

#endif
