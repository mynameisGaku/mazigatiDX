#pragma once
#include "Library/Core/Thread/Atomic.h"
#include "Library/Core/Thread/Mutex.h"
#include "Library/Core/Types/CompilerMacros.h"

#include "Library/Core/Collection/Array.h"
#include "Library/Core/Collection/Map.h"
#include "Library/Core/Interfaces/Physics/PhysicsContactTypes.h"
#include "Library/Physics/PhysicsTypes.h"

#pragma push_macro( "min" )
#pragma push_macro( "max" )
#undef min
#undef max
#include <Jolt/Physics/Collision/ContactListener.h>
#pragma pop_macro( "max" )
#pragma pop_macro( "min" )

class CPhysicsSystem;
class FContactListenerRegistry;

//=============================================================================
// CContactEventQueue（内部用）
//   Jolt のワーカースレッドから届く接触イベントを溜め、
//   メインスレッドで接触の通知の受け取り側（IPhysicsContactListener。FContactListenerRegistry が持つ）に配信する。
//   受け取り側の登録と、衝突コールバックの使い方は IPhysicsContacts を参照。
//=============================================================================
class CContactEventQueue final : public JPH::ContactListener
{
public:
	// 収集の有効/無効（リスナーが 0 個のときは無効にして負荷を下げる）
	FORCEINLINE void SetEnabled( bool bInEnabled ) { bEnabled.Store( bInEnabled, EMemoryOrder::Relaxed ); }

	// 溜まったイベントを配信してクリアする（メインスレッドから呼ぶ）
	void Dispatch( CPhysicsSystem& System, FContactListenerRegistry& Registry );

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

	TAtomic<bool>   bEnabled { false };
	FMutex          Mutex;
	TArray<FEvent>      Events;
	TArray<FEvent>      DispatchBuffer;

	TMap<uint64, int32> IgnoredPairs;  // key: NContactRules::MakePairKey、value: その組をつなぐ関節の数
};
