// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Framework/Object/GameObject.h"
#include "Library/Math/Math.h"

class FRigidBody;
class CPhysicsSubsystem;

//=============================================================================
// CPhysicsBody
//   物理で動く球か箱。剛体（動的）を作り、毎フレームその位置と向きで、色つきの図形を描く。
//   剛体の SetUserData に自分を入れるので、レイキャスト（IPhysicsQuery::RayCast）の結果から、このオブジェクトへ戻れる。
//   画面の下へ落ちたら、自分で Destroy する。
//=============================================================================
class CPhysicsBody : public CGameObject
{
public:
	enum class EShape : uint8
	{
		Ball,
		Box,
	};

	// InSize は、球なら半径、箱なら半分の大きさ（メートル）。InPosition は最初の中心（メートル）
	CPhysicsBody( EShape InShape, const FVector3& InPosition, float32 InSize, const FColor32& InColor );

	void OnSpawn() override;
	void Tick( float32 DeltaTime ) override;
	void Draw( FDrawContext& CommandList ) override;
	void OnDestroy() override;

	FORCEINLINE FRigidBody* GetBody() const { return Body; }

	// マウスが指している間、輪郭を白くする（シーンが毎フレーム決める）
	FORCEINLINE void SetHighlighted( bool bInHighlighted ) { bHighlighted = bInHighlighted; }

private:
	EShape Shape;
	FVector3 StartPosition;
	float32 Size;
	FColor32 Color;
	bool bHighlighted = false;
	CPhysicsSubsystem* Physics = nullptr;
	FRigidBody* Body = nullptr;
};
