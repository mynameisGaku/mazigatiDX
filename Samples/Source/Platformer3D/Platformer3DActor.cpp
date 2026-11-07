#include "Platformer3DActor.h"

#include "Library/Framework/Render/Draw/DrawContext.h"
#include "Source/Platformer3D/Platformer3DLevel.h"

void CPlatformer3DActor::RecordBlobShadow( FDrawContext& CommandList, const FPlatformer3DLevel& Level, const FVector3& Feet, float32 Radius )
{
	const float32 Ground = Level.GetGroundHeight( Feet.x, Feet.z, Feet.y );
	if ( Ground < FPlatformer3DLevel::KillHeight )
	{
		return;
	}
	// 高いところにいるほど、小さく見える（すぐ下の地面との距離）
	const float32 Height = NMath::Max( Feet.y - Ground, 0.0f );
	const float32 Scale = NMath::Clamp( 1.0f - Height * 0.12f, 0.35f, 1.0f );
	CommandList.Cone3D( FVector3( Feet.x, Ground + 0.03f, Feet.z ), FVector3( Feet.x, Ground + 0.005f, Feet.z ), Radius * Scale, 16, FColor32( 24, 28, 24 ) );
}
