#include "StaticBlock.h"

#include "Library/Core/Log/Log.h"
#include "Library/Framework/Render/Draw/DrawContext.h"
#include "Library/Framework/Scene/Scene.h"
#include "Library/Physics/PhysicsSubsystem.h"
#include "Source/Common/PhysicsView.h"

DEFINE_LOG_CATEGORY_STATIC( LogSamples, Log )

CStaticBlock::CStaticBlock( const FVector3& InCenter, const FVector3& InHalfExtents )
	: Center( InCenter )
	, HalfExtents( InHalfExtents )
{}

void CStaticBlock::OnSpawn()
{
	Physics = GetScene()->GetSubsystem<CPhysicsSubsystem>();
	if ( Physics == nullptr )
	{
		LOG( LogSamples, Warning, "%s was placed in a scene without physics (derive the scene from IPhysicsScene).", *GetDebugName() );
		return;
	}
	FTransform Transform = FTransform::Identity;
	Transform.Position = Center;
	Body = Physics->CreateBody( FCollisionShape::Box( HalfExtents ), EBodyType::Static, Transform );
}

void CStaticBlock::Draw( FDrawContext& CommandList )
{
	CommandList.Box( NPhysicsSampleView::ToScreenX( Center.x - HalfExtents.x ), NPhysicsSampleView::ToScreenY( Center.y + HalfExtents.y ),
					 NPhysicsSampleView::ToScreenX( Center.x + HalfExtents.x ), NPhysicsSampleView::ToScreenY( Center.y - HalfExtents.y ), FColor32( 90, 96, 110 ) );
}

void CStaticBlock::OnDestroy()
{
	if ( Physics != nullptr && Body != nullptr )
	{
		Physics->DestroyBody( Body );
	}
	Body = nullptr;
}
