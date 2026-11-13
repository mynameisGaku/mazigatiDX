// SPDX-License-Identifier: MIT
#include "HitStop.h"

#include "Library/Math/MathUtils.h"

namespace
{
	// 震えの向きが入れ替わる間隔（秒）
	constexpr float32 ShiverInterval = 1.0f / 60.0f;
}

void FHitStop::Start( float32 Seconds )
{
	if ( !NMath::IsFinite( Seconds ) || Seconds <= 0.0f )
	{
		return;
	}
	const float32 Wanted = NMath::Min( Seconds, MaxSeconds );
	if ( Wanted > Remaining )
	{
		Remaining = Wanted;
		Duration = Wanted;
	}
}

void FHitStop::Cancel()
{
	Remaining = 0.0f;
	Duration = 0.0f;
}

float32 FHitStop::Advance( float32 DeltaTime )
{
	if ( !NMath::IsFinite( DeltaTime ) || DeltaTime <= 0.0f )
	{
		return 0.0f;
	}
	if ( Remaining <= 0.0f )
	{
		return DeltaTime;
	}
	if ( DeltaTime < Remaining )
	{
		Remaining -= DeltaTime;
		return 0.0f;
	}
	// このフレームの途中で解けた: 解けたあとの分だけ、ゲームの時間が進む
	const float32 Left = DeltaTime - Remaining;
	Remaining = 0.0f;
	Duration = 0.0f;
	return Left;
}

float32 FHitStop::GetShiver() const
{
	if ( Remaining <= 0.0f || Duration <= 0.0f )
	{
		return 0.0f;
	}
	const float32 Elapsed = Duration - Remaining;
	const int32 Step = static_cast<int32>( Elapsed / ShiverInterval );
	const float32 Direction = ( Step % 2 ) == 0 ? 1.0f : -1.0f;
	return Direction * NMath::Clamp( Remaining / Duration, 0.0f, 1.0f );
}

float32 FHitStop::SecondsFromPower( float32 Power, float32 Scale )
{
	const float32 SafePower = NMath::IsFinite( Power ) ? NMath::Max( Power, 0.0f ) : 0.0f;
	const float32 SafeScale = NMath::IsFinite( Scale ) ? NMath::Max( Scale, 0.0f ) : 1.0f;
	const float32 Frames = ( 2.0f + SafePower * 0.35f ) * SafeScale;
	return NMath::Clamp( Frames / 60.0f, 1.0f / 60.0f, 0.5f );
}
