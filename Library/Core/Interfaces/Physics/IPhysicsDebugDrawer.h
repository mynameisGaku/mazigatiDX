// SPDX-License-Identifier: NOASSERTION
#pragma once
#include "Library/Math/Color.h"
#include "Library/Math/Vector3.h"

//=============================================================================
// IPhysicsDebugDrawer
//   物理のデバッグ描画が、線を 1 本ずつ渡す先。実装は、画面に描く側（FDrawContext など）が持つ。
//   物理の側は、すべての図形を線に直して渡すので、描く側に必要なのは線分の描画だけ。
//   線は、物理の Update と同時でないときに、呼んだスレッドから順に渡される。
//=============================================================================
class IPhysicsDebugDrawer
{
public:
	virtual ~IPhysicsDebugDrawer() = default;

	// ワールド座標の線分 1 本
	virtual void DrawLine( const FVector3& A, const FVector3& B, const FColor32& Color ) = 0;
};
