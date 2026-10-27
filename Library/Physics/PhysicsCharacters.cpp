#include "PhysicsSystem.h"

#include "Library/Core/Profiler/ProfilerScope.h"
#include "Library/Core/Types/Numeric.h"
#include "Library/Core/Types/Utility.h"
#include "Library/Physics/PhysicsCharacter.h"
#include "Library/Physics/PhysicsCharacterRules.h"
#include "Library/Physics/PhysicsInput.h"

#pragma push_macro( "min" )
#pragma push_macro( "max" )
#undef min
#undef max
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Physics/Body/Body.h>
#include <Jolt/Physics/Body/BodyFilter.h>
#include <Jolt/Physics/Character/CharacterVirtual.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>
#include <Jolt/Physics/Collision/ShapeFilter.h>
#pragma pop_macro( "max" )
#pragma pop_macro( "min" )

namespace
{
	// 足元を原点にした縦向きのカプセル。Height は足から頭の先まで（半球を含む）
	JPH::ShapeRefC MakeCharacterShape( float32 Radius, float32 Height )
	{
		const float32 HalfCylinder = ( Height - 2.0f * Radius ) * 0.5f;
		const JPH::RotatedTranslatedShapeSettings ShapeSettings( JPH::Vec3( 0.0f, Height * 0.5f, 0.0f ), JPH::Quat::sIdentity(), new JPH::CapsuleShape( HalfCylinder, Radius ) );
		const JPH::ShapeSettings::ShapeResult Result = ShapeSettings.Create();
		return Result.IsValid() ? Result.Get() : JPH::ShapeRefC();
	}

	// キャラクターが動くときに使う絞り込み。自分の剛体には当たらず、衝突レイヤーの表（CPhysicsSystem のフィルター）に従う
	struct FCharacterFilters
	{
		FCharacterFilters( const JPH::PhysicsSystem& System, JPH::ObjectLayer Layer, const JPH::BodyID& OwnBody )
			: BroadPhase( System.GetDefaultBroadPhaseLayerFilter( Layer ) )
			, ObjectLayer( System.GetDefaultLayerFilter( Layer ) )
			, Body( OwnBody )
		{}

		JPH::DefaultBroadPhaseLayerFilter BroadPhase;
		JPH::DefaultObjectLayerFilter     ObjectLayer;
		JPH::IgnoreSingleBodyFilter       Body;
		JPH::ShapeFilter                  Shape;
	};
}

bool CPhysicsSystem::IsCharacterBody( const FRigidBody* Body ) const
{
	if ( Body == nullptr ) return false;

	for ( const TUniquePtr<FPhysicsCharacter>& Character : Characters )
	{
		if ( Character->Body == Body ) return true;
	}
	return false;
}

