#include "PhysicsConstraint.h"

#include "Library/Physics/PhysicsConstraintRules.h"
#include "Library/Physics/PhysicsTypes.h"
#include "Library/Physics/RigidBody.h"

#pragma push_macro( "min" )
#pragma push_macro( "max" )
#undef min
#undef max
#include <Jolt/Physics/Body/Body.h>
#include <Jolt/Physics/Constraints/ConeConstraint.h>
#include <Jolt/Physics/Constraints/DistanceConstraint.h>
#include <Jolt/Physics/Constraints/HingeConstraint.h>
#include <Jolt/Physics/Constraints/SliderConstraint.h>
#pragma pop_macro( "max" )
#pragma pop_macro( "min" )

namespace
{
	JPH::SpringSettings ToJoltSpring( const FConstraintSpring& Spring )
	{
		return JPH::SpringSettings( JPH::ESpringMode::FrequencyAndDamping, Spring.Frequency, Spring.Damping );
	}

	FConstraintSpring FromJoltSpring( const JPH::SpringSettings& Spring )
	{
		FConstraintSpring Result;
		Result.Frequency = Spring.mFrequency;
		Result.Damping   = Spring.mDamping;
		return Result;
	}

	// Hinge と Slider の、範囲の両端が同じか（硬く止めると動けなくなる状態）
	bool HasCollapsedLimits( const JPH::HingeConstraint& Hinge ) { return Hinge.GetLimitsMin() == Hinge.GetLimitsMax(); }
	bool HasCollapsedLimits( const JPH::SliderConstraint& Slider ) { return Slider.GetLimitsMin() == Slider.GetLimitsMax(); }
}

FPhysicsConstraint::FPhysicsConstraint( EConstraintType InType, FRigidBody* InBodyA, FRigidBody* InBodyB, JPH::TwoBodyConstraint* InJoint, bool bInCollideConnected )
	: Type( InType )
	, BodyA( InBodyA )
	, BodyB( InBodyB )
	, bAnchoredToWorld( InBodyB == nullptr )
	, bCollideConnected( bInCollideConnected )
	, Joint( InJoint )
{
}

void FPhysicsConstraint::WakeBodies() const
{
	if ( BodyA != nullptr ) BodyA->Activate();
	if ( BodyB != nullptr ) BodyB->Activate();
}

#pragma region State

bool FPhysicsConstraint::IsEnabled() const
{
	return Joint != nullptr && Joint->GetEnabled();
}

bool FPhysicsConstraint::SetEnabled( bool bEnabled )
{
	if ( Joint == nullptr ) return false;

	Joint->SetEnabled( bEnabled );
	WakeBodies();
	return true;
}

float32 FPhysicsConstraint::GetCurrentValue() const
{
	if ( Joint == nullptr ) return 0.0f;

	switch ( Type )
	{
	case EConstraintType::Hinge:
		return static_cast<const JPH::HingeConstraint*>( Joint )->GetCurrentAngle();
	case EConstraintType::Slider:
		return static_cast<const JPH::SliderConstraint*>( Joint )->GetCurrentPosition();
	case EConstraintType::Distance:
	{
		// 2 つの取り付け点（各剛体の重心からの相対位置）をワールドへ直して、その間を測る
		const JPH::RVec3 PointA = Joint->GetBody1()->GetCenterOfMassTransform() * Joint->GetConstraintToBody1Matrix().GetTranslation();
		const JPH::RVec3 PointB = Joint->GetBody2()->GetCenterOfMassTransform() * Joint->GetConstraintToBody2Matrix().GetTranslation();
		return static_cast<float32>( ( PointB - PointA ).Length() );
	}
	default:
		return 0.0f;
	}
}

#pragma endregion

#pragma region Limits

