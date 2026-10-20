#include "RigidBody.h"

#include "Library/Physics/PhysicsSystem.h"

#include <Jolt/Physics/Body/BodyInterface.h>

namespace
{
	JPH::EActivation ToActivation( bool bActivate )
	{
		return bActivate ? JPH::EActivation::Activate : JPH::EActivation::DontActivate;
	}
}

FRigidBody::FRigidBody( JPH::BodyInterface& InBodyInterface, PhysBodyID InBodyID, EBodyType InType, const FVector3& InScale )
	: BodyInterface( &InBodyInterface )
	, BodyID( InBodyID )
	, BodyType( InType )
	, Scale( InScale )
{}

#pragma region Transform

FTransform FRigidBody::GetTransform() const
{
	PhysRVec3 Position;
	PhysQuat  Rotation;
	GetBodyInterface().GetPositionAndRotation( BodyID, Position, Rotation );
	return FTransform( NPhysics::FromPhys( Position ), NPhysics::FromPhys( Rotation ), Scale );
}

void FRigidBody::SetTransform( const FTransform& Transform, bool bActivate )
{
	if ( !Transform.Scale.Equals( Scale ) )
	{
		LOG( LogPhysics, Warning, "Scale cannot be changed after creation. Ignored." );
	}
	GetBodyInterface().SetPositionAndRotation( BodyID, NPhysics::ToPhysR( Transform.Position ), NPhysics::ToPhys( Transform.Rotation.Normalized() ), ToActivation( bActivate ) );
}

FVector3 FRigidBody::GetPosition() const
{
	return NPhysics::FromPhys( GetBodyInterface().GetPosition( BodyID ) );
}

void FRigidBody::SetPosition( const FVector3& Position, bool bActivate )
{
	GetBodyInterface().SetPosition( BodyID, NPhysics::ToPhysR( Position ), ToActivation( bActivate ) );
}

FQuaternion FRigidBody::GetRotation() const
{
	return NPhysics::FromPhys( GetBodyInterface().GetRotation( BodyID ) );
}

void FRigidBody::SetRotation( const FQuaternion& Rotation, bool bActivate )
{
	GetBodyInterface().SetRotation( BodyID, NPhysics::ToPhys( Rotation.Normalized() ), ToActivation( bActivate ) );
}

void FRigidBody::MoveKinematic( const FVector3& TargetPosition, const FQuaternion& TargetRotation, float32 DeltaTime )
{
	if ( BodyType != EBodyType::Kinematic )
	{
		LOG( LogPhysics, Warning, "MoveKinematic called on a non-kinematic body." );
		return;
	}
	GetBodyInterface().MoveKinematic( BodyID, NPhysics::ToPhysR( TargetPosition ), NPhysics::ToPhys( TargetRotation.Normalized() ), DeltaTime );
}

#pragma endregion

#pragma region Velocity

FVector3 FRigidBody::GetLinearVelocity() const
{
	return NPhysics::FromPhys( GetBodyInterface().GetLinearVelocity( BodyID ) );
}

void FRigidBody::SetLinearVelocity( const FVector3& Velocity )
{
	GetBodyInterface().SetLinearVelocity( BodyID, NPhysics::ToPhys( Velocity ) );
}

FVector3 FRigidBody::GetAngularVelocity() const
{
	return NPhysics::FromPhys( GetBodyInterface().GetAngularVelocity( BodyID ) );
}

void FRigidBody::SetAngularVelocity( const FVector3& Velocity )
{
	GetBodyInterface().SetAngularVelocity( BodyID, NPhysics::ToPhys( Velocity ) );
}

#pragma endregion

#pragma region Force

void FRigidBody::AddForce( const FVector3& Force )
{
	GetBodyInterface().AddForce( BodyID, NPhysics::ToPhys( Force ) );
}

void FRigidBody::AddForceAtPosition( const FVector3& Force, const FVector3& WorldPosition )
{
	GetBodyInterface().AddForce( BodyID, NPhysics::ToPhys( Force ), NPhysics::ToPhysR( WorldPosition ) );
}

void FRigidBody::AddTorque( const FVector3& Torque )
{
	GetBodyInterface().AddTorque( BodyID, NPhysics::ToPhys( Torque ) );
}

void FRigidBody::AddImpulse( const FVector3& Impulse )
{
	GetBodyInterface().AddImpulse( BodyID, NPhysics::ToPhys( Impulse ) );
}

void FRigidBody::AddImpulseAtPosition( const FVector3& Impulse, const FVector3& WorldPosition )
{
	GetBodyInterface().AddImpulse( BodyID, NPhysics::ToPhys( Impulse ), NPhysics::ToPhysR( WorldPosition ) );
}

void FRigidBody::AddAngularImpulse( const FVector3& AngularImpulse )
{
	GetBodyInterface().AddAngularImpulse( BodyID, NPhysics::ToPhys( AngularImpulse ) );
}

#pragma endregion

#pragma region State

void FRigidBody::Activate()
{
	GetBodyInterface().ActivateBody( BodyID );
}

void FRigidBody::Deactivate()
{
	GetBodyInterface().DeactivateBody( BodyID );
}

bool FRigidBody::IsActive() const
{
	return GetBodyInterface().IsActive( BodyID );
}

void FRigidBody::SetFriction( float32 Friction )
{
	GetBodyInterface().SetFriction( BodyID, Friction );
}

void FRigidBody::SetRestitution( float32 Restitution )
{
	GetBodyInterface().SetRestitution( BodyID, Restitution );
}

void FRigidBody::SetGravityFactor( float32 GravityFactor )
{
	GetBodyInterface().SetGravityFactor( BodyID, GravityFactor );
}

#pragma endregion
