#pragma once
#include "Library/Core/Types/CompilerMacros.h"
#include "Library/Core/Memory/UniquePtr.h"

#include "Library/Core/Collection/Array.h"
#include "Library/Core/Collection/Map.h"
#include "Library/Physics/PhysicsTypes.h"
#include "Library/Physics/CollisionShape.h"
#include "Library/Physics/RigidBody.h"

namespace JPH
{
	class PhysicsSystem;
	class BodyInterface;
	class TempAllocatorImpl;
	class JobSystemThreadPool;
}

class CContactListener;
class CContactEventQueue;

//=============================================================================
// FPhysicsSettings
//=============================================================================
struct FPhysicsSettings
{
	uint32   MaxBodies             = 10240;
	uint32   MaxBodyPairs          = 65536;
	uint32   MaxContactConstraints = 10240;
	usize    TempAllocatorSize     = 10 * 1024 * 1024;
	int32    NumThreads            = -1;  // -1 で (論理コア数 - 1)

	FVector3 Gravity               = FVector3( 0.0f, -9.81f, 0.0f );

	float32  FixedDeltaTime        = 1.0f / 60.0f;  // 物理の固定ステップ
	int32    MaxSubSteps           = 4;             // 1 回の Update で進める最大ステップ数
	int32    CollisionSteps        = 1;             // 1 ステップ内の衝突判定回数
};

//=============================================================================
// CPhysicsSystem
//   Jolt Physics のワールド 1 つ分。複数作ってよい（シーンごとに CPhysicsSubsystem が 1 つ持つ）。
//   フレームワークに依存しないので、単体でも動かせる。
//
//   CPhysicsSystem Physics;
//   Physics.Init();
//   FRigidBody* Body = Physics.CreateBody( FCollisionShape::Box( { 1, 1, 1 } ), EBodyType::Dynamic );
//   Physics.Update( DeltaTime );
//   FTransform T = Body->GetTransform();
//   Physics.Shutdown();
//=============================================================================
class CPhysicsSystem
{
public:
	CPhysicsSystem();
	virtual ~CPhysicsSystem();

	CPhysicsSystem( const CPhysicsSystem& ) = delete;
	CPhysicsSystem& operator=( const CPhysicsSystem& ) = delete;

	//-------------------------------------------------------------------------
	// ライフサイクル
	//-------------------------------------------------------------------------
	TResult<void> Init( const FPhysicsSettings& InSettings = FPhysicsSettings() );
	void Shutdown();
	FORCEINLINE bool IsInitialized() const { return bInitialized; }

	// 経過時間を蓄積し、FixedDeltaTime ごとにシミュレーションを進める
	void Update( float32 DeltaTime );

	// 描画補間用: 前ステップから次ステップまでの割合 (0..1)
	float32 GetInterpolationAlpha() const;

	//-------------------------------------------------------------------------
	// 剛体
	//-------------------------------------------------------------------------
	// Transform.Scale は形状に焼き込まれる。失敗時は nullptr
	FRigidBody* CreateBody( const FCollisionShape& Shape, EBodyType Type,
							const FTransform& Transform = FTransform::Identity,
							const FRigidBodySettings& Settings = FRigidBodySettings() );
	void DestroyBody( FRigidBody* Body );
	void DestroyAllBodies();

	FRigidBody* FindBody( const PhysBodyID& BodyID ) const;
	FORCEINLINE int32 GetBodyCount() const { return Bodies.Num(); }

	// 大量の Body を追加した後に呼ぶとブロードフェーズが最適化される
	void OptimizeBroadPhase();

	//-------------------------------------------------------------------------
	// 衝突コールバック（所有権は呼び出し側）
	//-------------------------------------------------------------------------
	void AddContactListener( CContactListener* Listener );
	void RemoveContactListener( CContactListener* Listener );

	//-------------------------------------------------------------------------
	// ワールド設定
	//-------------------------------------------------------------------------
	void SetGravity( const FVector3& Gravity );
	FVector3 GetGravity() const;

	//-------------------------------------------------------------------------
	// Jolt への直接アクセス（ラッパーにない機能を使う場合）
	//-------------------------------------------------------------------------
	JPH::PhysicsSystem& GetJoltSystem();
	JPH::BodyInterface& GetBodyInterface();

private:
	struct FLayerFilters;

	void Step( float32 DeltaTime );

	bool             bInitialized = false;
	FPhysicsSettings Settings;
	float32          Accumulator  = 0.0f;

	TUniquePtr<JPH::TempAllocatorImpl>   TempAllocator;
	TUniquePtr<JPH::JobSystemThreadPool> JobSystem;
	TUniquePtr<FLayerFilters>            LayerFilters;
	TUniquePtr<CContactEventQueue>       ContactQueue;
	TUniquePtr<JPH::PhysicsSystem>       JoltSystem;

	TMap<uint32, TUniquePtr<FRigidBody>> Bodies;  // key: BodyID::GetIndexAndSequenceNumber
	TArray<CContactListener*> ContactListeners;
};
