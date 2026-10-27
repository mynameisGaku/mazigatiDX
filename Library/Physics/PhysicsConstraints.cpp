#include "PhysicsSystem.h"

#include "Library/Physics/ContactListener.h"
#include "Library/Physics/PhysicsConstraintRules.h"
#include "Library/Physics/PhysicsInput.h"
#include "Library/Core/Types/Numeric.h"
#include "Library/Core/Types/Utility.h"

#pragma push_macro( "min" )
#pragma push_macro( "max" )
#undef min
#undef max
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Physics/Body/Body.h>
#include <Jolt/Physics/Constraints/ConeConstraint.h>
#include <Jolt/Physics/Constraints/DistanceConstraint.h>
#include <Jolt/Physics/Constraints/FixedConstraint.h>
#include <Jolt/Physics/Constraints/HingeConstraint.h>
#include <Jolt/Physics/Constraints/PointConstraint.h>
#include <Jolt/Physics/Constraints/SliderConstraint.h>
#pragma pop_macro( "max" )
#pragma pop_macro( "min" )

namespace
{
	const char* ConstraintTypeToString( EConstraintType Type )
	{
		switch ( Type )
		{
		case EConstraintType::Fixed:    return "fixed";
		case EConstraintType::Point:    return "point";
		case EConstraintType::Distance: return "distance";
		case EConstraintType::Hinge:    return "hinge";
		case EConstraintType::Slider:   return "slider";
		case EConstraintType::Cone:     return "cone";
		}
		return "unknown";
	}

	JPH::SpringSettings ToJoltSpring( const FConstraintSpring& Spring )
	{
		return JPH::SpringSettings( JPH::ESpringMode::FrequencyAndDamping, Spring.Frequency, Spring.Damping );
	}

	// ゲームの設定を確かめてそろえた結果（Jolt の設定を作るのに使う）
	struct FPreparedConstraint
	{
		FVector3 Anchor;
		FVector3 AnchorB;
		FVector3 Axis;
		float32  LimitMin = 0.0f;
		float32  LimitMax = 0.0f;
		float32  MaxFriction = 0.0f;
		float32  BreakForce  = 0.0f;
		float32  BreakTorque = 0.0f;
	};

	// 関節が点と向きを保つためにかけた力積（このステップの合計）を読む。OutForce は位置、OutTorque は向き
	void ReadConstraintImpulses( EConstraintType Type, const JPH::TwoBodyConstraint& Joint, float32& OutForce, float32& OutTorque )
	{
		OutForce  = 0.0f;
		OutTorque = 0.0f;
		switch ( Type )
		{
		case EConstraintType::Fixed:
		{
			const JPH::FixedConstraint& Fixed = static_cast<const JPH::FixedConstraint&>( Joint );
			OutForce  = Fixed.GetTotalLambdaPosition().Length();
			OutTorque = Fixed.GetTotalLambdaRotation().Length();
			break;
		}
		case EConstraintType::Point:
			OutForce = static_cast<const JPH::PointConstraint&>( Joint ).GetTotalLambdaPosition().Length();
			break;
		case EConstraintType::Distance:
			OutForce = NMath::Abs( static_cast<const JPH::DistanceConstraint&>( Joint ).GetTotalLambdaPosition() );
			break;
		case EConstraintType::Hinge:
		{
			const JPH::HingeConstraint& Hinge = static_cast<const JPH::HingeConstraint&>( Joint );
			OutForce  = Hinge.GetTotalLambdaPosition().Length();
			OutTorque = Hinge.GetTotalLambdaRotation().Length();
			break;
		}
		case EConstraintType::Slider:
		{
			const JPH::SliderConstraint& Slider = static_cast<const JPH::SliderConstraint&>( Joint );
			OutForce  = Slider.GetTotalLambdaPosition().Length();
			OutTorque = Slider.GetTotalLambdaRotation().Length();
			break;
		}
		case EConstraintType::Cone:
		{
			const JPH::ConeConstraint& Cone = static_cast<const JPH::ConeConstraint&>( Joint );
			OutForce  = Cone.GetTotalLambdaPosition().Length();
			OutTorque = NMath::Abs( Cone.GetTotalLambdaRotation() );
			break;
		}
		}
	}

