#include "TimeControl.h"

void FEngineTimeControl::SetScale( float32 InScale )
{
	// NaN は 1 に戻す（比較がすべて偽になるので、最初に調べる）
	if ( !( InScale == InScale ) )
	{
		Scale = 1.0f;
		return;
	}
	Scale = InScale < 0.0f ? 0.0f : ( InScale > MaxScale ? MaxScale : InScale );
}

void FEngineTimeControl::Step( int32 Frames )
{
	bPaused = true;
	if ( Frames > 0 ) StepsRemaining += Frames;
}

float32 FEngineTimeControl::Apply( float32 PlatformDeltaTime )
{
	if ( bPaused )
	{
		if ( StepsRemaining > 0 )
		{
			--StepsRemaining;
			return StepDeltaTime * Scale;
		}
		return 0.0f;
	}
	return Scale == 1.0f ? PlatformDeltaTime : PlatformDeltaTime * Scale;
}
