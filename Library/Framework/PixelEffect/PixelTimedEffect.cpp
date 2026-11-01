// SPDX-License-Identifier: MIT
#include "Library/Framework/PixelEffect/PixelTimedEffect.h"

#include "Library/Framework/PixelEffect/PixelColor.h"
#include "Library/Math/MathUtils.h"

namespace
{
	// 1 回の Update で進める時間の上限（止まっていた後に、刻みが溜まりすぎないように）
	constexpr float32 MaxUpdateTime = 0.25f;
	// 足し引きの丸めの誤差で、刻みが 1 つずれないための余裕（刻みの 1/1000 未満）
	constexpr float32 StepTolerance = 1.0e-6f;
}

CPixelTimedEffect::CPixelTimedEffect( const FPixelEffectMeta& InMeta )
	: Meta( InMeta )
{
	Palette = NPixelColor::GetPalette( Meta.DefaultPalette );
}

const FPixelEffectMeta& CPixelTimedEffect::GetMeta() const
{
	return Meta;
}

void CPixelTimedEffect::Start( const FPixelPlacement& InPlacement )
{
	Placement = InPlacement;
	Palette = Placement.Palette.IsEmpty() ? NPixelColor::GetPalette( Meta.DefaultPalette ) : Placement.Palette;
	StepCount = 0;
	PendingTime = 0.0f;
	FPixelRandom Random( Placement.Seed );
	OnStart( Random );
}

void CPixelTimedEffect::Update( float32 DeltaTime )
{
	if ( !( DeltaTime > 0.0f ) )
	{
		return;
	}
	PendingTime += NMath::Min( DeltaTime, MaxUpdateTime );
	while ( PendingTime >= StepTime - StepTolerance )
	{
		PendingTime -= StepTime;
		if ( IsFinished() )
		{
			continue;
		}
		++StepCount;
		OnStep( StepTime );
	}
}

void CPixelTimedEffect::Draw( IPixelSurface& Surface, EPixelLayer Layer ) const
{
	if ( IsFinished() )
	{
		return;
	}
	const CPixelPainter Painter( Surface, Placement.Position, Placement.bFlipHorizontal );
	OnDraw( Painter, Layer );
}

bool CPixelTimedEffect::IsFinished() const
{
	return !Meta.bLoop && static_cast<float32>( StepCount ) * StepTime >= Meta.Duration;
}

void CPixelTimedEffect::OnStart( FPixelRandom& Random )
{
	( void )Random;
}

void CPixelTimedEffect::OnStep( float32 InStepTime )
{
	( void )InStepTime;
}

float32 CPixelTimedEffect::GetTime() const
{
	const float32 Elapsed = static_cast<float32>( StepCount ) * StepTime;
	if ( !( Meta.Duration > 0.0f ) )
	{
		return Elapsed;
	}
	if ( Meta.bLoop )
	{
		return Elapsed - Meta.Duration * NMath::Floor( Elapsed / Meta.Duration );
	}
	return NMath::Min( Elapsed, Meta.Duration );
}

float32 CPixelTimedEffect::GetProgress() const
{
	return Meta.Duration > 0.0f ? NMath::Clamp( GetTime() / Meta.Duration, 0.0f, 1.0f ) : 0.0f;
}
