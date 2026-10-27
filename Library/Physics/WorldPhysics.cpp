#include "WorldPhysics.h"


#include "Library/Core/Log/Log.h"
#include "Library/Core/Types/Numeric.h"
#include "Library/Core/Types/Utility.h"
#include "Library/Framework/Asset/AssetManager.h"
#include "Library/Framework/Object/GameObject.h"
#include "Library/Framework/Scene/Scene.h"
#include "Library/Framework/World/WorldCollision.h"
#include "Library/Physics/PhysicsSubsystem.h"

REGISTER_SUBSYSTEM( CWorldTriggerSubsystem )

void CWorldColliderComponent::OnDetach()
{
	// トリガーの重なりの記録は、CWorldTriggerSubsystem が、剛体が無くなったことを見て片付ける（ここからは触らない: シーンの片付けで、先に無くなっていることがある）
	if ( Physics != nullptr && Body != nullptr ) Physics->DestroyBody( Body );
	Body = nullptr;
}

namespace
{
	constexpr float32 MinScale = 1.0e-4f;

	CWorldTriggerSubsystem* GetTriggers( CScene& Scene ) { return Scene.GetSubsystem<CWorldTriggerSubsystem>(); }

	void SpawnCollider( FWorldSpawnContext& Context )
	{
		if ( !Context.Definition.Collider.IsSet() ) return;
		const FWorldCollider& Spec = Context.Definition.Collider.GetValue();

		const FWorldCollisionDecision Decision = NWorld::ResolveCollision( Context.World.Settings, Context.Definition );
		if ( !Decision.bEnabled ) return;

		CPhysicsSubsystem* Physics = Context.Scene.GetSubsystem<CPhysicsSubsystem>();
		if ( Physics == nullptr )
		{
			Context.Warn( "the collider was not placed: the scene has no physics (derive the scene from IPhysicsScene)" );
			return;
		}

		// 大きさは、剛体の形に焼き込まれる。負の拡大は絶対値で、0 に近い拡大は作れない
		const FTransform& World = Context.WorldTransform;
		const FVector3 Scale( NMath::Abs( World.Scale.x ), NMath::Abs( World.Scale.y ), NMath::Abs( World.Scale.z ) );
		if ( Scale.x < MinScale || Scale.y < MinScale || Scale.z < MinScale )
		{
			Context.Warn( "the collider was not placed: the object's scale has a component that is (almost) 0" );
			return;
		}

		FCollisionShape Shape;
		FVector3 BodyPosition = World.TransformPoint( Spec.Offset );
		switch ( Spec.Shape )
		{
		case EWorldColliderShape::Box: Shape = FCollisionShape::Box( Spec.Size * 0.5f ); break;
		case EWorldColliderShape::Sphere: Shape = FCollisionShape::Sphere( Spec.Radius ); break;
		case EWorldColliderShape::Capsule: Shape = FCollisionShape::Capsule( NMath::Max( ( Spec.Height - Spec.Radius * 2.0f ) * 0.5f, 1.0e-3f ), Spec.Radius ); break;
		case EWorldColliderShape::Mesh:
		{
			// 三角形そのまま（モデル・編集メッシュの座標のまま。ずれ（Offset）は使わない）
			BodyPosition = World.Position;
			TArray<FVector3> Vertices;
			TArray<uint32> Indices;
			if ( NWorld::UsesEditMeshCollider( Context.Definition ) )
			{
				// オブジェクト自身の Mesh 部品（編集メッシュ）
				const FEditMesh& EditMesh = Context.Definition.Mesh.GetValue();
				Vertices = EditMesh.Vertices;
				Indices = EditMesh.BuildTriangles();
				if ( Indices.IsEmpty() )
				{
					Context.Warn( "the mesh collider's Mesh component has no usable faces" );
					return;
				}
			}
			else
			{
				const FString MeshPath = NWorld::GetColliderMeshPath( Context.Definition );
				if ( MeshPath.IsEmpty() )
				{
					Context.Warn( "the mesh collider was not placed: it has no triangles to use (add a Mesh or Model component, or set the collider's Mesh file)" );
					return;
				}
				if ( Context.Assets == nullptr )
				{
					Context.Warn( "the mesh collider '" + MeshPath + "' was not placed: the asset manager is not running" );
					return;
				}
				const TSharedPtr<CModel> Model = Context.Assets->LoadModel( MeshPath );
				if ( !Model.IsValid() )
				{
					Context.Warn( "the mesh collider's model '" + MeshPath + "' could not be loaded" );
					return;
				}
				if ( !Model->GetCollisionMesh( Vertices, Indices ) )
				{
					Context.Warn( "the mesh collider's model '" + MeshPath + "' has no triangles" );
					return;
				}
			}
			const TResult<FCollisionShape> MeshShape = FCollisionShape::Mesh( TArrayView<const FVector3>( Vertices.GetData(), Vertices.Num() ), TArrayView<const uint32>( Indices.GetData(), Indices.Num() ) );
			if ( MeshShape.IsErr() )
			{
				Context.Warn( "the mesh collider's shape could not be made: " + MeshShape.UnwrapErr() );
				return;
			}
			Shape = MeshShape.Unwrap();
			break;
		}
		}
		if ( !Shape.IsValid() )
		{
			Context.Warn( "the collider's shape could not be made" );
			return;
		}

		// トリガーは、動かない運動体（Kinematic）にして、眠らせない。静的な剛体のままだと、物理は眠った（止まった）物を検出しないので、
		// 中で止まった物が「出た」ことになり、また動くと「入った」になってしまう。
		// 眠らせないのは念のため（この版の Jolt では、眠らせても、検出は変わらなかった）。
		// メッシュは、運動体にすると質量が決まらない（Jolt が受け付けない）ので、トリガーでも静的のまま（止まった物は検出できない。検証が警告する）
		const bool bKinematicTrigger = Spec.bTrigger && Spec.Shape != EWorldColliderShape::Mesh;
		FRigidBodySettings Settings;
		Settings.bIsSensor = Spec.bTrigger;
		Settings.bAllowSleeping = !bKinematicTrigger;
		const FTransform BodyTransform( BodyPosition, World.Rotation, Scale );
		FRigidBody* Body = Physics->CreateBody( Shape, bKinematicTrigger ? EBodyType::Kinematic : EBodyType::Static, BodyTransform, Settings );
		if ( Body == nullptr )
		{
			Context.Warn( "the physics world could not create the collider's body" );
			return;
		}
		Body->SetUserData( &Context.Object );
		CWorldColliderComponent* Component = Context.Object.AddComponent<CWorldColliderComponent>( *Physics, Body, Spec.bTrigger );
		if ( Spec.bTrigger )
		{
			if ( CWorldTriggerSubsystem* Triggers = GetTriggers( Context.Scene ) ) Triggers->Register( *Component );
		}
	}
}

