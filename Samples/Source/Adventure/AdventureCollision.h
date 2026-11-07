// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Math/Math.h"

struct FAdventureState;

//=============================================================================
// NAdventureCollision
//   地面の上から見た、円と四角形の当たり。主人公・敵（円）が、壁・柱・門（四角形）に、めり込まないようにする。
//=============================================================================
namespace NAdventureCollision
{
	// 円（中心 Position の X, Z と、半径 Radius）を、通れない四角形の外へ、押し出す（滑るように、少しずつ動かせる）
	void Resolve( const FAdventureState& State, FVector3& Position, float32 Radius );

	// その位置に、円が置けるか（どの四角形にも、ふれていない）
	bool IsFree( const FAdventureState& State, const FVector3& Position, float32 Radius );
}