	// 設定を確かめる。使えなければ false にして、理由を OutReason へ入れる
	bool PrepareConstraint( const FConstraintSettings& Settings, FPreparedConstraint& OutPrepared, const char*& OutReason )
	{
		OutPrepared.Anchor  = Settings.Anchor;
		OutPrepared.AnchorB = Settings.AnchorB;
		if ( !NPhysicsInput::IsFiniteVector( Settings.Anchor ) )
		{
			OutReason = "the anchor is not finite";
			return false;
		}
		if ( !NConstraintRules::NormalizeNonNegative( Settings.BreakForce, OutPrepared.BreakForce ) || !NConstraintRules::NormalizeNonNegative( Settings.BreakTorque, OutPrepared.BreakTorque ) )
		{
			OutReason = "BreakForce and BreakTorque must be >= 0";
			return false;
		}

		switch ( Settings.Type )
		{
		case EConstraintType::Fixed:
		case EConstraintType::Point:
			return true;

		case EConstraintType::Distance:
			if ( !NPhysicsInput::IsFiniteVector( Settings.AnchorB ) )
			{
				OutReason = "AnchorB is not finite";
				return false;
			}
			if ( !NConstraintRules::IsValidSpring( Settings.LimitSpring ) )
			{
				OutReason = "the limit spring is invalid (frequency and damping must be finite and >= 0)";
				return false;
			}
			if ( !NConstraintRules::NormalizeLimits( Settings.Type, Settings.LimitMin, Settings.LimitMax, Settings.LimitSpring, true, OutPrepared.LimitMin, OutPrepared.LimitMax ) )
			{
				OutReason = "the distance limits are invalid (NaN, or the minimum is above the maximum)";
				return false;
			}
			return true;

		case EConstraintType::Hinge:
		case EConstraintType::Slider:
			if ( !NPhysicsInput::NormalizeDirection( Settings.Axis, OutPrepared.Axis ) )
			{
				OutReason = "the axis is zero or not finite";
				return false;
			}
			if ( !NConstraintRules::IsValidSpring( Settings.LimitSpring ) )
			{
				OutReason = "the limit spring is invalid (frequency and damping must be finite and >= 0)";
				return false;
			}
			if ( !NConstraintRules::NormalizeNonNegative( Settings.MaxFriction, OutPrepared.MaxFriction ) )
			{
				OutReason = "MaxFriction must be >= 0";
				return false;
			}
			if ( !NConstraintRules::NormalizeLimits( Settings.Type, Settings.LimitMin, Settings.LimitMax, Settings.LimitSpring, false, OutPrepared.LimitMin, OutPrepared.LimitMax ) )
			{
				OutReason = "the limits are invalid (NaN, the range excludes 0, or it collapses to a point with a hard limit: use Fixed)";
				return false;
			}
			return true;

		case EConstraintType::Cone:
			if ( !NPhysicsInput::NormalizeDirection( Settings.Axis, OutPrepared.Axis ) )
			{
				OutReason = "the axis is zero or not finite";
				return false;
			}
			if ( !NMath::IsFinite( Settings.HalfConeAngle ) || Settings.HalfConeAngle < 0.0f || Settings.HalfConeAngle > NMath::PI )
			{
				OutReason = "HalfConeAngle must be within 0..PI";
				return false;
			}
			return true;
		}

		OutReason = "unknown constraint type";
		return false;
	}

