#include "PhysicsSystem.h"

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <thread>

#include "Library/Core/Profiler/ProfilerScope.h"
#include "Library/Physics/ContactListener.h"

#pragma push_macro( "min" )
#pragma push_macro( "max" )
#undef min
#undef max
#include <Jolt/RegisterTypes.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Physics/PhysicsSettings.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#pragma pop_macro( "max" )
#pragma pop_macro( "min" )

DEFINE_LOG_CATEGORY( LogPhysics )

namespace
{
	//-------------------------------------------------------------------------
	// Jolt のログ / アサートを Log に流す
	//-------------------------------------------------------------------------
	void JoltTrace( const char* Format, ... )
	{
		char Buffer[1024];
		va_list Args;
		va_start( Args, Format );
		std::vsnprintf( Buffer, sizeof( Buffer ), Format, Args );
		va_end( Args );
		CLogManager::Get().Log( LogPhysics, ELogVerbosity::Log, "Jolt", 0, "Jolt", "%s", Buffer );
	}

#ifdef JPH_ENABLE_ASSERTS
	bool JoltAssertFailed( const char* Expression, const char* Message, const char* File, JPH::uint Line )
	{
		CLogManager::Get().Log( LogPhysics, ELogVerbosity::Error, File, static_cast<int32>( Line ), "Jolt",
								"Assert failed: %s %s", Expression, Message ? Message : "" );
		return true;  // ブレークする
	}
#endif

	const char* BodyTypeToString( EBodyType Type )
	{
		switch ( Type )
		{
		case EBodyType::Static:    return "static";
		case EBodyType::Kinematic: return "kinematic";
		case EBodyType::Dynamic:   return "dynamic";
		}
		return "unknown";
	}

	//-------------------------------------------------------------------------
	// Jolt のグローバル初期化（プロセスで 1 回）
	//   ワールドが複数あっても、最初の Init で登録し最後の Shutdown で解除する
	//-------------------------------------------------------------------------
	int32 GJoltUserCount = 0;

	void AcquireJolt()
	{
		if ( GJoltUserCount++ > 0 ) return;

		JPH::RegisterDefaultAllocator();
		JPH::Trace = JoltTrace;
		JPH_IF_ENABLE_ASSERTS( JPH::AssertFailed = JoltAssertFailed; )
		JPH::Factory::sInstance = new JPH::Factory();
		JPH::RegisterTypes();
	}

	void ReleaseJolt()
	{
		if ( --GJoltUserCount > 0 ) return;

		JPH::UnregisterTypes();
		delete JPH::Factory::sInstance;
		JPH::Factory::sInstance = nullptr;
	}

	//-------------------------------------------------------------------------
	// レイヤーフィルター
	//-------------------------------------------------------------------------
	class FBroadPhaseLayerInterface final : public JPH::BroadPhaseLayerInterface
	{
	public:
		JPH::uint GetNumBroadPhaseLayers() const override { return NPhysicsBroadPhaseLayers::Count; }

		JPH::BroadPhaseLayer GetBroadPhaseLayer( JPH::ObjectLayer Layer ) const override
		{
			return Layer == NPhysicsLayers::NonMoving ? NPhysicsBroadPhaseLayers::NonMoving : NPhysicsBroadPhaseLayers::Moving;
		}

#if defined( JPH_EXTERNAL_PROFILE ) || defined( JPH_PROFILE_ENABLED )
		const char* GetBroadPhaseLayerName( JPH::BroadPhaseLayer Layer ) const override
		{
			return Layer == NPhysicsBroadPhaseLayers::NonMoving ? "NonMoving" : "Moving";
		}
#endif
	};

	class FObjectVsBroadPhaseLayerFilter final : public JPH::ObjectVsBroadPhaseLayerFilter
	{
	public:
		bool ShouldCollide( JPH::ObjectLayer Layer1, JPH::BroadPhaseLayer Layer2 ) const override
		{
			if ( Layer1 == NPhysicsLayers::NonMoving ) return Layer2 == NPhysicsBroadPhaseLayers::Moving;
			return true;
		}
	};

