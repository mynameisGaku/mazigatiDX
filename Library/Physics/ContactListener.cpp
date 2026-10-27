#include "ContactListener.h"

#include "Library/Core/Thread/Atomic.h"
#include "Library/Core/Thread/Mutex.h"
#include "Library/Physics/PhysicsContactRegistry.h"
#include "Library/Physics/PhysicsContactRules.h"
#include "Library/Physics/PhysicsSystem.h"
#include "Library/Physics/RigidBody.h"

#include <Jolt/Physics/Body/Body.h>

void CContactEventQueue::Dispatch( CPhysicsSystem& System, FContactListenerRegistry& Registry )
{
	{
		FScopedLock Lock( Mutex );
		NAlgo::Swap( DispatchBuffer, Events );
		Events.Reset();
	}

	Registry.BeginDispatch();

	// 接触点ごとの通知。コールバック内で破棄される可能性があるため、剛体は配るたびに引き直す
	for ( const FEvent& Event : DispatchBuffer )
	{
		const uint32 KeyA = Event.Body1.GetIndexAndSequenceNumber();
		const uint32 KeyB = Event.Body2.GetIndexAndSequenceNumber();
		if ( System.FindBody( Event.Body1 ) == nullptr || System.FindBody( Event.Body2 ) == nullptr )
		{
			LOG( LogPhysics, VeryVerbose, "Contact event dropped: a body was destroyed before the event was delivered" );
			Registry.ForgetPair( KeyA, KeyB );
			continue;
		}

		switch ( Event.Type )
		{
		case EEventType::Added:     Registry.DeliverContact( System, FContactListenerRegistry::EContactSignal::Added, KeyA, KeyB, Event.Info );     break;
		case EEventType::Persisted: Registry.DeliverContact( System, FContactListenerRegistry::EContactSignal::Persisted, KeyA, KeyB, Event.Info ); break;
		case EEventType::Removed:   Registry.DeliverContact( System, FContactListenerRegistry::EContactSignal::Removed, KeyA, KeyB, Event.Info );   break;
		}
	}

	// 剛体の組ごとの通知。始まりを先に全部配ってから、終わりを配る
	// （複合形状で、接触する部位が同じステップの中で入れ替わっても、End と Begin の対が途切れないようにする）
	for ( const FEvent& Event : DispatchBuffer )
	{
		if ( Event.Type != EEventType::Added ) continue;
		Registry.DeliverBodyBegin( System, Event.Body1.GetIndexAndSequenceNumber(), Event.Body2.GetIndexAndSequenceNumber(), Event.Info );
	}
	for ( const FEvent& Event : DispatchBuffer )
	{
		if ( Event.Type != EEventType::Removed ) continue;
		Registry.DeliverBodyEnd( System, Event.Body1.GetIndexAndSequenceNumber(), Event.Body2.GetIndexAndSequenceNumber() );
	}

	// 眠って途切れていた組が、起きても触れていなければ End にする
	Registry.DeliverWakeCheck( System );

	DispatchBuffer.Reset();
	Registry.EndDispatch();
}

void CContactEventQueue::Clear()
{
	FScopedLock Lock( Mutex );
	Events.Reset();
}

void CContactEventQueue::AddIgnoredPair( const PhysBodyID& BodyA, const PhysBodyID& BodyB )
{
	++IgnoredPairs.FindOrAdd( NContactRules::MakePairKey( BodyA.GetIndexAndSequenceNumber(), BodyB.GetIndexAndSequenceNumber() ), 0 );
}

void CContactEventQueue::RemoveIgnoredPair( const PhysBodyID& BodyA, const PhysBodyID& BodyB )
{
	const uint64 Key = NContactRules::MakePairKey( BodyA.GetIndexAndSequenceNumber(), BodyB.GetIndexAndSequenceNumber() );
	int32* Count = IgnoredPairs.Find( Key );
	if ( Count == nullptr ) return;

	if ( --( *Count ) <= 0 ) IgnoredPairs.Remove( Key );
}

JPH::ValidateResult CContactEventQueue::OnContactValidate( const JPH::Body& Body1, const JPH::Body& Body2, JPH::RVec3Arg, const JPH::CollideShapeResult& )
{
	// Jolt のワーカースレッドから呼ばれる。読むだけ（書き換えるのは Update の合間）
	if ( !IgnoredPairs.IsEmpty() && IgnoredPairs.Find( NContactRules::MakePairKey( Body1.GetID().GetIndexAndSequenceNumber(), Body2.GetID().GetIndexAndSequenceNumber() ) ) != nullptr )
	{
		return JPH::ValidateResult::RejectAllContactsForThisBodyPair;
	}
	return JPH::ValidateResult::AcceptAllContactsForThisBodyPair;
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

	// 接触の瞬間の速さ。Jolt が接触を見つけた時点では、まだぶつかった反動を解いていないので、ぶつかる前の速さになる
	if ( !Manifold.mRelativeContactPointsOn1.empty() )
	{
		const JPH::RVec3 Point = Manifold.GetWorldSpaceContactPointOn1( 0 );
		Info.Point        = NPhysics::FromPhys( Point );
		Info.ClosingSpeed = NContactRules::ClosingSpeed( NPhysics::FromPhys( Body1.GetPointVelocity( Point ) ), NPhysics::FromPhys( Body2.GetPointVelocity( Point ) ), Info.Normal );
	}
	else
	{
		Info.ClosingSpeed = NContactRules::ClosingSpeed( NPhysics::FromPhys( Body1.GetLinearVelocity() ), NPhysics::FromPhys( Body2.GetLinearVelocity() ), Info.Normal );
	}

	FScopedLock Lock( Mutex );
	Events.Add( { Type, Body1.GetID(), Body2.GetID(), Info } );
}
