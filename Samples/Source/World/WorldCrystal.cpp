#include "WorldCrystal.h"

#include "Library/Framework/Component/TransformComponent.h"

CWorldCrystal::CWorldCrystal( float32 InDegreesPerSecond )
	: DegreesPerSecond( InDegreesPerSecond )
{}

void CWorldCrystal::Tick( float32 DeltaTime )
{
	CGameObject* Parent = GetParent();
	CTransformComponent* Transform = Parent != nullptr ? Parent->GetComponent<CTransformComponent>() : nullptr;
	if ( Transform == nullptr )
	{
		return;
	}
	if ( !bHasBase )
	{
		BaseRotation = Transform->GetLocalRotation();
		bHasBase = true;
	}
	Angle += NMath::ToRadians( DegreesPerSecond ) * DeltaTime;
	Transform->SetLocalRotation( FQuaternion::FromAxisAngle( FVector3::Up, Angle ) * BaseRotation );
}
