#include "PhysicsBody.h"

#include "Library/Core/Log/Log.h"
#include "Library/Framework/Component/TransformComponent.h"
#include "Library/Framework/Render/Draw/DrawContext.h"
#include "Library/Framework/Scene/Scene.h"
#include "Library/Physics/PhysicsSubsystem.h"
#include "Source/Common/PhysicsView.h"

DEFINE_LOG_CATEGORY_STATIC( LogSamples, Log )

namespace
{
	// 世界のこの高さより下へ落ちたら、消す
	constexpr float32 KillY = -3.0f;
}

CPhysicsBody::CPhysicsBody( EShape InShape, const FVector3& InPosition, float32 InSize, const FColor32& InColor )
	: Shape( InShape )
	, StartPosition( InPosition )
	, Size( InSize )
	, Color( InColor )
{}

void CPhysicsBody::OnSpawn()
{
	Physics = GetScene()->GetSubsystem<CPhysicsSubsystem>();
	if ( Physics == nullptr )
	{
		LOG( LogSamples, Warning, "%s was spawned in a scene without physics (derive the scene from IPhysicsScene). Destroying it.", *GetDebugName() );
		Destroy();
		return;
	}
	FRigidBodySettings Settings;
	Settings.Restitution = 0.45f;
	Settings.Friction = 0.5f;
	FTransform Transform = FTransform::Identity;
	Transform.Position = StartPosition;
	const FCollisionShape CollisionShape = Shape == EShape::Ball ? FCollisionShape::Sphere( Size ) : FCollisionShape::Box( FVector3( Size, Size, Size ) );
	Body = Physics->CreateBody( CollisionShape, EBodyType::Dynamic, Transform, Settings );
	if ( Body == nullptr )
	{
		LOG( LogSamples, Error, "%s could not create its rigid body. Destroying it.", *GetDebugName() );
		Destroy();
		return;
	}
	// ゲームオブジェクトに戻れるように（レイキャストの結果は、剛体のポインターと、この値）
	Body->SetUserData( this );
	AddComponent<CTransformComponent>()->SetLocalTransform( Body->GetTransform() );
}

void CPhysicsBody::Tick( float32 DeltaTime )
{
	if ( Body == nullptr )
	{
		return;
	}
	if ( CTransformComponent* TransformComponent = GetComponent<CTransformComponent>() )
	{
		TransformComponent->SetLocalTransform( Body->GetTransform() );
	}
	if ( Body->GetPosition().y < KillY )
	{
		Destroy();
	}
}

void CPhysicsBody::Draw( FDrawContext& CommandList )
{
	if ( Body == nullptr )
	{
		return;
	}
	const FVector3 Position = Body->GetPosition();
	const FQuaternion Rotation = Body->GetRotation();
	const FColor32 Outline = bHighlighted ? FColor32::White : FColor32( 20, 22, 28 );
	const int32 Thickness = bHighlighted ? 3 : 1;
	if ( Shape == EShape::Ball )
	{
		const int32 CenterX = NPhysicsSampleView::ToScreenX( Position.x );
		const int32 CenterY = NPhysicsSampleView::ToScreenY( Position.y );
		const int32 Radius = static_cast<int32>( Size * NPhysicsSampleView::PixelsPerMeter );
		CommandList.Circle( CenterX, CenterY, Radius, Color, true );
		CommandList.Circle( CenterX, CenterY, Radius, Outline, false, Thickness );
		// 回っているのが分かるように、中心から縁へ線を 1 本
		const FVector3 Edge = Position + Rotation.Rotate( FVector3( Size, 0.0f, 0.0f ) );
		CommandList.Line( CenterX, CenterY, NPhysicsSampleView::ToScreenX( Edge.x ), NPhysicsSampleView::ToScreenY( Edge.y ), Outline );
		return;
	}
	// 箱: 4 つの角を、剛体の向きで回して、画面の座標にする
	const FVector3 Corners[4] =
	{
		Position + Rotation.Rotate( FVector3( -Size, -Size, 0.0f ) ),
		Position + Rotation.Rotate( FVector3( Size, -Size, 0.0f ) ),
		Position + Rotation.Rotate( FVector3( Size, Size, 0.0f ) ),
		Position + Rotation.Rotate( FVector3( -Size, Size, 0.0f ) ),
	};
	int32 X[4];
	int32 Y[4];
	for ( int32 Index = 0; Index < 4; ++Index )
	{
		X[Index] = NPhysicsSampleView::ToScreenX( Corners[Index].x );
		Y[Index] = NPhysicsSampleView::ToScreenY( Corners[Index].y );
	}
	CommandList.Triangle( X[0], Y[0], X[1], Y[1], X[2], Y[2], Color, true );
	CommandList.Triangle( X[0], Y[0], X[2], Y[2], X[3], Y[3], Color, true );
	for ( int32 Index = 0; Index < 4; ++Index )
	{
		const int32 Next = ( Index + 1 ) % 4;
		CommandList.Line( X[Index], Y[Index], X[Next], Y[Next], Outline, Thickness );
	}
}

void CPhysicsBody::OnDestroy()
{
	if ( Physics != nullptr && Body != nullptr )
	{
		Physics->DestroyBody( Body );
	}
	Body = nullptr;
}
