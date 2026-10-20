// SPDX-License-Identifier: NOASSERTION
#ifndef FRAMEWORK_EFFECT_EFFECTMESHGEOMETRY_H
#define FRAMEWORK_EFFECT_EFFECTMESHGEOMETRY_H

#include "Library/Core/Collection/Array.h"
#include "Library/Core/Types/TypeDefinition.h"
#include "Library/Framework/Effect/EffectMeshDefinition.h"
#include "Library/Core/Interfaces/Render/MeshTypes.h"

// playback内で共有する読み取り専用のCPU頂点・index列。
struct FEffectMeshGeometry
{
	// XY面の形状頂点。粒子ごとの変換では元配列を書き換えない。
	TArray<FMeshVertex> Vertices;
	// 三角形順に並んだ頂点添字。
	TArray<uint32> Indices;
};

namespace NEffectMesh
{
	// 設定から上限内の平面または円環を作り、不正値では出力を変えない。
	bool BuildGeometry( const FEffectMeshDefinition& Definition, FEffectMeshGeometry& Out, FString* Error = nullptr );
}

#endif