	// Jolt の関節を作る（まだ世界には足さない）。参照の数は 0 のまま。
	// Jolt は「Body2 が Body1 に対して動いた量」を角度・位置・モーターの向きにするので、Body1 に B（または世界）、Body2 に A を渡す。
	// こうすると、使う側から見て「A が B（または世界）に対して動いた量」になる
	JPH::TwoBodyConstraint* CreateJoltConstraint( const FConstraintSettings& Settings, const FPreparedConstraint& Prepared, JPH::Body& Body1, JPH::Body& Body2 )
	{
		const JPH::RVec3 Anchor  = NPhysics::ToPhysR( Prepared.Anchor );
		const JPH::RVec3 AnchorB = NPhysics::ToPhysR( Prepared.AnchorB );
		const PhysVec3   Axis    = NPhysics::ToPhys( Prepared.Axis );

		switch ( Settings.Type )
		{
		case EConstraintType::Fixed:
		{
			JPH::FixedConstraintSettings Joint;
			Joint.mEnabled = Settings.bEnabled;
			Joint.mSpace   = JPH::EConstraintSpace::WorldSpace;
			Joint.mPoint1  = Anchor;
			Joint.mPoint2  = Anchor;
			return Joint.Create( Body1, Body2 );
		}
		case EConstraintType::Point:
		{
			JPH::PointConstraintSettings Joint;
			Joint.mEnabled = Settings.bEnabled;
			Joint.mSpace   = JPH::EConstraintSpace::WorldSpace;
			Joint.mPoint1  = Anchor;
			Joint.mPoint2  = Anchor;
			return Joint.Create( Body1, Body2 );
		}
		case EConstraintType::Distance:
		{
			JPH::DistanceConstraintSettings Joint;
			Joint.mEnabled               = Settings.bEnabled;
			Joint.mSpace                 = JPH::EConstraintSpace::WorldSpace;
			Joint.mPoint1                = AnchorB;  // Body1 = B 側
			Joint.mPoint2                = Anchor;   // Body2 = A 側
			Joint.mMinDistance           = Prepared.LimitMin;
			Joint.mMaxDistance           = Prepared.LimitMax;
			Joint.mLimitsSpringSettings  = ToJoltSpring( Settings.LimitSpring );
			return Joint.Create( Body1, Body2 );
		}
		case EConstraintType::Hinge:
		{
			// 両方の剛体で同じ軸・同じ基準の向きにする（作った瞬間の姿勢が角度 0）
			const PhysVec3 Normal = Axis.GetNormalizedPerpendicular();
			JPH::HingeConstraintSettings Joint;
			Joint.mEnabled              = Settings.bEnabled;
			Joint.mSpace                = JPH::EConstraintSpace::WorldSpace;
			Joint.mPoint1               = Anchor;
			Joint.mPoint2               = Anchor;
			Joint.mHingeAxis1           = Axis;
			Joint.mHingeAxis2           = Axis;
			Joint.mNormalAxis1          = Normal;
			Joint.mNormalAxis2          = Normal;
			Joint.mLimitsMin            = Prepared.LimitMin;
			Joint.mLimitsMax            = Prepared.LimitMax;
			Joint.mLimitsSpringSettings = ToJoltSpring( Settings.LimitSpring );
			Joint.mMaxFrictionTorque    = Prepared.MaxFriction;
			return Joint.Create( Body1, Body2 );
		}
		case EConstraintType::Slider:
		{
			JPH::SliderConstraintSettings Joint;
			Joint.mEnabled              = Settings.bEnabled;
			Joint.mSpace                = JPH::EConstraintSpace::WorldSpace;
			Joint.mAutoDetectPoint      = false;
			Joint.mPoint1               = Anchor;
			Joint.mPoint2               = Anchor;
			Joint.SetSliderAxis( Axis );  // 両方の剛体の軸と、直交する基準の向きをそろえる
			Joint.mLimitsMin            = Prepared.LimitMin;
			Joint.mLimitsMax            = Prepared.LimitMax;
			Joint.mLimitsSpringSettings = ToJoltSpring( Settings.LimitSpring );
			Joint.mMaxFrictionForce     = Prepared.MaxFriction;
			return Joint.Create( Body1, Body2 );
		}
		case EConstraintType::Cone:
		{
			JPH::ConeConstraintSettings Joint;
			Joint.mEnabled       = Settings.bEnabled;
			Joint.mSpace         = JPH::EConstraintSpace::WorldSpace;
			Joint.mPoint1        = Anchor;
			Joint.mPoint2        = Anchor;
			Joint.mTwistAxis1    = Axis;
			Joint.mTwistAxis2    = Axis;
			Joint.mHalfConeAngle = Settings.HalfConeAngle;
			return Joint.Create( Body1, Body2 );
		}
		}
		return nullptr;
	}
}

