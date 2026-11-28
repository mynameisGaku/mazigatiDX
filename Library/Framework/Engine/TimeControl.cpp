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

void FEngineTimeControl::SetFixedDeltaTime( float32 InSeconds )
{
	// NaN と 0 以下は、固定しない（比較がすべて偽になる NaN は、最初に調べる）
	if ( !( InSeconds > 0.0f ) )
	{
		FixedDeltaTime = 0.0f;
		return;
	}
	FixedDeltaTime = InSeconds > MaxFixedDeltaTime ? MaxFixedDeltaTime : InSeconds;
}

void FEngineTimeControl::Step( int32 Frames )
{
	bPaused = true;
	if ( Frames > 0 )
	{
		StepsRemaining += Frames;
	}
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
	// 固定の刻みがあれば、実際の経過時間の代わりに使う（無いときは、経過時間をビットも変えずに返す）
	const float32 DeltaTime = FixedDeltaTime > 0.0f ? FixedDeltaTime : PlatformDeltaTime;
	return Scale == 1.0f ? DeltaTime : DeltaTime * Scale;
}
