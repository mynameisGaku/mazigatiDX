#include "ContactListener.h"

#include "Library/Core/Thread/Atomic.h"
#include "Library/Core/Thread/Mutex.h"
#include "Library/Physics/PhysicsSystem.h"
#include "Library/Physics/RigidBody.h"

#include <Jolt/Physics/Body/Body.h>

void CContactEventQueue::Dispatch( CPhysicsSystem& System, const TArray<CContactListener*>& Listeners )
{
	{
		FScopedLock Lock( Mutex );
		NAlgo::Swap( DispatchBuffer, Events );
		Events.Reset();
	}

	for ( const FEvent& Event : DispatchBuffer )
	{
		// コールバック内で破棄される可能性があるため毎回引き直す
		FRigidBody* BodyA = System.FindBody( Event.Body1 );
		FRigidBody* BodyB = System.FindBody( Event.Body2 );
		if ( !BodyA || !BodyB )
		{
			LOG( LogPhysics, VeryVerbose, "Contact event dropped: a body was destroyed before the event was delivered" );
			continue;
		}

		for ( CContactListener* Listener : Listeners )
		{
			switch ( Event.Type )
			{
			case EEventType::Added:     Listener->OnContactAdded( BodyA, BodyB, Event.Info );     break;
			case EEventType::Persisted: Listener->OnContactPersisted( BodyA, BodyB, Event.Info ); break;
			case EEventType::Removed:   Listener->OnContactRemoved( BodyA, BodyB );               break;
			}
		}
	}
	DispatchBuffer.Reset();
}

void CContactEventQueue::Clear()
{
	FScopedLock Lock( Mutex );
	Events.Reset();
}

void CContactEventQueue::OnContactAdded( const JPH::Body& Body1, const JPH::Body& Body2, const JPH::ContactManifold& Manifold, JPH::ContactSettings& )
{
	Push( EEventType::Added, Body1, Body2, Manifold );
}

void CContactEventQueue::OnContactPersisted( const JPH::Body& Body1, const JPH::Body& Body2, const JPH::ContactManifold& Manifold, JPH::ContactSettings& )
{
	Push( EEventType::Persisted, Body1, Body2, Manifold );
}

void CContactEventQueue::OnContactRemoved( const JPH::SubShapeIDPair& SubShapePair )
{
	if ( !bEnabled.Load( EMemoryOrder::Relaxed ) ) return;

	FScopedLock Lock( Mutex );
	Events.Add( { EEventType::Removed, SubShapePair.GetBody1ID(), SubShapePair.GetBody2ID(), FContactInfo{} } );
}

void CContactEventQueue::Push( EEventType Type, const JPH::Body& Body1, const JPH::Body& Body2, const JPH::ContactManifold& Manifold )
{
	if ( !bEnabled.Load( EMemoryOrder::Relaxed ) ) return;

	FContactInfo Info;
	Info.Normal           = NPhysics::FromPhys( Manifold.mWorldSpaceNormal );
	Info.PenetrationDepth = Manifold.mPenetrationDepth;
	if ( !Manifold.mRelativeContactPointsOn1.empty() )
	{
		Info.Point = NPhysics::FromPhys( Manifold.GetWorldSpaceContactPointOn1( 0 ) );
	}

	FScopedLock Lock( Mutex );
	Events.Add( { Type, Body1.GetID(), Body2.GetID(), Info } );
}

#pragma region VirtualDefaults

// 仮想関数は vtable 経由で呼ばれインライン展開できないので、ヘッダーではなくここで定義する

void CContactListener::OnContactAdded( FRigidBody* BodyA, FRigidBody* BodyB, const FContactInfo& Info )
{
}

void CContactListener::OnContactPersisted( FRigidBody* BodyA, FRigidBody* BodyB, const FContactInfo& Info )
{
}

void CContactListener::OnContactRemoved( FRigidBody* BodyA, FRigidBody* BodyB )
{
}

#pragma endregion
