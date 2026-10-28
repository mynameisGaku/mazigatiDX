#pragma once
#include "Library/Framework/Object/GameObject.h"
#include "Library/Math/Math.h"

class FRigidBody;
class CPhysicsSubsystem;
class CTransformComponent;

//=============================================================================
// 物理ワールド (m, Y 上向き) と画面 (px, Y 下向き) の変換
//=============================================================================
namespace NSampleView
{
	constexpr float32 PixelsPerMeter = 50.0f;

	int32 ToScreenX( float32 X );
	int32 ToScreenY( float32 Y );
}

//=============================================================================
// CPhysicsBall
//   シーンの CPhysicsSubsystem に球の剛体を作り、その位置に円を描く。
//   Lifetime 秒経つか画面外に落ちると自分で Destroy する。
//=============================================================================
class CPhysicsBall : public CGameObject
{
public:
	CPhysicsBall( const FVector3& InStartPosition, float32 InRadius, const FColor32& InColor, float32 InLifetime );

	void OnSpawn() override;
	void Tick( float32 DeltaTime ) override;
	void Draw( FDrawContext& CommandList ) override;
	void OnDestroy() override;

private:
	FVector3 StartPosition;
	float32 Radius;
	FColor32 Color;
	float32 Lifetime;
	float32 Age = 0.0f;

	CPhysicsSubsystem* Physics = nullptr;
	FRigidBody* Body = nullptr;
	// 剛体の姿勢を毎フレーム写す（デバッグ表示で位置を見られるように）
	CTransformComponent* Transform = nullptr;
};

//=============================================================================
// CPhysicsFloor
//   静的な箱の床
//=============================================================================
class CPhysicsFloor : public CGameObject
{
public:
	CPhysicsFloor( const FVector3& InCenter, const FVector3& InHalfExtents );

	void OnSpawn() override;
	void Draw( FDrawContext& CommandList ) override;
	void OnDestroy() override;

private:
	FVector3 Center;
	FVector3 HalfExtents;

	CPhysicsSubsystem* Physics = nullptr;
	FRigidBody* Body = nullptr;
};