bool FPhysicsConstraint::SetLimits( float32 Min, float32 Max )
{
	if ( Joint == nullptr ) return false;

	float32 LimitMin = 0.0f;
	float32 LimitMax = 0.0f;
	switch ( Type )
	{
	case EConstraintType::Hinge:
	{
		JPH::HingeConstraint* Hinge = static_cast<JPH::HingeConstraint*>( Joint );
		if ( !NConstraintRules::NormalizeLimits( Type, Min, Max, FromJoltSpring( Hinge->GetLimitsSpringSettings() ), false, LimitMin, LimitMax ) ) return false;
		Hinge->SetLimits( LimitMin, LimitMax );
		break;
	}
	case EConstraintType::Slider:
	{
		JPH::SliderConstraint* Slider = static_cast<JPH::SliderConstraint*>( Joint );
		if ( !NConstraintRules::NormalizeLimits( Type, Min, Max, FromJoltSpring( Slider->GetLimitsSpringSettings() ), false, LimitMin, LimitMax ) ) return false;
		Slider->SetLimits( LimitMin, LimitMax );
		break;
	}
	case EConstraintType::Distance:
	{
		if ( !NConstraintRules::NormalizeLimits( Type, Min, Max, FConstraintSpring(), false, LimitMin, LimitMax ) ) return false;
		static_cast<JPH::DistanceConstraint*>( Joint )->SetDistance( LimitMin, LimitMax );
		break;
	}
	default:
		return false;
	}

	WakeBodies();
	return true;
}

bool FPhysicsConstraint::SetLimitSpring( const FConstraintSpring& Spring )
{
	if ( Joint == nullptr || !NConstraintRules::IsValidSpring( Spring ) ) return false;

	switch ( Type )
	{
	case EConstraintType::Hinge:
	{
		JPH::HingeConstraint* Hinge = static_cast<JPH::HingeConstraint*>( Joint );
		if ( Spring.Frequency <= 0.0f && HasCollapsedLimits( *Hinge ) ) return false;  // 範囲が潰れているのに硬く止めると、動けなくなる
		Hinge->SetLimitsSpringSettings( ToJoltSpring( Spring ) );
		break;
	}
	case EConstraintType::Slider:
	{
		JPH::SliderConstraint* Slider = static_cast<JPH::SliderConstraint*>( Joint );
		if ( Spring.Frequency <= 0.0f && HasCollapsedLimits( *Slider ) ) return false;
		Slider->SetLimitsSpringSettings( ToJoltSpring( Spring ) );
		break;
	}
	case EConstraintType::Distance:
		static_cast<JPH::DistanceConstraint*>( Joint )->SetLimitsSpringSettings( ToJoltSpring( Spring ) );
		break;
	default:
		return false;
	}

	WakeBodies();
	return true;
}

bool FPhysicsConstraint::SetMaxFriction( float32 MaxFriction )
{
	float32 Friction = 0.0f;
	if ( Joint == nullptr || !NConstraintRules::NormalizeNonNegative( MaxFriction, Friction ) ) return false;

	switch ( Type )
	{
	case EConstraintType::Hinge:
		static_cast<JPH::HingeConstraint*>( Joint )->SetMaxFrictionTorque( Friction );
		break;
	case EConstraintType::Slider:
		static_cast<JPH::SliderConstraint*>( Joint )->SetMaxFrictionForce( Friction );
		break;
	default:
		return false;
	}

	WakeBodies();
	return true;
}

bool FPhysicsConstraint::SetHalfConeAngle( float32 HalfConeAngle )
{
	if ( Joint == nullptr || Type != EConstraintType::Cone ) return false;
	if ( !NMath::IsFinite( HalfConeAngle ) || HalfConeAngle < 0.0f || HalfConeAngle > NMath::PI ) return false;

	static_cast<JPH::ConeConstraint*>( Joint )->SetHalfConeAngle( HalfConeAngle );
	WakeBodies();
	return true;
}

#pragma endregion

#pragma region Motor