	class FObjectLayerPairFilter final : public JPH::ObjectLayerPairFilter
	{
	public:
		bool ShouldCollide( JPH::ObjectLayer Layer1, JPH::ObjectLayer Layer2 ) const override
		{
			if ( Layer1 == NPhysicsLayers::NonMoving ) return Layer2 == NPhysicsLayers::Moving;
			return true;
		}
	};
}

struct CPhysicsSystem::FLayerFilters
{
	FBroadPhaseLayerInterface      BroadPhaseLayer;
	FObjectVsBroadPhaseLayerFilter ObjectVsBroadPhase;
	FObjectLayerPairFilter         ObjectPair;
};

#pragma region Lifecycle

CPhysicsSystem::CPhysicsSystem() = default;

CPhysicsSystem::~CPhysicsSystem()
{
	Shutdown();
}

TResult<void> CPhysicsSystem::Init( const FPhysicsSettings& InSettings )
{
	if ( bInitialized ) return TResult<void>::Err( "PhysicsSystem is already initialized" );
	if ( InSettings.FixedDeltaTime <= 0.0f ) return TResult<void>::Err( "FixedDeltaTime must be positive" );

	Settings    = InSettings;
	Accumulator = 0.0f;

	AcquireJolt();

	const int32 NumThreads = Settings.NumThreads >= 0
		? Settings.NumThreads
		: std::max( 1, static_cast<int32>( std::thread::hardware_concurrency() ) - 1 );

	TempAllocator = MakeUnique<JPH::TempAllocatorImpl>( Settings.TempAllocatorSize );
	JobSystem     = MakeUnique<JPH::JobSystemThreadPool>( JPH::cMaxPhysicsJobs, JPH::cMaxPhysicsBarriers, NumThreads );
	LayerFilters  = MakeUnique<FLayerFilters>();
	ContactQueue  = MakeUnique<CContactEventQueue>();
	ContactQueue->SetEnabled( !ContactListeners.IsEmpty() );

	JoltSystem = MakeUnique<JPH::PhysicsSystem>();
	JoltSystem->Init( Settings.MaxBodies, 0, Settings.MaxBodyPairs, Settings.MaxContactConstraints,
					  LayerFilters->BroadPhaseLayer, LayerFilters->ObjectVsBroadPhase, LayerFilters->ObjectPair );
	JoltSystem->SetContactListener( ContactQueue.Get() );
	JoltSystem->SetGravity( NPhysics::ToPhys( Settings.Gravity ) );

	bInitialized = true;
	LOG( LogPhysics, Log, "Initialized (threads: %d, max bodies: %u)", NumThreads, Settings.MaxBodies );
	return TResult<void>::Ok();
}

void CPhysicsSystem::Shutdown()
{
	if ( !bInitialized ) return;

	DestroyAllBodies();

	JoltSystem.Reset();
	ContactQueue.Reset();
	LayerFilters.Reset();
	JobSystem.Reset();
	TempAllocator.Reset();

	ReleaseJolt();

	bInitialized = false;
	LOG( LogPhysics, Log, "Shutdown" );
}

void CPhysicsSystem::Update( float32 DeltaTime )
{
	if ( !bInitialized ) return;

	// 処理落ち時に無限にステップを重ねないよう上限を設ける
	const float32 MaxAccumulated = Settings.FixedDeltaTime * static_cast<float32>( Settings.MaxSubSteps );
	Accumulator = std::min( Accumulator + std::max( DeltaTime, 0.0f ), MaxAccumulated );

	while ( Accumulator >= Settings.FixedDeltaTime )
	{
		Step( Settings.FixedDeltaTime );
		Accumulator -= Settings.FixedDeltaTime;
	}
}

float32 CPhysicsSystem::GetInterpolationAlpha() const
{
	return Settings.FixedDeltaTime > 0.0f ? Accumulator / Settings.FixedDeltaTime : 0.0f;
}

