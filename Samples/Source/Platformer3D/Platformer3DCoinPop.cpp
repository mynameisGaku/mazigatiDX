#include "Platformer3DCoinPop.h"

#include "Library/Framework/Render/Draw/DrawContext.h"
#include "Source/Platformer3D/Platformer3DState.h"

namespace
{
	constexpr float32 Lifetime = 0.55f;
	constexpr float32 RiseHeight = 2.2f;
}

CPlatformer3DCoinPop::CPlatformer3DCoinPop( const TSharedPtr<FPlatformer3DState>& InState, const FVector3& InStart )
	: State( InState )
	, Start( InStart )
{}

void CPlatformer3DCoinPop::Tick( float32 DeltaTime )
{
	Age += DeltaTime;
	if ( Age >= Lifetime )
	{
		Destroy();
	}
}

void CPlatformer3DCoinPop::Record3D( FDrawContext& CommandList )
{
	const float32 T = Age / Lifetime;
	const float32 Rise = RiseHeight * 4.0f * T * ( 1.0f - T );
	const FVector3 Center = Start + FVector3( 0.0f, 0.5f + Rise, 0.0f );
	const float32 Spin = NMath::Abs( NMath::Cos( Age * 22.0f ) );
	CommandList.Cube3D( Center - FVector3( 0.28f * Spin + 0.04f, 0.3f, 0.04f ), Center + FVector3( 0.28f * Spin + 0.04f, 0.3f, 0.04f ), FColor32( 250, 206, 40 ) );
}
