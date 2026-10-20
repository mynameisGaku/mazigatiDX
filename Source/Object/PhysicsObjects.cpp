#include "PhysicsObjects.h"

#include "Library/Core/Interfaces/Platform/IPlatform.h"
#include "Library/Core/Log/Log.h"
#include "Library/Framework/Component/TransformComponent.h"
#include "Library/Framework/Engine/Engine.h"
#include "Library/Framework/Scene/Scene.h"
#include "Library/Physics/PhysicsSubsystem.h"
#include "Library/Platform/DxLib/DxLibInclude.h"

DEFINE_LOG_CATEGORY_STATIC( LogPhysicsObjects, Log )

namespace
{
	// 物理ワールドより下に落ちたら消す
	constexpr float32 KillY = -5.0f;

	FTransform MakeTransform( const FVector3& Position )
	{
		FTransform Transform = FTransform::Identity;
		Transform.Position = Position;
		return Transform;
	}
}

int32 NSampleView::ToScreenX( float32 X )
{
	return static_cast<int32>( X * PixelsPerMeter );
}

int32 NSampleView::ToScreenY( float32 Y )
{
	const IPlatform* Platform = GetAppSubsystem<IPlatform>();
	const int32 ScreenHeight = Platform ? Platform->GetScreenHeight() : 0;
	return ScreenHeight - static_cast<int32>( Y * PixelsPerMeter );
}

#pragma region CPhysicsBall

CPhysicsBall::CPhysicsBall( const FVector3& InStartPosition, float32 InRadius, const FColor32& InColor, float32 InLifetime )
	: StartPosition( InStartPosition )
	, Radius( InRadius )
	, Color( InColor )
	, Lifetime( InLifetime )
{}

void CPhysicsBall::OnSpawn()
{
	Physics = GetScene()->GetSubsystem<CPhysicsSubsystem>();
	if ( Physics == nullptr )
	{
		// 物理の無いシーンに置かれた
		LOG( LogPhysicsObjects, Warning, "%s was spawned in a scene without physics (derive the scene from IPhysicsScene). Destroying it.", *GetDebugName() );
		Destroy();
		return;
	}

	FRigidBodySettings Settings;
	Settings.Restitution = 0.6f;
	Body = Physics->CreateBody( FCollisionShape::Sphere( Radius ), EBodyType::Dynamic, MakeTransform( StartPosition ), Settings );
	if ( Body == nullptr )
	{
		LOG( LogPhysicsObjects, Error, "%s could not create its rigid body. Destroying it.", *GetDebugName() );
		Destroy();
		return;
	}

	Transform = AddComponent<CTransformComponent>();
	Transform->SetLocalTransform( Body->GetTransform() );
}

void CPhysicsBall::Tick( float32 DeltaTime )
{
	Transform->SetLocalTransform( Body->GetTransform() );

	Age += DeltaTime;
	if ( Age >= Lifetime || Body->GetPosition().y < KillY ) Destroy();
}

void CPhysicsBall::Draw()
{
	if ( Body == nullptr ) return;

	const FVector3 Position = Body->GetPosition();
	DrawCircle( NSampleView::ToScreenX( Position.x ), NSampleView::ToScreenY( Position.y ),
				static_cast<int>( Radius * NSampleView::PixelsPerMeter ), Color.ToDxColor(), TRUE );
}

void CPhysicsBall::OnDestroy()
{
	if ( Physics != nullptr && Body != nullptr ) Physics->DestroyBody( Body );
	Body = nullptr;
}

#pragma endregion

#pragma region CPhysicsFloor

CPhysicsFloor::CPhysicsFloor( const FVector3& InCenter, const FVector3& InHalfExtents )
	: Center( InCenter )
	, HalfExtents( InHalfExtents )
{}

void CPhysicsFloor::OnSpawn()
{
	Physics = GetScene()->GetSubsystem<CPhysicsSubsystem>();
	if ( Physics == nullptr )
	{
		LOG( LogPhysicsObjects, Warning, "%s was spawned in a scene without physics (derive the scene from IPhysicsScene). Destroying it.", *GetDebugName() );
		Destroy();
		return;
	}
	Body = Physics->CreateBody( FCollisionShape::Box( HalfExtents ), EBodyType::Static, MakeTransform( Center ) );
	if ( Body == nullptr ) LOG( LogPhysicsObjects, Error, "%s could not create its rigid body.", *GetDebugName() );
}

void CPhysicsFloor::Draw()
{
	DrawBox( NSampleView::ToScreenX( Center.x - HalfExtents.x ), NSampleView::ToScreenY( Center.y + HalfExtents.y ),
			 NSampleView::ToScreenX( Center.x + HalfExtents.x ), NSampleView::ToScreenY( Center.y - HalfExtents.y ),
			 FColor32::Gray.ToDxColor(), TRUE );
}

void CPhysicsFloor::OnDestroy()
{
	if ( Physics != nullptr && Body != nullptr ) Physics->DestroyBody( Body );
	Body = nullptr;
}

#pragma endregion
