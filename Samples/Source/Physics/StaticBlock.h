// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Framework/Object/GameObject.h"
#include "Library/Math/Math.h"

class FRigidBody;
class CPhysicsSubsystem;

//=============================================================================
// CStaticBlock
//   動かない箱（床・壁・足場）。物理の世界に、動かない剛体（EBodyType::Static）を作り、灰色の四角形で描く。
//   動かないので、Tick で位置を読む必要がない（剛体を持つだけ）。
//=============================================================================
class CStaticBlock : public CGameObject
{
public:
	// InCenter は中心（メートル）、InHalfExtents は半分の大きさ（メートル。Z は奥行き）
	CStaticBlock( const FVector3& InCenter, const FVector3& InHalfExtents );

	void OnSpawn() override;
	void Draw( FDrawContext& CommandList ) override;
	void OnDestroy() override;

private:
	FVector3 Center;
	FVector3 HalfExtents;
	CPhysicsSubsystem* Physics = nullptr;
	FRigidBody* Body = nullptr;
};