FPhysicsCharacter* CPhysicsSystem::CreateCharacter( const FVector3& Position, const FCharacterSettings& CharacterSettings )
{
	if ( !bInitialized )
	{
		LOG( LogPhysics, Error, "CreateCharacter called before Init." );
		return nullptr;
	}
	if ( !NPhysicsInput::IsFiniteVector( Position ) )
	{
		LOG( LogPhysics, Error, "CreateCharacter called with a position that is not finite." );
		return nullptr;
	}
	const char* Reason = "";
	if ( !NCharacterRules::Validate( CharacterSettings, Reason ) )
	{
		LOG( LogPhysics, Error, "CreateCharacter called with invalid settings: %s", Reason );
		return nullptr;
	}

	const JPH::ShapeRefC Shape = MakeCharacterShape( CharacterSettings.Radius, CharacterSettings.Height );
	if ( Shape == nullptr )
	{
		LOG( LogPhysics, Error, "CreateCharacter failed to create the capsule shape." );
		return nullptr;
	}

	JPH::CharacterVirtualSettings Joint;
	Joint.mShape            = Shape;
	Joint.mUp               = JPH::Vec3::sAxisY();
	Joint.mSupportingVolume = JPH::Plane( JPH::Vec3::sAxisY(), -CharacterSettings.Radius );  // 足元の半球より下に触れた面だけが、キャラクターを支える
	Joint.mMaxSlopeAngle    = CharacterSettings.MaxSlopeAngle;
	Joint.mMass             = CharacterSettings.Mass;
	Joint.mMaxStrength      = CharacterSettings.MaxPushForce;
	if ( CharacterSettings.bCreateBody )
	{
		Joint.mInnerBodyShape = Shape;
		Joint.mInnerBodyLayer = NPhysics::ToPhysObjectLayer( EBodyType::Kinematic, CharacterSettings.CollisionLayer );
	}

	JPH::CharacterVirtual* Character = new JPH::CharacterVirtual( &Joint, NPhysics::ToPhysR( Position ), JPH::Quat::sIdentity(), 0, JoltSystem.Get() );
	if ( CharacterSettings.bCreateBody && Character->GetInnerBodyID().IsInvalid() )
	{
		delete Character;
		LOG( LogPhysics, Error, "Failed to create the character body (MaxBodies = %u reached?)", Settings.MaxBodies );
		return nullptr;
	}
	Character->AddRef();  // こちらで参照を 1 つ持つ（外すときに手放す）

	// 剛体は、CreateBody で作ったものと同じように、クエリや接触の通知で扱えるようにする
	FRigidBody* Body = nullptr;
	if ( CharacterSettings.bCreateBody )
	{
		const PhysBodyID BodyID = Character->GetInnerBodyID();
		auto BodyHandle = MakeUnique<FRigidBody>( GetBodyInterface(), BodyID, EBodyType::Kinematic, FVector3::One );
		Body = BodyHandle.Get();
		Bodies.Add( BodyID.GetIndexAndSequenceNumber(), MoveTemp( BodyHandle ) );
	}

	FPhysicsCharacter* Result = new FPhysicsCharacter( *this, CharacterSettings, Character, Body );
	Characters.Add( TUniquePtr<FPhysicsCharacter>( Result ) );
	RefreshCharacter( *Result );  // 作った場所で何かの上に立っているか、すぐ分かるようにする

	LOG( LogPhysics, Verbose, "Created a character (%d character(s) in the world)", Characters.Num() );
	return Result;
}

void CPhysicsSystem::DestroyCharacter( FPhysicsCharacter* Character )
{
	if ( Character == nullptr || !bInitialized ) return;

	const int32 Index = Characters.IndexOfByPredicate( [Character]( const TUniquePtr<FPhysicsCharacter>& Item ) { return Item.Get() == Character; } );
	if ( Index == INDEX_NONE )
	{
		LOG( LogPhysics, Warning, "DestroyCharacter called with an unknown character." );
		return;
	}

	DetachCharacter( *Character );
	Characters.RemoveAtSwap( Index );
	LOG( LogPhysics, Verbose, "Destroyed a character (%d character(s) left)", Characters.Num() );
}

void CPhysicsSystem::DestroyAllCharacters()
{
	if ( !bInitialized ) return;

	if ( !Characters.IsEmpty() ) LOG( LogPhysics, Verbose, "Destroying all %d character(s)", Characters.Num() );
	for ( const TUniquePtr<FPhysicsCharacter>& Character : Characters ) DetachCharacter( *Character );
	Characters.Empty();
}

int32 CPhysicsSystem::GetCharacterCount() const
{
	return Characters.Num();
}

void CPhysicsSystem::DetachCharacter( FPhysicsCharacter& Character )
{
	if ( Character.Character == nullptr ) return;

	Character.LastPosition = NPhysics::FromPhys( Character.Character->GetPosition() );
	if ( Character.Body != nullptr )
	{
		// 剛体につないだ関節を、剛体が無くなる前に外す。剛体の入れ物を先に手放し、Jolt の剛体はキャラクターが一緒に片付ける
		DetachConstraintsOf( Character.Body );
		Bodies.Remove( Character.Body->GetBodyID().GetIndexAndSequenceNumber() );
		Character.Body = nullptr;
	}

	Character.Character->Release();  // キャラクターと、その剛体が壊れる
	Character.Character = nullptr;
}

