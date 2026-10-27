#pragma once
#include "Library/Core/Thread/Atomic.h"
#include "Library/Core/Thread/Mutex.h"
#include "Library/Core/Types/CompilerMacros.h"

#include "Library/Core/Collection/Array.h"
#include "Library/Core/Collection/Map.h"
#include "Library/Physics/PhysicsTypes.h"

#pragma push_macro( "min" )
#pragma push_macro( "max" )
#undef min
#undef max
#include <Jolt/Physics/Collision/ContactListener.h>
#pragma pop_macro( "max" )
#pragma pop_macro( "min" )

class FRigidBody;
class CPhysicsSystem;

//=============================================================================
// FContactInfo
//=============================================================================
struct FContactInfo
{
	FVector3 Point;                     // 接触点（ワールド座標、A 側の表面）
	FVector3 Normal;                    // 接触法線（A から B へ向かう方向）
	float32  PenetrationDepth = 0.0f;   // めり込み量
};

//=============================================================================
// CContactListener
//   衝突コールバックの基底クラス。継承して CPhysicsSystem::AddContactListener で登録する。
//
//   コールバックは CPhysicsSystem::Update() 内、各物理ステップの後に
//   メインスレッドから呼ばれる。そのため中で Body の生成・破棄・力の追加をしてもよい。
//   1 組の物体でも複合形状やメッシュでは部位ごとに複数回呼ばれることがある。
//=============================================================================
class CContactListener
{
public:
	virtual ~CContactListener() = default;

	// 接触開始
	virtual void OnContactAdded( FRigidBody* BodyA, FRigidBody* BodyB, const FContactInfo& Info );

	// 接触継続中（毎ステップ）
	virtual void OnContactPersisted( FRigidBody* BodyA, FRigidBody* BodyB, const FContactInfo& Info );

	// 接触終了（どちらかが既に破棄されている場合は呼ばれない）
	virtual void OnContactRemoved( FRigidBody* BodyA, FRigidBody* BodyB );
};

//=============================================================================
// CContactEventQueue（内部用）
//   Jolt のワーカースレッドから届く接触イベントを溜め、
//   メインスレッドで CContactListener 群に配信する。
//=============================================================================
class CContactEventQueue final : public JPH::ContactListener
{
public:
	// 収集の有効/無効（リスナーが 0 個のときは無効にして負荷を下げる）
	FORCEINLINE void SetEnabled( bool bInEnabled ) { bEnabled.Store( bInEnabled, EMemoryOrder::Relaxed ); }

	// 溜まったイベントを配信してクリアする（メインスレッドから呼ぶ）
	void Dispatch( CPhysicsSystem& System, const TArray<CContactListener*>& Listeners );

	void Clear();

	// 衝突させない組（関節でつないだ剛体。FConstraintSettings::bCollideConnected が false のとき）。同じ組に関節が複数ありうるので、数えて持つ。
	// 変えるのは物理の Update と同時でないとき（Update 中は Jolt のワーカースレッドが読む）
	void AddIgnoredPair( const PhysBodyID& BodyA, const PhysBodyID& BodyB );
	void RemoveIgnoredPair( const PhysBodyID& BodyA, const PhysBodyID& BodyB );

	// JPH::ContactListener
	JPH::ValidateResult OnContactValidate( const JPH::Body& Body1, const JPH::Body& Body2, JPH::RVec3Arg BaseOffset, const JPH::CollideShapeResult& CollisionResult ) override;
	void OnContactAdded( const JPH::Body& Body1, const JPH::Body& Body2, const JPH::ContactManifold& Manifold, JPH::ContactSettings& Settings ) override;
	void OnContactPersisted( const JPH::Body& Body1, const JPH::Body& Body2, const JPH::ContactManifold& Manifold, JPH::ContactSettings& Settings ) override;
	void OnContactRemoved( const JPH::SubShapeIDPair& SubShapePair ) override;

private:
	enum class EEventType : uint8
	{
		Added,
		Persisted,
		Removed,
	};

	struct FEvent
	{
		EEventType   Type;
		PhysBodyID   Body1;
		PhysBodyID   Body2;
		FContactInfo Info;
	};

	void Push( EEventType Type, const JPH::Body& Body1, const JPH::Body& Body2, const JPH::ContactManifold& Manifold );

	// 2 つの剛体の組を 1 つの値にする（順は問わない）
	static uint64 MakePairKey( const PhysBodyID& BodyA, const PhysBodyID& BodyB );

	TAtomic<bool>   bEnabled { false };
	FMutex          Mutex;
	TArray<FEvent>      Events;
	TArray<FEvent>      DispatchBuffer;

	TMap<uint64, int32> IgnoredPairs;  // key: MakePairKey、value: その組をつなぐ関節の数
};