void CPhysicsSystem::Step( float32 DeltaTime )
{
	JPH::EPhysicsUpdateError Error;
	{
		PROFILE_SCOPE_CAT( "Physics", "Jolt.Update" );
		Error = JoltSystem->Update( DeltaTime, Settings.CollisionSteps, TempAllocator.Get(), JobSystem.Get() );
	}
	if ( Error != JPH::EPhysicsUpdateError::None )
	{
		LOG( LogPhysics, Warning, "Physics update error (0x%x). Increase MaxBodyPairs / MaxContactConstraints.", static_cast<uint32>( Error ) );
	}

	if ( !ContactListeners.IsEmpty() )
	{
		// コールバック内での Add/Remove に備えてコピーを渡す
		const TArray<CContactListener*> Listeners = ContactListeners;
		PROFILE_SCOPE_CAT( "Physics", "DispatchContacts" );
		ContactQueue->Dispatch( *this, Listeners );
	}
}

#pragma endregion

#pragma region Body

FRigidBody* CPhysicsSystem::CreateBody( const FCollisionShape& Shape, EBodyType Type, const FTransform& Transform, const FRigidBodySettings& BodySettings )
{
	if ( !bInitialized )
	{
		LOG( LogPhysics, Error, "CreateBody called before Init." );
		return nullptr;
	}
	if ( !Shape.IsValid() )
	{
		LOG( LogPhysics, Error, "CreateBody called with an invalid shape." );
		return nullptr;
	}

	// スケールを形状に焼き込む
	FCollisionShape FinalShape = Shape;
	FVector3        FinalScale = FVector3::One;
	if ( !Transform.Scale.Equals( FVector3::One ) )
	{
		auto Scaled = Shape.Scaled( Transform.Scale );
		if ( Scaled.IsOk() )
		{
			FinalShape = std::move( Scaled ).Unwrap();
			FinalScale = Transform.Scale;
		}
		else
		{
			LOG( LogPhysics, Warning, "Failed to scale shape, using unscaled shape: %s", Scaled.UnwrapErr().c_str() );
		}
	}

	JPH::BodyCreationSettings Creation(
		FinalShape.GetShape().GetPtr(),
		NPhysics::ToPhysR( Transform.Position ),
		NPhysics::ToPhys( Transform.Rotation.Normalized() ),
		NPhysics::ToPhysMotionType( Type ),
		NPhysics::ToPhysObjectLayer( Type ) );

	Creation.mFriction       = BodySettings.Friction;
	Creation.mRestitution    = BodySettings.Restitution;
	Creation.mLinearDamping  = BodySettings.LinearDamping;
	Creation.mAngularDamping = BodySettings.AngularDamping;
	Creation.mGravityFactor  = BodySettings.GravityFactor;
	Creation.mIsSensor       = BodySettings.bIsSensor;
	Creation.mAllowSleeping  = BodySettings.bAllowSleeping;
	Creation.mMotionQuality  = BodySettings.bContinuousCollision ? JPH::EMotionQuality::LinearCast : JPH::EMotionQuality::Discrete;
	if ( Type == EBodyType::Dynamic && BodySettings.Mass > 0.0f )
	{
		Creation.mOverrideMassProperties       = JPH::EOverrideMassProperties::CalculateInertia;
		Creation.mMassPropertiesOverride.mMass = BodySettings.Mass;
	}

	JPH::BodyInterface& BodyInterface = GetBodyInterface();
	const JPH::EActivation Activation = ( BodySettings.bActivate && Type != EBodyType::Static ) ? JPH::EActivation::Activate : JPH::EActivation::DontActivate;
	const PhysBodyID BodyID = BodyInterface.CreateAndAddBody( Creation, Activation );
	if ( BodyID.IsInvalid() )
	{
		LOG( LogPhysics, Error, "Failed to create body (MaxBodies = %u reached?)", Settings.MaxBodies );
		return nullptr;
	}

	auto Body = MakeUnique<FRigidBody>( BodyInterface, BodyID, Type, FinalScale );
	FRigidBody* Raw = Body.Get();
	Bodies.Add( BodyID.GetIndexAndSequenceNumber(), std::move( Body ) );
	LOG( LogPhysics, Verbose, "Created %s body #%u (%d body(ies) in the world)", BodyTypeToString( Type ), BodyID.GetIndex(), Bodies.Num() );
	return Raw;
}

