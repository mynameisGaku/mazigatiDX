#include "Platformer2DCoinPop.h"

#include "Library/Framework/Render/Draw/DrawContext.h"
#include "Source/Platformer2D/Platformer2DState.h"

namespace
{
	constexpr float32 Lifetime = 0.55f;
	constexpr float32 RiseHeight = 96.0f;
}

CPlatformer2DCoinPop::CPlatformer2DCoinPop( const TSharedPtr<FPlatformer2DState>& InState, const FVector2& InStart )
	: State( InState )
	, Start( InStart )
{}

void CPlatformer2DCoinPop::Tick( float32 DeltaTime )
{
	Age += DeltaTime;
	if ( Age >= Lifetime )
	{
		Destroy();
	}
}

void CPlatformer2DCoinPop::Draw( FDrawContext& CommandList )
{
	// 放物線: 半分の時間で、いちばん高いところへ行き、また戻る
	const float32 T = Age / Lifetime;
	const float32 Height = RiseHeight * 4.0f * T * ( 1.0f - T );
	const int32 X = State->ToScreenX( Start.x );
	const int32 Y = static_cast<int32>( Start.y - 16.0f - Height );
	const int32 Rx = 3 + static_cast<int32>( 8.0f * NMath::Abs( NMath::Cos( Age * 22.0f ) ) );
	CommandList.Oval( X, Y, Rx, 11, FColor32( 250, 206, 40 ), true );
	CommandList.Oval( X, Y, Rx, 11, FColor32( 170, 120, 0 ), false );
}