bool CPhysicsSystem::OwnsBody( const FRigidBody* Body ) const
{
	return Body != nullptr && FindBody( Body->GetBodyID() ) == Body;
}

FPhysicsConstraint* CPhysicsSystem::CreateConstraint( FRigidBody* BodyA, FRigidBody* BodyB, const FConstraintSettings& ConstraintSettings )
{
	if ( !bInitialized )
	{
		LOG( LogPhysics, Error, "CreateConstraint called before Init." );
		return nullptr;
	}
	if ( !OwnsBody( BodyA ) || ( BodyB != nullptr && !OwnsBody( BodyB ) ) )
	{
		LOG( LogPhysics, Error, "CreateConstraint called with a body that is null (BodyA) or was not created by this world." );
		return nullptr;
	}
	if ( BodyA == BodyB )
	{
		LOG( LogPhysics, Error, "CreateConstraint called with the same body on both sides." );
		return nullptr;
	}
	if ( BodyA->GetBodyType() != EBodyType::Dynamic && ( BodyB == nullptr || BodyB->GetBodyType() != EBodyType::Dynamic ) )
	{
		LOG( LogPhysics, Error, "CreateConstraint needs at least one dynamic body." );
		return nullptr;
	}

	FPreparedConstraint Prepared;
	const char* Reason = "";
	if ( !PrepareConstraint( ConstraintSettings, Prepared, Reason ) )
	{
		LOG( LogPhysics, Error, "CreateConstraint called with invalid settings (%s): %s", ConstraintTypeToString( ConstraintSettings.Type ), Reason );
		return nullptr;
	}

	// 関節を作る。B が無ければ世界に固定する（Jolt の Body1 が B か世界、Body2 が A）
	const JPH::BodyLockInterfaceNoLock& Locks = JoltSystem->GetBodyLockInterfaceNoLock();
	JPH::Body* JoltBodyA = Locks.TryGetBody( BodyA->GetBodyID() );
	JPH::Body* JoltBodyB = BodyB != nullptr ? Locks.TryGetBody( BodyB->GetBodyID() ) : &JPH::Body::sFixedToWorld;
	if ( JoltBodyA == nullptr || JoltBodyB == nullptr )
	{
		LOG( LogPhysics, Error, "CreateConstraint could not find the Jolt body." );
		return nullptr;
	}

	JPH::TwoBodyConstraint* Joint = CreateJoltConstraint( ConstraintSettings, Prepared, *JoltBodyB, *JoltBodyA );
	if ( Joint == nullptr )
	{
		LOG( LogPhysics, Error, "CreateConstraint failed to create the Jolt constraint." );
		return nullptr;
	}
	JoltSystem->AddConstraint( Joint );  // 世界が参照を 1 つ持つ
	Joint->AddRef();                     // こちらも 1 つ持つ（外すときに、世界から外してから手放す）

	if ( !ConstraintSettings.bCollideConnected && BodyB != nullptr ) IgnoreCollision( *BodyA, *BodyB, true );

	FPhysicsConstraint* Constraint = new FPhysicsConstraint( ConstraintSettings.Type, BodyA, BodyB, Joint, ConstraintSettings.bCollideConnected );
	Constraint->BreakForce  = Prepared.BreakForce;
	Constraint->BreakTorque = Prepared.BreakTorque;
	Constraints.Add( TUniquePtr<FPhysicsConstraint>( Constraint ) );
	if ( ConstraintSettings.bEnabled ) Constraint->WakeBodies();

	LOG( LogPhysics, Verbose, "Created %s constraint (%d constraint(s) in the world)", ConstraintTypeToString( ConstraintSettings.Type ), Constraints.Num() );
	return Constraint;
}