//=============================================================================
// CWorldTriggerSubsystem
//=============================================================================
bool CWorldTriggerSubsystem::ShouldCreateSubsystem( CScene& Scene ) const
{
	return dynamic_cast<const IPhysicsScene*>( &Scene ) != nullptr;
}

void CWorldTriggerSubsystem::Initialize( FSubsystemCollection& Collection )
{
	Physics = Collection.InitializeDependency<CPhysicsSubsystem>();
}

void CWorldTriggerSubsystem::Deinitialize()
{
	if ( Physics != nullptr && bListening ) Physics->RemoveContactListener( this );
	bListening = false;
	Triggers.Empty();
	Physics = nullptr;
}

bool CWorldTriggerSubsystem::IsTickable() const
{
	return Physics != nullptr && !Triggers.IsEmpty();
}

ETickGroup CWorldTriggerSubsystem::GetTickGroup() const
{
	return ETickGroup::PostUpdate;
}

void CWorldTriggerSubsystem::Tick( float32 )
{
	// 無くなった剛体の記録を捨てる。物理は、剛体が消えるときに「出た」を知らせないので、捨てないと溜まり、重なりの数が合わなくなる
	TArray<uint32> GoneTriggers;
	for ( auto& Pair : Triggers )
	{
		if ( Physics->FindBody( PhysBodyID( Pair.Key ) ) == nullptr )
		{
			GoneTriggers.Add( Pair.Key );
			continue;
		}
		TArray<uint32> GoneOthers;
		for ( const auto& Overlap : Pair.Value.Overlaps )
		{
			if ( Physics->FindBody( PhysBodyID( Overlap.Key ) ) == nullptr ) GoneOthers.Add( Overlap.Key );
		}
		for ( const uint32 Key : GoneOthers ) Pair.Value.Overlaps.Remove( Key );
	}
	for ( const uint32 Key : GoneTriggers ) Triggers.Remove( Key );
}

