// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Types/TypeDefinition.h"
#include "Library/Math/Vector3.h"

//=============================================================================
// モデルへの当たり判定の結果（IAssetBackend::RaycastModel / OverlapModelSphere、FModelCollision）
//=============================================================================
struct FModelHit
{
	// 当たった点と、当たった三角形の向き（表の向きの単位ベクトル）
	FVector3 Position;
	FVector3 Normal;
	// 線の始まりから当たった点までの長さ（球のときは、球の中心から三角形のいちばん近い点まで）
	float32 Distance = 0.0f;
	// 当たった三角形の持ち主のフレーム・材質と、フレームの中での三角形の番号
	int32 Frame = -1;
	int32 Material = -1;
	int32 Triangle = -1;
};
