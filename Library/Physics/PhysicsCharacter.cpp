#include "PhysicsCharacter.h"

#include "Library/Physics/PhysicsCharacterRules.h"
#include "Library/Physics/PhysicsInput.h"
#include "Library/Physics/PhysicsSystem.h"

#pragma push_macro( "min" )
#pragma push_macro( "max" )
#undef min
#undef max
#include <Jolt/Physics/Character/CharacterVirtual.h>
#pragma pop_macro( "max" )
#pragma pop_macro( "min" )

FPhysicsCharacter::FPhysicsCharacter( CPhysicsSystem& InOwner, const FCharacterSettings& InSettings, JPH::CharacterVirtual* InCharacter, FRigidBody* InBody )
	: Owner( &InOwner )
	, Settings( InSettings )
	, Character( InCharacter )
	, Body( InBody )
{
}

#pragma region Movement

FVector3 FPhysicsCharacter::GetPosition() const
{
	return Character != nullptr ? NPhysics::FromPhys( Character->GetPosition() ) : LastPosition;
}

bool FPhysicsCharacter::SetPosition( const FVector3& Position )
{
	if ( Character == nullptr || !NPhysicsInput::IsFiniteVector( Position ) ) return false;

	Character->SetPosition( NPhysics::ToPhysR( Position ) );
	Owner->RefreshCharacter( *this );  // 新しい場所で何かの上に立っているか、すぐ分かるようにする
	return true;
}

bool FPhysicsCharacter::SetMoveVelocity( const FVector3& Velocity )
{
	if ( Character == nullptr || !NPhysicsInput::IsFiniteVector( Velocity ) ) return false;

	MoveVelocity = FVector3( Velocity.x, 0.0f, Velocity.z );
	return true;
}

FVector3 FPhysicsCharacter::GetVelocity() const
{
	return Character != nullptr ? NPhysics::FromPhys( Character->GetLinearVelocity() ) : FVector3::Zero;
}

bool FPhysicsCharacter::Jump( float32 Speed )
{
	if ( Character == nullptr || !NMath::IsFinite( Speed ) || Speed <= 0.0f || !IsOnGround() ) return false;

	bJumpRequested = true;
	JumpSpeed      = Speed;
	return true;
}

#pragma endregion

#pragma region Ground

ECharacterGroundState FPhysicsCharacter::GetGroundState() const
{
	if ( Character == nullptr ) return ECharacterGroundState::InAir;

	switch ( Character->GetGroundState() )
	{
	case JPH::CharacterBase::EGroundState::OnGround:      return ECharacterGroundState::OnGround;
	case JPH::CharacterBase::EGroundState::OnSteepGround: return ECharacterGroundState::OnSteepGround;
	case JPH::CharacterBase::EGroundState::NotSupported:  return ECharacterGroundState::NotSupported;
	case JPH::CharacterBase::EGroundState::InAir:         return ECharacterGroundState::InAir;
	}
	return ECharacterGroundState::InAir;
}

FVector3 FPhysicsCharacter::GetGroundNormal() const
{
	return Character != nullptr ? NPhysics::FromPhys( Character->GetGroundNormal() ) : FVector3::Zero;
}

FVector3 FPhysicsCharacter::GetGroundVelocity() const
{
	return Character != nullptr ? NPhysics::FromPhys( Character->GetGroundVelocity() ) : FVector3::Zero;
}

FRigidBody* FPhysicsCharacter::GetGroundBody() const
{
	return Character != nullptr ? Owner->FindBody( Character->GetGroundBodyID() ) : nullptr;
}

#pragma endregion

#pragma region Settings

bool FPhysicsCharacter::SetHeight( float32 Height )
{
	if ( Character == nullptr || !NCharacterRules::IsValidShape( Settings.Radius, Height ) ) return false;

	return Owner->ChangeCharacterHeight( *this, Height );
}

bool FPhysicsCharacter::SetMaxSlopeAngle( float32 Angle )
{
	if ( Character == nullptr || !NCharacterRules::IsValidSlopeAngle( Angle ) ) return false;

	Character->SetMaxSlopeAngle( Angle );
	Settings.MaxSlopeAngle = Angle;
	return true;
}

bool FPhysicsCharacter::SetStepHeight( float32 Height )
{
	if ( Character == nullptr || !NCharacterRules::IsFiniteNonNegative( Height ) ) return false;

	Settings.StepHeight = Height;
	return true;
}

bool FPhysicsCharacter::SetStickToFloorDistance( float32 Distance )
{
	if ( Character == nullptr || !NCharacterRules::IsFiniteNonNegative( Distance ) ) return false;

	Settings.StickToFloorDistance = Distance;
	return true;
}

bool FPhysicsCharacter::SetMass( float32 Mass )
{
	if ( Character == nullptr || !NCharacterRules::IsFiniteNonNegative( Mass ) ) return false;

	Character->SetMass( Mass );
	Settings.Mass = Mass;
	return true;
}

bool FPhysicsCharacter::SetMaxPushForce( float32 Force )
{
	if ( Character == nullptr || !NCharacterRules::IsFiniteNonNegative( Force ) ) return false;

	Character->SetMaxStrength( Force );
	Settings.MaxPushForce = Force;
	return true;
}

bool FPhysicsCharacter::SetGravityFactor( float32 Factor )
{
	if ( Character == nullptr || !NMath::IsFinite( Factor ) ) return false;

	Settings.GravityFactor = Factor;
	return true;
}

bool FPhysicsCharacter::SetCollisionLayer( uint32 Layer )
{
	if ( Character == nullptr || !NCollisionLayer::IsValid( Layer ) ) return false;

	if ( Body != nullptr && !Body->SetCollisionLayer( Layer ) ) return false;
	Settings.CollisionLayer = Layer;
	return true;
}

#pragma endregion