void CWorldTriggerSubsystem::Register( CWorldColliderComponent& Trigger )
{
	if ( Physics == nullptr || Trigger.GetBody() == nullptr ) return;
	FTriggerState& State = Triggers.FindOrAdd( Trigger.GetBody()->GetBodyID().GetIndexAndSequenceNumber() );
	State.Component = &Trigger;
	if ( !bListening )
	{
		Physics->AddContactListener( this );
		bListening = true;
	}
}

int32 CWorldTriggerSubsystem::GetNumOverlaps( const CWorldColliderComponent& Trigger ) const
{
	if ( Trigger.GetBody() == nullptr ) return 0;
	const FTriggerState* State = Triggers.Find( Trigger.GetBody()->GetBodyID().GetIndexAndSequenceNumber() );
	return State != nullptr ? State->Overlaps.Num() : 0;
}

void CWorldTriggerSubsystem::OnContactAdded( FRigidBody* BodyA, FRigidBody* BodyB, const FContactInfo& )
{
	HandleAdded( BodyA, BodyB );
	HandleAdded( BodyB, BodyA );
}

void CWorldTriggerSubsystem::OnContactRemoved( FRigidBody* BodyA, FRigidBody* BodyB )
{
	HandleRemoved( BodyA, BodyB );
	HandleRemoved( BodyB, BodyA );
}

void CWorldTriggerSubsystem::HandleAdded( FRigidBody* TriggerBody, FRigidBody* Other )
{
	FTriggerState* State = Triggers.Find( TriggerBody->GetBodyID().GetIndexAndSequenceNumber() );
	if ( State == nullptr || State->Component == nullptr ) return;
	int32& Count = State->Overlaps.FindOrAdd( Other->GetBodyID().GetIndexAndSequenceNumber() );
	if ( ++Count != 1 ) return;

	// コールバックの中で、物を作る・消してもよいように、必要なものを取り出してから呼ぶ（State はもう使わない）
	CWorldColliderComponent* Component = State->Component;
	const CWorldColliderComponent::FTriggerCallback Callback = Component->GetOnEnter();
	CGameObject* Owner = Component->GetOwner();
	if ( Callback && Owner != nullptr ) Callback( *Owner, *Other );
}

void CWorldTriggerSubsystem::HandleRemoved( FRigidBody* TriggerBody, FRigidBody* Other )
{
	FTriggerState* State = Triggers.Find( TriggerBody->GetBodyID().GetIndexAndSequenceNumber() );
	if ( State == nullptr || State->Component == nullptr ) return;
	int32* Count = State->Overlaps.Find( Other->GetBodyID().GetIndexAndSequenceNumber() );
	if ( Count == nullptr ) return;
	if ( --*Count > 0 ) return;
	State->Overlaps.Remove( Other->GetBodyID().GetIndexAndSequenceNumber() );

	CWorldColliderComponent* Component = State->Component;
	const CWorldColliderComponent::FTriggerCallback Callback = Component->GetOnExit();
	CGameObject* Owner = Component->GetOwner();
	if ( Callback && Owner != nullptr ) Callback( *Owner, *Other );
}

//=============================================================================
// 公開の関数
//=============================================================================
void NPhysicsWorld::RegisterSpawners( FWorldSpawnerRegistry& Registry )
{
	Registry.Add( "Collider", SpawnCollider );
}

bool NPhysicsWorld::SetTriggerCallbacks( CGameObject& Trigger, CWorldColliderComponent::FTriggerCallback OnEnter, CWorldColliderComponent::FTriggerCallback OnExit )
{
	CWorldColliderComponent* Component = Trigger.GetComponent<CWorldColliderComponent>();
	if ( Component == nullptr || !Component->IsTrigger() ) return false;
	Component->SetOnEnter( MoveTemp( OnEnter ) );
	Component->SetOnExit( MoveTemp( OnExit ) );
	return true;
}