void CPhysicsSystem::DestroyBody( FRigidBody* Body )
{
	if ( !Body || !bInitialized ) return;

	const PhysBodyID BodyID = Body->GetBodyID();
	const TUniquePtr<FRigidBody>* Found = Bodies.Find( BodyID.GetIndexAndSequenceNumber() );
	if ( !Found || Found->Get() != Body )
	{
		LOG( LogPhysics, Warning, "DestroyBody called with an unknown body." );
		return;
	}

	JPH::BodyInterface& BodyInterface = GetBodyInterface();
	BodyInterface.RemoveBody( BodyID );
	BodyInterface.DestroyBody( BodyID );
	Bodies.Remove( BodyID.GetIndexAndSequenceNumber() );
	LOG( LogPhysics, Verbose, "Destroyed body #%u (%d body(ies) left)", BodyID.GetIndex(), Bodies.Num() );
}

void CPhysicsSystem::DestroyAllBodies()
{
	if ( !bInitialized ) return;

	if ( !Bodies.IsEmpty() ) LOG( LogPhysics, Verbose, "Destroying all %d body(ies)", Bodies.Num() );
	JPH::BodyInterface& BodyInterface = GetBodyInterface();
	for ( auto& [Key, Body] : Bodies )
	{
		const PhysBodyID BodyID = Body->GetBodyID();
		BodyInterface.RemoveBody( BodyID );
		BodyInterface.DestroyBody( BodyID );
	}
	Bodies.Empty();
	ContactQueue->Clear();
}

FRigidBody* CPhysicsSystem::FindBody( const PhysBodyID& BodyID ) const
{
	const TUniquePtr<FRigidBody>* Found = Bodies.Find( BodyID.GetIndexAndSequenceNumber() );
	return Found ? Found->Get() : nullptr;
}

void CPhysicsSystem::OptimizeBroadPhase()
{
	if ( !bInitialized )
	{
		LOG( LogPhysics, Warning, "OptimizeBroadPhase called before Init." );
		return;
	}
	JoltSystem->OptimizeBroadPhase();
	LOG( LogPhysics, Verbose, "Broad phase optimized (%d body(ies))", Bodies.Num() );
}

#pragma endregion

#pragma region ContactListener

void CPhysicsSystem::AddContactListener( CContactListener* Listener )
{
	if ( !Listener ) return;
	if ( ContactListeners.Contains( Listener ) ) return;

	ContactListeners.Add( Listener );
	if ( ContactQueue ) ContactQueue->SetEnabled( true );
	LOG( LogPhysics, Verbose, "Contact listener added (%d listener(s))", ContactListeners.Num() );
}

void CPhysicsSystem::RemoveContactListener( CContactListener* Listener )
{
	if ( ContactListeners.Remove( Listener ) > 0 ) LOG( LogPhysics, Verbose, "Contact listener removed (%d listener(s) left)", ContactListeners.Num() );
	if ( ContactQueue && ContactListeners.IsEmpty() )
	{
		ContactQueue->SetEnabled( false );
		ContactQueue->Clear();
	}
}

#pragma endregion

#pragma region World

void CPhysicsSystem::SetGravity( const FVector3& Gravity )
{
	Settings.Gravity = Gravity;
	if ( bInitialized ) JoltSystem->SetGravity( NPhysics::ToPhys( Gravity ) );
	LOG( LogPhysics, Verbose, "Gravity set to (%s)", *Gravity.ToString() );
}

FVector3 CPhysicsSystem::GetGravity() const
{
	return Settings.Gravity;
}

JPH::PhysicsSystem& CPhysicsSystem::GetJoltSystem()
{
	return *JoltSystem;
}

JPH::BodyInterface& CPhysicsSystem::GetBodyInterface()
{
	return JoltSystem->GetBodyInterface();
}

#pragma endregion
