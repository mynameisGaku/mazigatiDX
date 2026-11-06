// SPDX-License-Identifier: MIT
#include "Library/Framework/PixelEffect/Effects/PixelMagicCircleEffect.h"

#include "Library/Framework/PixelEffect/Canvas/PixelColor.h"
#include "Library/Framework/PixelEffect/Math/PixelMath.h"
#include "Library/Math/MathUtils.h"

namespace
{
	// 外の楕円の半径（横・縦）と、内の楕円の縮み
	const FVector2 OuterRadius( 26.0f, 8.0f );
	constexpr float32 InnerScale = 0.68f;
	constexpr int32 RuneCount = 8;

	FPixelEffectMeta MakeMeta()
	{
		FPixelEffectMeta Meta;
		Meta.Name = "MagicCircle";
		Meta.Category = "Magic";
		Meta.Anchor = FVector2( 48.0f, 76.0f );
		Meta.Duration = 2.0f;
		Meta.bLoop = true;
		Meta.DefaultPalette = EPixelPalette::Arcane;
		return Meta;
	}
}

CPixelMagicCircleEffect::CPixelMagicCircleEffect()
	: CPixelTimedEffect( MakeMeta() )
{
}

void CPixelMagicCircleEffect::OnDraw( const CPixelPainter& Painter, EPixelLayer Layer ) const
{
	const float32 Turn = GetProgress() * NMath::TWO_PI;
	// 明るさは 1 周に 2 回ゆっくり脈打つ
	const float32 Pulse = 0.5f + 0.5f * NMath::Sin( Turn * 2.0f );
	if ( Layer == EPixelLayer::Back )
	{
		const FColor32 Ring = NPixelColor::SampleStep( GetPalette(), 0.6f + Pulse * 0.25f );
		Painter.Ellipse( FVector2(), OuterRadius, Ring, FPixelStroke { 1.0f, 1, 0.3f } );
		Painter.Ellipse( FVector2(), FVector2( OuterRadius.x * InnerScale, OuterRadius.y * InnerScale ), NPixelColor::SampleStep( GetPalette(), 0.5f ), FPixelStroke { 0.75f, 1, 0.15f } );
		Painter.Glow( FVector2(), 30.0f, Ring, 0.25f + Pulse * 0.15f );
	}
	for ( int32 Index = 0; Index < RuneCount; ++Index )
	{
		const float32 Angle = Turn + NMath::TWO_PI * static_cast<float32>( Index ) / static_cast<float32>( RuneCount );
		const FVector2 Rune( NMath::Cos( Angle ) * OuterRadius.x, NMath::Sin( Angle ) * OuterRadius.y );
		// 奥（上半分）の印は暗く後ろの層へ、手前は明るく前の層へ
		const bool bFront = NMath::Sin( Angle ) > 0.0f;
		if ( bFront != ( Layer == EPixelLayer::Front ) )
		{
			continue;
		}
		const FColor32 Color = NPixelColor::SampleStep( GetPalette(), bFront ? 0.95f : 0.6f );
		Painter.Rect( Rune - FVector2( 1.0f, 1.0f ), FVector2( 2.0f, 2.0f ), Color, FPixelStroke { 1.0f, 1, bFront ? 0.4f : 0.1f } );
		// 光の筋: 印ごとに、時刻をずらして伸び縮みする
		const float32 Beam = NPixelMath::PingPong( NPixelMath::Wrap( GetProgress() * 2.0f + static_cast<float32>( Index ) * 0.37f, 1.0f ) * 2.0f );
		Painter.Line( Rune, Rune - FVector2( 0.0f, 4.0f + Beam * 14.0f ), Color, FPixelStroke { 0.5f + Beam * 0.4f, 1, 0.2f } );
	}
}
