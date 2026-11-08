// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Collection/Array.h"
#include "Library/Framework/Render/Types/ShaderVertex.h"

//=============================================================================
// NMaterialPreview: マテリアルの見た目を確かめる、小さなメッシュ（球・立方体・板）
//   FShaderVertex3D とインデックス（uint16）。大きさは、半径（半辺）1 で、原点が中心。UV は 0〜1。法線は外向き（長さ 1）。
//   CMaterial::DrawSurfaceMesh に渡せる。エディタのプレビューや、サンプルで使う。
//=============================================================================
struct FMaterialPreviewMesh
{
	TArray<FShaderVertex3D> Vertices;
	TArray<uint16> Indices;
};

namespace NMaterialPreview
{
	// 球。Segments は経度の分割（3 以上）、Rings は緯度の分割（2 以上）。頂点は (Segments + 1) * (Rings + 1) 個（65535 を超えない範囲に収める）
	FMaterialPreviewMesh MakeSphere( int32 Segments = 32, int32 Rings = 16 );
	// 立方体（面ごとに頂点を持つ。面の法線）
	FMaterialPreviewMesh MakeCube();
	// 板（XZ 平面。上向き。UV は 0〜1）
	FMaterialPreviewMesh MakePlane();
}
