#include "SensorZone.h"

#include "Library/Core/Log/Log.h"
#include "Library/Framework/Render/Draw/DrawContext.h"
#include "Library/Framework/Scene/Scene.h"
#include "Library/Physics/PhysicsSubsystem.h"
#include "Source/Common/PhysicsView.h"

DEFINE_LOG_CATEGORY_STATIC( LogSamples, Log )

CSensorZone::CSensorZone( const FVector3& InCenter, const FVector3& InHalfExtents )
	: Center( InCenter )
	, HalfExtents( InHalfExtents )
{}

void CSensorZone::OnSpawn()
{
	Physics = GetScene()->GetSubsystem<CPhysicsSubsystem>();
	if ( Physics == nullptr )
	{
		LOG( LogSamples, Warning, "%s was placed in a scene without physics (derive the scene from IPhysicsScene).", *GetDebugName() );
		return;
	}
	FRigidBodySettings Settings;
	Settings.bIsSensor = true;
	FTransform Transform = FTransform::Identity;
	Transform.Position = Center;
	Body = Physics->CreateBody( FCollisionShape::Box( HalfExtents ), EBodyType::Static, Transform, Settings );

	// この剛体が絡む通知だけを受け取る
	FPhysicsContactFilter Filter;
	Filter.Body = Body;
	Physics->AddContactListener( this, Filter );
}

void CSensorZone::Draw( FDrawContext& CommandList )
{
	const int32 Left = NPhysicsSampleView::ToScreenX( Center.x - HalfExtents.x );
	const int32 Top = NPhysicsSampleView::ToScreenY( Center.y + HalfExtents.y );
	const int32 Right = NPhysicsSampleView::ToScreenX( Center.x + HalfExtents.x );
	const int32 Bottom = NPhysicsSampleView::ToScreenY( Center.y - HalfExtents.y );
	const FColor32 Color = NumInside > 0 ? FColor32( 90, 220, 120 ) : FColor32( 70, 110, 90 );
	CommandList.Box( Left, Top, Right, Bottom, Color, false, 2 );
}

void CSensorZone::OnDestroy()
{
	if ( Physics != nullptr )
	{
		Physics->RemoveContactListener( this );
		if ( Body != nullptr )
		{
			Physics->DestroyBody( Body );
		}
	}
	Body = nullptr;
}

bool CSensorZone::IsMine( const FRigidBody* Other ) const
{
	return Body != nullptr && Other == Body;
}

void CSensorZone::OnBodyContactBegin( FRigidBody* BodyA, FRigidBody* BodyB, const FContactInfo& Info )
{
	if ( IsMine( BodyA ) || IsMine( BodyB ) )
	{
		++NumInside;
		++NumEntered;
	}
}

void CSensorZone::OnBodyContactEnd( FRigidBody* BodyA, FRigidBody* BodyB )
{
	if ( ( IsMine( BodyA ) || IsMine( BodyB ) ) && NumInside > 0 )
	{
		--NumInside;
	}
}
