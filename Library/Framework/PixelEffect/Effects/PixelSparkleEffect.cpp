// SPDX-License-Identifier: MIT
#include "Library/Framework/PixelEffect/Effects/PixelSparkleEffect.h"

#include "Library/Framework/PixelEffect/Canvas/PixelColor.h"
#include "Library/Framework/PixelEffect/Math/PixelMath.h"
#include "Library/Math/MathUtils.h"

namespace
{
	constexpr int32 StarCount = 9;
	constexpr float32 Spread = 28.0f;
	// 1 つの星が光っている長さ（1 周に対する割合）
	constexpr float32 TwinkleLength = 0.35f;

	FPixelEffectMeta MakeMeta()
	{
		FPixelEffectMeta Meta;
		Meta.Name = "Sparkle";
		Meta.Category = "Magic";
		Meta.Duration = 1.6f;
		Meta.bLoop = true;
		Meta.DefaultPalette = EPixelPalette::Holy;
		return Meta;
	}
}

CPixelSparkleEffect::CPixelSparkleEffect()
	: CPixelTimedEffect( MakeMeta() )
{
}

void CPixelSparkleEffect::OnStart( FPixelRandom& Random )
{
	Stars.Reset();
	for ( int32 Index = 0; Index < StarCount; ++Index )
	{
		FStar Star;
		Star.Position = FVector2( Random.Range( -Spread, Spread ), Random.Range( -Spread, Spread * 0.6f ) );
		Star.Phase = Random.Range( 0.0f, 1.0f );
		Star.Size = static_cast<float32>( Random.RangeInt( 2, 4 ) );
		Stars.Add( Star );
	}
}

void CPixelSparkleEffect::OnDraw( const CPixelPainter& Painter, EPixelLayer Layer ) const
{
	if ( Layer != EPixelLayer::Front )
	{
		return;
	}
	const float32 Progress = GetProgress();
	for ( const FStar& Star : Stars )
	{
		// 星の明るさ: 間合いの所で山になる（1 周でつながるよう、差を -0.5〜0.5 に折り返す）
		const float32 Offset = NPixelMath::Wrap( Progress - Star.Phase + 0.5f, 1.0f ) - 0.5f;
		const float32 Brightness = NPixelMath::Clamp01( 1.0f - NMath::Abs( Offset ) / ( TwinkleLength * 0.5f ) );
		if ( Brightness <= 0.0f )
		{
			continue;
		}
		const FColor32 Color = NPixelColor::SampleStep( GetPalette(), 0.5f + Brightness * 0.5f );
		const float32 Arm = NMath::Floor( Star.Size * Brightness + 0.5f );
		FPixelStroke Stroke;
		Stroke.Glow = 0.3f * Brightness;
		if ( Arm >= 1.0f )
		{
			Painter.Line( Star.Position - FVector2( Arm, 0.0f ), Star.Position + FVector2( Arm, 0.0f ), Color, Stroke );
			Painter.Line( Star.Position - FVector2( 0.0f, Arm ), Star.Position + FVector2( 0.0f, Arm ), Color, Stroke );
		}
		Painter.Dot( Star.Position, NPixelColor::White(), Stroke );
		Painter.Glow( Star.Position, 3.0f + Star.Size * 2.0f, Color, 0.5f * Brightness );
	}
}
