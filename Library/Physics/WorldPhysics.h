#pragma once

#include "Library/Core/Collection/Map.h"
#include "Library/Core/Interfaces/Physics/IPhysicsContacts.h"
#include "Library/Core/Interfaces/Tickable/ITickable.h"
#include "Library/Core/Types/Callable.h"
#include "Library/Core/Types/Utility.h"
#include "Library/Framework/Component/Component.h"
#include "Library/Framework/Subsystem/SceneSubsystem.h"
#include "Library/Framework/World/WorldSpawner.h"

class CPhysicsSubsystem;
class FRigidBody;

//=============================================================================
// ワールド（.world）の当たり判定を、物理の剛体にする
//   Framework は Physics を知らないので、Collider 部品の設置は、Physics 側がレジストリに登録する。
//   シーンを作る側（アプリの起動のとき）に、1 回呼ぶ:
//
//   NPhysicsWorld::RegisterSpawners( FWorldSpawnerRegistry::Global() );
//
//   Collider 部品を持つオブジェクトは、NWorld::ResolveCollision（WorldCollision.h）が「付ける」と決めたときだけ、静的な剛体になる
//   （オブジェクトごとの指定 > ワールドのタグ規則 > 既定）。形は Box / Sphere / Capsule / Mesh（モデルの三角形そのまま。静的な床・壁・地形用）。
//   Trigger なら、ぶつからず、重なりだけを知らせる剛体（センサー。動かない Kinematic で、眠らない: 中で止まった物も、出たことにならない。
//   Mesh のトリガーだけは静的のままで、動いている物しか検出できない）。
//   シーンが物理を持たない（IPhysicsScene でない）ときは、警告を出して付けない。
//
//   剛体のユーザーデータは、置き物（CGameObject*）。置き物が消えると、剛体も消える。
//   剛体は設置の時点の位置に置く静的なもので、置き物を動かしても追従しない。
//
//   トリガーの重なり（コードから）:
//
//   CGameObject* Goal = Stage.FindFirst( "Goal" );
//   NPhysicsWorld::SetTriggerCallbacks( *Goal,
//       []( CGameObject& Trigger, FRigidBody& Other ) { /* Other が入った。Other.GetUserData() が、入ったものの持ち主 */ },
//       []( CGameObject& Trigger, FRigidBody& Other ) { /* 出た */ } );
//   - 入った・出たは、同じ物について 1 回ずつ（メッシュなど、1 組で何回も接触が起きる形でも）。物理を進める Tick の中（メインスレッド）で呼ばれるので、中で物を作る・消してもよい。
//   - 入っている最中に、相手が消えたときは、出た、は呼ばれない。
//   - 知らせるのは、重なった「動く物」だけ（静的な物どうし・トリガーどうしは、物理が知らせない）。
//=============================================================================

// 置き物に付く、剛体の持ち主（置き物が消えるとき、剛体を物理のワールドから外す）
class CWorldColliderComponent : public CComponent
{
public:
	using FTriggerCallback = TCallable<void( CGameObject& Trigger, FRigidBody& Other )>;

	CWorldColliderComponent( CPhysicsSubsystem& InPhysics, FRigidBody* InBody, bool bInTrigger ) : Physics( &InPhysics ), Body( InBody ), bTrigger( bInTrigger ) {}

	FORCEINLINE FRigidBody* GetBody() const { return Body; }
	FORCEINLINE bool IsTrigger() const { return bTrigger; }

	// トリガーの重なりの開始・終了を知らせる（空なら知らせない）。トリガーでなければ、呼ばれない
	void SetOnEnter( FTriggerCallback Callback ) { OnEnter = MoveTemp( Callback ); }
	void SetOnExit( FTriggerCallback Callback ) { OnExit = MoveTemp( Callback ); }
	FORCEINLINE const FTriggerCallback& GetOnEnter() const { return OnEnter; }
	FORCEINLINE const FTriggerCallback& GetOnExit() const { return OnExit; }

	void OnDetach() override;

private:
	CPhysicsSubsystem* Physics = nullptr;
	FRigidBody* Body = nullptr;
	bool bTrigger = false;
	FTriggerCallback OnEnter;
	FTriggerCallback OnExit;
};

//=============================================================================
// CWorldTriggerSubsystem
//   物理のあるシーンの、トリガーの重なりを数える。トリガーが 1 つでもあるときだけ、物理の接触の通知を受ける。
//=============================================================================
class CWorldTriggerSubsystem : public CSceneSubsystem, public ITickable, public IPhysicsContactListener
{
public:
	bool ShouldCreateSubsystem( CScene& Scene ) const override;
	void Initialize( FSubsystemCollection& Collection ) override;
	void Deinitialize() override;

	void Tick( float32 DeltaTime ) override;
	bool IsTickable() const override;
	ETickGroup GetTickGroup() const override;

	// トリガーを登録する（設置が呼ぶ）。無くなったトリガーは、Tick が片付ける
	void Register( CWorldColliderComponent& Trigger );

	// いま、トリガーに重なっている物の数（確認・デバッグ用）
	int32 GetNumOverlaps( const CWorldColliderComponent& Trigger ) const;

	void OnContactAdded( FRigidBody* BodyA, FRigidBody* BodyB, const FContactInfo& Info ) override;
	void OnContactRemoved( FRigidBody* BodyA, FRigidBody* BodyB ) override;

private:
	struct FTriggerState
	{
		CWorldColliderComponent* Component = nullptr;
		TMap<uint32, int32> Overlaps;   // 重なっている物（剛体の識別子）-> 接触の数（1 組で複数回の接触が起きる形のため、数える）
	};

	void HandleAdded( FRigidBody* TriggerBody, FRigidBody* Other );
	void HandleRemoved( FRigidBody* TriggerBody, FRigidBody* Other );

	CPhysicsSubsystem* Physics = nullptr;
	bool bListening = false;
	TMap<uint32, FTriggerState> Triggers;   // トリガーの剛体の識別子 -> 状態
};

namespace NPhysicsWorld
{
	// "Collider" の設置を登録する
	void RegisterSpawners( FWorldSpawnerRegistry& Registry );

	// トリガーの重なりの開始・終了を知らせるコールバックを付ける。Trigger が、物理のあるシーンの、Trigger の Collider を持つ置き物でなければ false
	bool SetTriggerCallbacks( CGameObject& Trigger, CWorldColliderComponent::FTriggerCallback OnEnter, CWorldColliderComponent::FTriggerCallback OnExit = nullptr );
}
