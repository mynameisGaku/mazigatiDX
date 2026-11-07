#include "PhysicsView.h"

#include "Library/Core/Interfaces/Platform/IPlatform.h"
#include "Library/Framework/Engine/Engine.h"

namespace
{
	int32 GetScreenHeight()
	{
		const IPlatform* Platform = GetAppSubsystem<IPlatform>();
		return Platform != nullptr ? Platform->GetScreenHeight() : 0;
	}
}

int32 NPhysicsSampleView::ToScreenX( float32 X )
{
	return static_cast<int32>( X * PixelsPerMeter );
}

int32 NPhysicsSampleView::ToScreenY( float32 Y )
{
	return GetScreenHeight() - static_cast<int32>( Y * PixelsPerMeter );
}

FVector3 NPhysicsSampleView::ToWorld( int32 ScreenX, int32 ScreenY )
{
	return FVector3( static_cast<float32>( ScreenX ) / PixelsPerMeter, static_cast<float32>( GetScreenHeight() - ScreenY ) / PixelsPerMeter, 0.0f );
}
