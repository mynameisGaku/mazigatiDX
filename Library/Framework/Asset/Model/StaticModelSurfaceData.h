// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Collection/Array.h"
#include "Library/Framework/Asset/Model/StaticModelSurfaceMaterial.h"
#include "Library/Framework/Asset/Model/StaticModelSurfaceSection.h"
#include "Library/Framework/Render/Types/MeshTypes.h"

// 静的モデルの頂点・三角形・材質範囲を、backend間で受け渡す値データ。
// 材質値は照明・alpha test・UV変換などnative描画の同等性を保証しない。
struct FStaticModelSurfaceData
{
	TArray<FMeshVertex> Vertices;                 // 元modelのreference位置・法線・UV0・色(形式により材質色含む)
	TArray<uint32> Indices;                       // 順序を保った三角形index列
	TArray<FStaticModelSurfaceSection> Sections;  // index列を隙間なく覆う連続材質範囲
	TArray<FStaticModelSurfaceMaterial> Materials; // sectionが参照する材質の写し
};