void CPhysicsSystem::RefreshCharacter( FPhysicsCharacter& Character )
{
	if ( Character.Character == nullptr ) return;

	const JPH::BodyID OwnBody = Character.Character->GetInnerBodyID();
	const FCharacterFilters Filters( *JoltSystem, NPhysics::ToPhysObjectLayer( EBodyType::Kinematic, Character.Settings.CollisionLayer ), OwnBody );
	Character.Character->RefreshContacts( Filters.BroadPhase, Filters.ObjectLayer, Filters.Body, Filters.Shape, *TempAllocator );
}

bool CPhysicsSystem::ChangeCharacterHeight( FPhysicsCharacter& Character, float32 Height )
{
	const JPH::ShapeRefC Shape = MakeCharacterShape( Character.Settings.Radius, Height );
	if ( Shape == nullptr ) return false;

	const JPH::BodyID OwnBody = Character.Character->GetInnerBodyID();
	const FCharacterFilters Filters( *JoltSystem, NPhysics::ToPhysObjectLayer( EBodyType::Kinematic, Character.Settings.CollisionLayer ), OwnBody );

	// 新しい形が、他の物体に（許す深さを超えて）めり込むなら、変えない
	const float32 MaxPenetration = 1.5f * JoltSystem->GetPhysicsSettings().mPenetrationSlop;
	if ( !Character.Character->SetShape( Shape, MaxPenetration, Filters.BroadPhase, Filters.ObjectLayer, Filters.Body, Filters.Shape, *TempAllocator ) ) return false;

	Character.Character->SetInnerBodyShape( Shape );
	Character.Settings.Height = Height;
	return true;
}

void CPhysicsSystem::UpdateCharacters( float32 DeltaTime )
{
	for ( const TUniquePtr<FPhysicsCharacter>& Item : Characters )
	{
		FPhysicsCharacter& Character = *Item;
		if ( Character.Character == nullptr ) continue;  // 外れている

		JPH::CharacterVirtual& Joint = *Character.Character;

		// 水平は歩く速さ、上下は「地面にいれば床の速さ（動く床に乗る）、跳ぶなら初速を足す。空中なら今の速さ」に重力を足す
		const JPH::Vec3 Gravity = JPH::Vec3( 0.0f, Settings.Gravity.y * Character.Settings.GravityFactor, 0.0f );
		const JPH::Vec3 CurrentVertical( 0.0f, Joint.GetLinearVelocity().GetY(), 0.0f );
		const JPH::Vec3 GroundVelocity = Joint.GetGroundVelocity();
		const bool bMovingTowardsGround = ( CurrentVertical.GetY() - GroundVelocity.GetY() ) < 0.1f;

		JPH::Vec3 NewVelocity = CurrentVertical;
		if ( Joint.GetGroundState() == JPH::CharacterBase::EGroundState::OnGround && bMovingTowardsGround )
		{
			NewVelocity = GroundVelocity;
			if ( Character.bJumpRequested ) NewVelocity += Character.JumpSpeed * JPH::Vec3::sAxisY();
		}
		Character.bJumpRequested = false;
		NewVelocity += Gravity * DeltaTime;
		NewVelocity += JPH::Vec3( Character.MoveVelocity.x, 0.0f, Character.MoveVelocity.z );
		Joint.SetLinearVelocity( NewVelocity );

		JPH::CharacterVirtual::ExtendedUpdateSettings Extended;
		Extended.mStickToFloorStepDown = JPH::Vec3( 0.0f, -Character.Settings.StickToFloorDistance, 0.0f );
		Extended.mWalkStairsStepUp     = JPH::Vec3( 0.0f, Character.Settings.StepHeight, 0.0f );

		const FCharacterFilters Filters( *JoltSystem, NPhysics::ToPhysObjectLayer( EBodyType::Kinematic, Character.Settings.CollisionLayer ), Joint.GetInnerBodyID() );
		Joint.ExtendedUpdate( DeltaTime, Gravity, Extended, Filters.BroadPhase, Filters.ObjectLayer, Filters.Body, Filters.Shape, *TempAllocator );
	}
}
