// SPDX-License-Identifier: MIT
#include "Library/Framework/PixelEffect/Effects/PixelShockwaveEffect.h"

#include "Library/Framework/PixelEffect/PixelColor.h"
#include "Library/Framework/PixelEffect/PixelMath.h"
#include "Library/Math/MathUtils.h"

namespace
{
	constexpr float32 MaxRadius = 40.0f;
	// 内側の輪が遅れて始まる割合
	constexpr float32 InnerDelay = 0.18f;

	FPixelEffectMeta MakeMeta()
	{
		FPixelEffectMeta Meta;
		Meta.Name = "Shockwave";
		Meta.Category = "Combat";
		Meta.Duration = 0.7f;
		Meta.DefaultPalette = EPixelPalette::Electric;
		return Meta;
	}
}

CPixelShockwaveEffect::CPixelShockwaveEffect()
	: CPixelTimedEffect( MakeMeta() )
{
}

void CPixelShockwaveEffect::OnDraw( const CPixelPainter& Painter, EPixelLayer Layer ) const
{
	if ( Layer != EPixelLayer::Front )
	{
		return;
	}
	const float32 Progress = GetProgress();
	const float32 Outer = NPixelMath::EaseOutCubic( Progress );
	FPixelStroke OuterStroke;
	OuterStroke.Alpha = 1.0f - Progress;
	OuterStroke.Width = Progress < 0.4f ? 3 : ( Progress < 0.75f ? 2 : 1 );
	OuterStroke.Glow = 0.5f * ( 1.0f - Progress );
	Painter.Ring( FVector2(), 3.0f + Outer * MaxRadius, NPixelColor::SampleStep( GetPalette(), 1.0f - Progress * 0.6f ), OuterStroke );

	if ( Progress > InnerDelay )
	{
		const float32 Inner = ( Progress - InnerDelay ) / ( 1.0f - InnerDelay );
		FPixelStroke InnerStroke;
		InnerStroke.Alpha = ( 1.0f - Inner ) * 0.7f;
		Painter.Ring( FVector2(), 2.0f + NPixelMath::EaseOutCubic( Inner ) * MaxRadius * 0.7f, NPixelColor::SampleStep( GetPalette(), 0.6f ), InnerStroke );
	}
}