void CPhysicsSystem::DestroyConstraint( FPhysicsConstraint* Constraint )
{
	if ( Constraint == nullptr || !bInitialized ) return;

	const int32 Index = Constraints.IndexOfByPredicate( [Constraint]( const TUniquePtr<FPhysicsConstraint>& Item ) { return Item.Get() == Constraint; } );
	if ( Index == INDEX_NONE )
	{
		LOG( LogPhysics, Warning, "DestroyConstraint called with an unknown constraint." );
		return;
	}

	DetachConstraint( *Constraint, true );
	Constraints.RemoveAtSwap( Index );
	LOG( LogPhysics, Verbose, "Destroyed constraint (%d constraint(s) left)", Constraints.Num() );
}

void CPhysicsSystem::DestroyAllConstraints()
{
	if ( !bInitialized ) return;

	if ( !Constraints.IsEmpty() ) LOG( LogPhysics, Verbose, "Destroying all %d constraint(s)", Constraints.Num() );
	for ( const TUniquePtr<FPhysicsConstraint>& Constraint : Constraints ) DetachConstraint( *Constraint, true );
	Constraints.Empty();
}

int32 CPhysicsSystem::GetConstraintCount() const
{
	return Constraints.Num();
}

bool CPhysicsSystem::AddConstraintListener( IPhysicsConstraintListener* Listener )
{
	if ( Listener == nullptr ) return false;

	ConstraintListeners.AddUnique( Listener );
	return true;
}

void CPhysicsSystem::RemoveConstraintListener( IPhysicsConstraintListener* Listener )
{
	ConstraintListeners.Remove( Listener );
}

bool CPhysicsSystem::OwnsConstraint( const FPhysicsConstraint* Constraint ) const
{
	return Constraint != nullptr && Constraints.ContainsByPredicate( [Constraint]( const TUniquePtr<FPhysicsConstraint>& Item ) { return Item.Get() == Constraint; } );
}

