#include "RopeJoint.h"

#include "Library/Core/Log/Log.h"
#include "Library/Framework/Render/Draw/DrawContext.h"
#include "Library/Framework/Scene/Scene.h"
#include "Library/Physics/Body/RigidBody.h"
#include "Library/Physics/Constraint/PhysicsConstraint.h"
#include "Library/Physics/PhysicsSubsystem.h"
#include "Source/Common/PhysicsView.h"
#include "Source/Physics/PhysicsBody.h"

DEFINE_LOG_CATEGORY_STATIC( LogSamples, Log )

CRopeJoint::CRopeJoint( const TSharedPtr<CPhysicsBody>& InWeight, const FVector3& InHook, float32 InLength )
	: Weight( InWeight )
	, Hook( InHook )
	, Length( InLength )
{}

void CRopeJoint::OnSpawn()
{
	Physics = GetScene()->GetSubsystem<CPhysicsSubsystem>();
	TSharedPtr<CPhysicsBody> WeightObject = Weight.Pin();
	if ( Physics == nullptr || WeightObject == nullptr || WeightObject->GetBody() == nullptr )
	{
		LOG( LogSamples, Warning, "%s needs a physics scene and a weight with a rigid body.", *GetDebugName() );
		return;
	}
	// BodyB が nullptr なら、物体を世界に固定する。2 つ目の点（AnchorB）は、世界の点。長さは 0（ゆるむ）から Length まで
	Constraint = Physics->CreateConstraint( WeightObject->GetBody(), nullptr, FConstraintSettings::Distance( WeightObject->GetBody()->GetPosition(), Hook, 0.0f, Length ) );
}

void CRopeJoint::Draw( FDrawContext& CommandList )
{
	const int32 HookX = NPhysicsSampleView::ToScreenX( Hook.x );
	const int32 HookY = NPhysicsSampleView::ToScreenY( Hook.y );
	CommandList.Circle( HookX, HookY, 5, FColor32::Gray, true );
	TSharedPtr<CPhysicsBody> WeightObject = Weight.Pin();
	if ( WeightObject == nullptr || WeightObject->GetBody() == nullptr )
	{
		return;
	}
	const FVector3 Position = WeightObject->GetBody()->GetPosition();
	CommandList.Line( HookX, HookY, NPhysicsSampleView::ToScreenX( Position.x ), NPhysicsSampleView::ToScreenY( Position.y ), FColor32( 200, 170, 110 ), 2 );
}

void CRopeJoint::OnDestroy()
{
	if ( Physics != nullptr && Constraint != nullptr )
	{
		Physics->DestroyConstraint( Constraint );
	}
	Constraint = nullptr;
}
