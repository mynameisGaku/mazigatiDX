#pragma once
#include "Library/Core/Types/CompilerMacros.h"
#include "Library/Core/Memory/UniquePtr.h"

#include "Library/Core/Collection/Array.h"
#include "Library/Core/Collection/Map.h"
#include "Library/Core/Interfaces/Physics/ICharacterControllers.h"
#include "Library/Core/Interfaces/Physics/ICollisionLayers.h"
#include "Library/Core/Interfaces/Physics/IPhysicsConstraints.h"
#include "Library/Core/Interfaces/Physics/IPhysicsContacts.h"
#include "Library/Core/Interfaces/Physics/IPhysicsDebug.h"
#include "Library/Core/Interfaces/Physics/IPhysicsQuery.h"
#include "Library/Physics/PhysicsTypes.h"
#include "Library/Physics/CollisionShape.h"
#include "Library/Physics/PhysicsCharacter.h"
#include "Library/Physics/PhysicsContactRegistry.h"
#include "Library/Physics/PhysicsConstraint.h"
#include "Library/Physics/RigidBody.h"

namespace JPH
{
	class PhysicsSystem;
	class BodyInterface;
	class TempAllocatorImpl;
	class JobSystemThreadPool;
}

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
//   空間クエリ（レイ・重なり・掃引）は IPhysicsQuery、衝突レイヤーの表は ICollisionLayers、関節（拘束）は IPhysicsConstraints、操作するキャラクターは ICharacterControllers、デバッグ描画は IPhysicsDebug、接触の通知（ぶつかった・離れた・センサーに入った）は IPhysicsContacts として使える。
//
//   CPhysicsSystem Physics;
//   Physics.Init();
//   FRigidBody* Body = Physics.CreateBody( FCollisionShape::Box( { 1, 1, 1 } ), EBodyType::Dynamic );
//   Physics.Update( DeltaTime );
//   FTransform T = Body->GetTransform();
//   Physics.Shutdown();
//=============================================================================
class CPhysicsSystem : public IPhysicsQuery, public ICollisionLayers, public IPhysicsConstraints, public ICharacterControllers, public IPhysicsDebug, public IPhysicsContacts
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
	// 空間クエリ（IPhysicsQuery。使い方と注意はそちらを参照）
	//-------------------------------------------------------------------------
	bool RayCast( const FVector3& Origin, const FVector3& Direction, float32 MaxDistance, FRayHit& OutHit,
				  const FPhysicsQueryFilter& Filter = FPhysicsQueryFilter() ) const override;
	int32 RayCastAll( const FVector3& Origin, const FVector3& Direction, float32 MaxDistance, TArray<FRayHit>& OutHits,
					  const FPhysicsQueryFilter& Filter = FPhysicsQueryFilter() ) const override;
	int32 Overlap( const FQueryShape& Shape, const FVector3& Position, const FQuaternion& Rotation, TArray<FOverlapHit>& OutHits,
				   const FPhysicsQueryFilter& Filter = FPhysicsQueryFilter() ) const override;
	bool Sweep( const FQueryShape& Shape, const FVector3& Start, const FQuaternion& Rotation, const FVector3& Direction, float32 MaxDistance,
				FShapeCastHit& OutHit, const FPhysicsQueryFilter& Filter = FPhysicsQueryFilter() ) const override;

	//-------------------------------------------------------------------------
	// 衝突レイヤー（ICollisionLayers。使い方と注意はそちらを参照）
	//-------------------------------------------------------------------------
	bool SetLayerCollision( uint32 LayerA, uint32 LayerB, bool bCollide ) override;
	bool GetLayerCollision( uint32 LayerA, uint32 LayerB ) const override;
	bool SetLayerCollisionMask( uint32 Layer, uint32 CollidesWithMask ) override;
	uint32 GetLayerCollisionMask( uint32 Layer ) const override;
	void ResetLayerCollision() override;

	//-------------------------------------------------------------------------
	// 関節（IPhysicsConstraints。使い方と注意はそちらを参照）
	//-------------------------------------------------------------------------
	FPhysicsConstraint* CreateConstraint( FRigidBody* BodyA, FRigidBody* BodyB, const FConstraintSettings& ConstraintSettings ) override;
	void DestroyConstraint( FPhysicsConstraint* Constraint ) override;
	void DestroyAllConstraints() override;
	int32 GetConstraintCount() const override;
	bool AddConstraintListener( IPhysicsConstraintListener* Listener ) override;
	void RemoveConstraintListener( IPhysicsConstraintListener* Listener ) override;

	//-------------------------------------------------------------------------
	// 操作するキャラクター（ICharacterControllers。使い方と注意はそちらを参照）
	//-------------------------------------------------------------------------
	FPhysicsCharacter* CreateCharacter( const FVector3& Position, const FCharacterSettings& CharacterSettings ) override;
	void DestroyCharacter( FPhysicsCharacter* Character ) override;
	void DestroyAllCharacters() override;
	int32 GetCharacterCount() const override;

	//-------------------------------------------------------------------------
	// デバッグ描画（IPhysicsDebug。使い方と注意はそちらを参照）
	//-------------------------------------------------------------------------
	int32 DrawDebug( IPhysicsDebugDrawer& Drawer, const FPhysicsDebugDrawSettings& DrawSettings = FPhysicsDebugDrawSettings() ) const override;

	//-------------------------------------------------------------------------
	// 接触の通知（IPhysicsContacts。使い方と注意はそちらを参照。受け取り側の所有権は呼び出し側）
	//-------------------------------------------------------------------------
	bool AddContactListener( IPhysicsContactListener* Listener, const FPhysicsContactFilter& Filter = FPhysicsContactFilter() ) override;
	void RemoveContactListener( IPhysicsContactListener* Listener ) override;
	int32 GetContactListenerCount() const override;

	// 受け取り側が数えている、接触中の剛体の組の数（受け取り側ごとの合計。確認・デバッグ用で、窓口には出さない）
	int32 GetContactPairCount() const;

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
	friend class FPhysicsCharacter;

	struct FLayerFilters;

	void Step( float32 DeltaTime );

	// 衝突の表が変わるレイヤーの、動く剛体を起こす
	void WakeLayerBodies( uint32 LayerMask );

	// この世界で作った剛体か
	bool OwnsBody( const FRigidBody* Body ) const;

	// 関節を Jolt から外し、剛体との関係を切る（入れ物は残す）。bWakeBodies なら、つないでいた剛体を起こす
	void DetachConstraint( FPhysicsConstraint& Constraint, bool bWakeBodies );

	// 剛体につないだ関節をすべて外す（その剛体を破棄する前に呼ぶ）
	void DetachConstraintsOf( const FRigidBody* Body );

	// この世界が持っている関節か
	bool OwnsConstraint( const FPhysicsConstraint* Constraint ) const;

	// 物理のステップの後に、関節がかけた力を調べ、限界を超えたものを外して知らせる
	void UpdateConstraintLoads( float32 StepDelta );

	// 2 つの剛体の衝突を止める・戻す（関節でつないだ組。数えて持つので、止めた回数だけ戻す）
	void IgnoreCollision( const FRigidBody& BodyA, const FRigidBody& BodyB, bool bIgnore );

	// キャラクターが持つ剛体か（DestroyBody では壊せない）
	bool IsCharacterBody( const FRigidBody* Body ) const;

	// キャラクターを Jolt から外し、その剛体も一緒に片付ける（入れ物は残す）
	void DetachCharacter( FPhysicsCharacter& Character );

	// 瞬間移動の後などに、足元の状態（何の上に立っているか）を調べ直す
	void RefreshCharacter( FPhysicsCharacter& Character );

	// キャラクターの高さを変える。新しい形が他の物体にめり込むなら false で何も変えない
	bool ChangeCharacterHeight( FPhysicsCharacter& Character, float32 Height );

	// 固定ステップ 1 つ分、すべてのキャラクターを動かす（Jolt の更新の前に呼ぶ）
	void UpdateCharacters( float32 DeltaTime );

	bool             bInitialized = false;
	FPhysicsSettings Settings;
	float32          Accumulator  = 0.0f;

	// 衝突レイヤーの表。[A] の bit B が立っていれば A と B はぶつかる。常に対称（Init / Shutdown では変えない）
	uint32 LayerCollisionMasks[NCollisionLayer::MaxCount];

	TUniquePtr<JPH::TempAllocatorImpl>   TempAllocator;
	TUniquePtr<JPH::JobSystemThreadPool> JobSystem;
	TUniquePtr<FLayerFilters>            LayerFilters;
	TUniquePtr<CContactEventQueue>       ContactQueue;
	TUniquePtr<JPH::PhysicsSystem>       JoltSystem;

	TMap<uint32, TUniquePtr<FRigidBody>> Bodies;  // key: BodyID::GetIndexAndSequenceNumber
	TArray<TUniquePtr<FPhysicsConstraint>> Constraints;  // 外れて入れ物だけ残っているものも含む
	TArray<TUniquePtr<FPhysicsCharacter>>  Characters;   // 外れて入れ物だけ残っているものも含む
	TArray<IPhysicsConstraintListener*>    ConstraintListeners;  // Init / Shutdown をまたいで残す
	FContactListenerRegistry  ContactListeners;  // 接触の通知の受け取り側。Init / Shutdown をまたいで残す
};
