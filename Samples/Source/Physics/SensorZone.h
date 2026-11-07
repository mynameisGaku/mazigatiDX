// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Interfaces/Physics/IPhysicsContacts.h"
#include "Library/Framework/Object/GameObject.h"
#include "Library/Math/Math.h"

class FRigidBody;
class CPhysicsSubsystem;

//=============================================================================
// CSensorZone
//   センサー（トリガー）の領域。物にぶつからず、通り抜けられるが、中に入った / 出たことは、通知で分かる。
//   剛体の bIsSensor と、接触の通知（IPhysicsContactListener の OnBodyContactBegin / End）の見本。
//   中にある物の数を数えて、1 つでもあれば領域を明るく描く。
//=============================================================================
class CSensorZone : public CGameObject, public IPhysicsContactListener
{
public:
	CSensorZone( const FVector3& InCenter, const FVector3& InHalfExtents );

	void OnSpawn() override;
	void Draw( FDrawContext& CommandList ) override;
	void OnDestroy() override;

	// IPhysicsContactListener（物理の Update の中から、メインスレッドで呼ばれる）
	void OnBodyContactBegin( FRigidBody* BodyA, FRigidBody* BodyB, const FContactInfo& Info ) override;
	void OnBodyContactEnd( FRigidBody* BodyA, FRigidBody* BodyB ) override;

	// 領域の中にある物の数と、今までに入った物の延べ数
	FORCEINLINE int32 GetNumInside() const { return NumInside; }
	FORCEINLINE int32 GetNumEntered() const { return NumEntered; }

private:
	bool IsMine( const FRigidBody* Body ) const;

	FVector3 Center;
	FVector3 HalfExtents;
	int32 NumInside = 0;
	int32 NumEntered = 0;
	CPhysicsSubsystem* Physics = nullptr;
	FRigidBody* Body = nullptr;
};
