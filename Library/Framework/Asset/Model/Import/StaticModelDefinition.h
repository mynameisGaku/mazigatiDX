// SPDX-License-Identifier: MIT
#pragma once
#include "Library/Framework/Asset/Model/Import/ImportedScene.h"
#include "Library/Framework/Asset/Model/StaticModelSurfaceData.h"
#include "Library/Math/Matrix4x4.h"
#include "Library/Framework/Asset/Model/ModelStructureTypes.h"

// 取り込んだ静的モデルの不変値。GPUや画像の所有を持たない。
struct FStaticModelDefinition
{
	FModelStructure Structure; // 静的なフレーム・メッシュ・材質のメタデータ。
	FStaticModelSurfaceData Surface; // フレーム変換を適用した形状。
	TArray<FImportedNode> Nodes; // 名前と親基準の変換。
	FVector3 BoundsMin, BoundsMax; // モデル座標の外接箱。
	int32 NumMeshes = 0; // 元のメッシュ数。
};