bool FPhysicsConstraint::SetMotorVelocity( float32 TargetVelocity, float32 MaxForce )
{
	float32 Force = 0.0f;
	if ( Joint == nullptr || !NMath::IsFinite( TargetVelocity ) || !NConstraintRules::NormalizeNonNegative( MaxForce, Force ) ) return false;

	switch ( Type )
	{
	case EConstraintType::Hinge:
	{
		JPH::HingeConstraint* Hinge = static_cast<JPH::HingeConstraint*>( Joint );
		Hinge->GetMotorSettings().SetTorqueLimit( Force );
		Hinge->SetTargetAngularVelocity( TargetVelocity );
		Hinge->SetMotorState( JPH::EMotorState::Velocity );
		break;
	}
	case EConstraintType::Slider:
	{
		JPH::SliderConstraint* Slider = static_cast<JPH::SliderConstraint*>( Joint );
		Slider->GetMotorSettings().SetForceLimit( Force );
		Slider->SetTargetVelocity( TargetVelocity );
		Slider->SetMotorState( JPH::EMotorState::Velocity );
		break;
	}
	default:
		return false;
	}

	WakeBodies();
	return true;
}

bool FPhysicsConstraint::SetMotorPosition( float32 TargetPosition, const FConstraintSpring& Spring, float32 MaxForce )
{
	float32 Force = 0.0f;
	if ( Joint == nullptr || !NMath::IsFinite( TargetPosition ) || !NConstraintRules::IsValidSpring( Spring ) || !NConstraintRules::NormalizeNonNegative( MaxForce, Force ) ) return false;

	switch ( Type )
	{
	case EConstraintType::Hinge:
	{
		JPH::HingeConstraint* Hinge = static_cast<JPH::HingeConstraint*>( Joint );
		Hinge->GetMotorSettings().mSpringSettings = ToJoltSpring( Spring );
		Hinge->GetMotorSettings().SetTorqueLimit( Force );
		Hinge->SetTargetAngle( TargetPosition );
		Hinge->SetMotorState( JPH::EMotorState::Position );
		break;
	}
	case EConstraintType::Slider:
	{
		JPH::SliderConstraint* Slider = static_cast<JPH::SliderConstraint*>( Joint );
		Slider->GetMotorSettings().mSpringSettings = ToJoltSpring( Spring );
		Slider->GetMotorSettings().SetForceLimit( Force );
		Slider->SetTargetPosition( TargetPosition );
		Slider->SetMotorState( JPH::EMotorState::Position );
		break;
	}
	default:
		return false;
	}

	WakeBodies();
	return true;
}

bool FPhysicsConstraint::DisableMotor()
{
	if ( Joint == nullptr ) return false;

	switch ( Type )
	{
	case EConstraintType::Hinge:
		static_cast<JPH::HingeConstraint*>( Joint )->SetMotorState( JPH::EMotorState::Off );
		break;
	case EConstraintType::Slider:
		static_cast<JPH::SliderConstraint*>( Joint )->SetMotorState( JPH::EMotorState::Off );
		break;
	default:
		return false;
	}

	WakeBodies();
	return true;
}

EConstraintMotorMode FPhysicsConstraint::GetMotorMode() const
{
	if ( Joint == nullptr ) return EConstraintMotorMode::Off;

	JPH::EMotorState State = JPH::EMotorState::Off;
	switch ( Type )
	{
	case EConstraintType::Hinge:
		State = static_cast<const JPH::HingeConstraint*>( Joint )->GetMotorState();
		break;
	case EConstraintType::Slider:
		State = static_cast<const JPH::SliderConstraint*>( Joint )->GetMotorState();
		break;
	default:
		break;
	}

	switch ( State )
	{
	case JPH::EMotorState::Velocity: return EConstraintMotorMode::Velocity;
	case JPH::EMotorState::Position:
	case JPH::EMotorState::PositionAndVelocity: return EConstraintMotorMode::Position;
	default: return EConstraintMotorMode::Off;
	}
}

#pragma endregion

#pragma region Breaking

bool FPhysicsConstraint::SetBreakLimits( float32 Force, float32 Torque )
{
	if ( Joint == nullptr ) return false;

	float32 NewForce  = 0.0f;
	float32 NewTorque = 0.0f;
	if ( !NConstraintRules::NormalizeNonNegative( Force, NewForce ) || !NConstraintRules::NormalizeNonNegative( Torque, NewTorque ) ) return false;

	BreakForce  = NewForce;
	BreakTorque = NewTorque;
	return true;
}

#pragma endregion
