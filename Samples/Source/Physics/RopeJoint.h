// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Framework/Object/GameObject.h"
#include "Library/Math/Math.h"

class CPhysicsBody;
class CPhysicsSubsystem;
class FPhysicsConstraint;

//=============================================================================
// CRopeJoint
//   世界の 1 点（フック）から、ロープで物体をぶら下げる。関節（FConstraintSettings::Distance）の見本。
//   ロープは、のびない・ゆるむ（長さが 0 から最大まで）。物体は、フックを中心に振り子のように揺れる。
//   描くのは、フックの点と、フックから物体までの線だけ（物体そのものは、CPhysicsBody が描く）。
//=============================================================================
class CRopeJoint : public CGameObject
{
public:
	// InWeight はぶら下げる物体（先に置いておく）。InHook はフックの位置（メートル）、InLength はロープの長さ（メートル）
	CRopeJoint( const TSharedPtr<CPhysicsBody>& InWeight, const FVector3& InHook, float32 InLength );

	void OnSpawn() override;
	void Draw( FDrawContext& CommandList ) override;
	void OnDestroy() override;

private:
	TWeakPtr<CPhysicsBody> Weight;
	FVector3 Hook;
	float32 Length;
	CPhysicsSubsystem* Physics = nullptr;
	FPhysicsConstraint* Constraint = nullptr;
};