void CPhysicsSystem::UpdateConstraintLoads( float32 StepDelta )
{
	// Jolt は、衝突のステップ（CollisionSteps）ごとに関節を解く。力積は最後の 1 回分なので、その長さで割る
	const float32 SolveDelta = StepDelta / static_cast<float32>( NMath::Max( 1, Settings.CollisionSteps ) );

	struct FBreak
	{
		FPhysicsConstraint*  Constraint;
		PhysBodyID           IdA;
		PhysBodyID           IdB;
		bool                 bHasB;
		FConstraintBreakInfo Info;
	};
	TArray<FBreak> Breaks;

	for ( const TUniquePtr<FPhysicsConstraint>& Item : Constraints )
	{
		FPhysicsConstraint& Constraint = *Item;
		Constraint.AppliedForce  = 0.0f;
		Constraint.AppliedTorque = 0.0f;
		if ( Constraint.Joint == nullptr || !Constraint.Joint->GetEnabled() ) continue;

		// つないだ剛体がどちらも眠っているなら、関節は解かれていない（前の力積が残っているだけ）
		if ( !Constraint.BodyA->IsActive() && ( Constraint.BodyB == nullptr || !Constraint.BodyB->IsActive() ) ) continue;

		float32 ImpulseForce  = 0.0f;
		float32 ImpulseTorque = 0.0f;
		ReadConstraintImpulses( Constraint.Type, *Constraint.Joint, ImpulseForce, ImpulseTorque );
		Constraint.AppliedForce  = NConstraintRules::ImpulseToLoad( ImpulseForce, SolveDelta );
		Constraint.AppliedTorque = NConstraintRules::ImpulseToLoad( ImpulseTorque, SolveDelta );

		if ( !NConstraintRules::ShouldBreak( Constraint.AppliedForce, Constraint.AppliedTorque, Constraint.BreakForce, Constraint.BreakTorque ) ) continue;

		FBreak Break;
		Break.Constraint  = &Constraint;
		Break.IdA         = Constraint.BodyA->GetBodyID();
		Break.bHasB       = Constraint.BodyB != nullptr;
		if ( Break.bHasB ) Break.IdB = Constraint.BodyB->GetBodyID();
		Break.Info.Force  = Constraint.AppliedForce;
		Break.Info.Torque = Constraint.AppliedTorque;
		Breaks.Add( Break );
	}
	if ( Breaks.IsEmpty() ) return;

	// 先にすべて外してから知らせる（通知の中で、世界の状態を触ってよいように）
	for ( const FBreak& Break : Breaks )
	{
		Break.Constraint->bBroken = true;
		DetachConstraint( *Break.Constraint, true );
		LOG( LogPhysics, Verbose, "A %s constraint broke (force %.1f N, torque %.1f N*m)", ConstraintTypeToString( Break.Constraint->Type ), Break.Info.Force, Break.Info.Torque );
	}
	if ( ConstraintListeners.IsEmpty() ) return;

	const TArray<IPhysicsConstraintListener*> Listeners = ConstraintListeners;
	for ( const FBreak& Break : Breaks )
	{
		for ( IPhysicsConstraintListener* Listener : Listeners )
		{
			// 通知の中で、解除・破棄が起きていることがある。毎回確かめる
			if ( !ConstraintListeners.Contains( Listener ) ) continue;
			if ( !OwnsConstraint( Break.Constraint ) ) break;
			FRigidBody* BodyA = FindBody( Break.IdA );
			FRigidBody* BodyB = Break.bHasB ? FindBody( Break.IdB ) : nullptr;
			if ( BodyA == nullptr || ( Break.bHasB && BodyB == nullptr ) ) break;

			Listener->OnConstraintBroken( Break.Constraint, BodyA, BodyB, Break.Info );
		}
	}
}

void CPhysicsSystem::IgnoreCollision( const FRigidBody& BodyA, const FRigidBody& BodyB, bool bIgnore )
{
	if ( bIgnore ) ContactQueue->AddIgnoredPair( BodyA.GetBodyID(), BodyB.GetBodyID() );
	else ContactQueue->RemoveIgnoredPair( BodyA.GetBodyID(), BodyB.GetBodyID() );

	// Jolt は「ぶつからなかった」ことも組ごとに覚えていて、2 つが動かないうちは次のステップでも使い回す。
	// 組の扱いが変わったので、その覚えを捨てて、次のステップで調べ直させる（止めたなら今の接触が消え、戻したなら接触が生まれる）
	JPH::BodyInterface& BodyInterface = GetBodyInterface();
	BodyInterface.InvalidateContactCache( BodyA.GetBodyID() );
	BodyInterface.InvalidateContactCache( BodyB.GetBodyID() );
}

void CPhysicsSystem::DetachConstraint( FPhysicsConstraint& Constraint, bool bWakeBodies )
{
	if ( Constraint.Joint == nullptr ) return;

	// つないでいた剛体は、支えを失うので起こす
	if ( bWakeBodies ) Constraint.WakeBodies();
	if ( !Constraint.bCollideConnected && Constraint.BodyA != nullptr && Constraint.BodyB != nullptr ) IgnoreCollision( *Constraint.BodyA, *Constraint.BodyB, false );

	JoltSystem->RemoveConstraint( Constraint.Joint );
	Constraint.Joint->Release();
	Constraint.Joint = nullptr;
	Constraint.BodyA = nullptr;
	Constraint.BodyB = nullptr;
}

void CPhysicsSystem::DetachConstraintsOf( const FRigidBody* Body )
{
	for ( const TUniquePtr<FPhysicsConstraint>& Constraint : Constraints )
	{
		if ( Constraint->BodyA == Body || Constraint->BodyB == Body ) DetachConstraint( *Constraint, true );
	}
}
