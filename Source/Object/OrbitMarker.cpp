#include "OrbitMarker.h"

#include "Library/Core/Interfaces/Inspector/IPropertyInspector.h"
#include "Library/Framework/Component/TransformComponent.h"
#include "Library/Platform/DxLib/DxLibInclude.h"
#include "Source/Object/PhysicsObjects.h"

COrbitMarker::COrbitMarker( const FVector3& InLocalPosition, float32 InSpinDegreesPerSecond, int32 InHalfSizePixels, const FColor32& InColor )
	: InitialLocalPosition( InLocalPosition )
	, SpinDegreesPerSecond( InSpinDegreesPerSecond )
	, HalfSizePixels( InHalfSizePixels )
	, Color( InColor )
{}

void COrbitMarker::OnSpawn()
{
	Transform = AddComponent<CTransformComponent>();
	Transform->SetLocalPosition( InitialLocalPosition );
}

void COrbitMarker::Tick( float32 DeltaTime )
{
	if ( SpinDegreesPerSecond == 0.0f ) return;

	const FQuaternion Spin = FQuaternion::FromAxisAngle( FVector3::Forward, NMath::ToRadians( SpinDegreesPerSecond * DeltaTime ) );
	Transform->SetLocalRotation( ( Spin * Transform->GetLocalRotation() ).Normalized() );
}

void COrbitMarker::Draw()
{
	const FVector3 Position = Transform->GetWorldPosition();
	const int32 X = NSampleView::ToScreenX( Position.x );
	const int32 Y = NSampleView::ToScreenY( Position.y );
	DrawBox( X - HalfSizePixels, Y - HalfSizePixels, X + HalfSizePixels, Y + HalfSizePixels, Color.ToDxColor(), TRUE );
}

void COrbitMarker::InspectProperties( IPropertyInspector& Inspector )
{
	CGameObject::InspectProperties( Inspector );
	Inspector.Float( "Spin (deg/s)", SpinDegreesPerSecond );
}
