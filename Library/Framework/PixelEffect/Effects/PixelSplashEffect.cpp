// SPDX-License-Identifier: MIT
#include "Library/Framework/PixelEffect/Effects/PixelSplashEffect.h"

#include "Library/Framework/PixelEffect/PixelColor.h"
#include "Library/Framework/PixelEffect/PixelMath.h"
#include "Library/Math/MathUtils.h"

namespace
{
	constexpr int32 DropCount = 22;
	// 2 つ目の波紋が遅れて始まる割合
	constexpr float32 SecondRipple = 0.25f;

	FPixelEffectMeta MakeMeta()
	{
		FPixelEffectMeta Meta;
		Meta.Name = "Splash";
		Meta.Category = "Element";
		Meta.Anchor = FVector2( 48.0f, 70.0f );
		Meta.Duration = 0.9f;
		Meta.DefaultPalette = EPixelPalette::Water;
		return Meta;
	}
}

CPixelSplashEffect::CPixelSplashEffect()
	: CPixelTimedEffect( MakeMeta() )
{
}

void CPixelSplashEffect::OnStart( FPixelRandom& Random )
{
	Drops.Clear();
	for ( int32 Index = 0; Index < DropCount; ++Index )
	{
		// 上向きの扇形（真上から左右へ 60 度ほど）
		const float32 Angle = -NMath::PI * 0.5f + Random.Range( -1.05f, 1.05f );
		const float32 Speed = Random.Range( 60.0f, 125.0f );
		FPixelParticle Drop;
		Drop.Position = FVector2( Random.Range( -3.0f, 3.0f ), 0.0f );
		Drop.Velocity = FVector2( NMath::Cos( Angle ) * Speed, NMath::Sin( Angle ) * Speed );
		Drop.Life = Random.Range( 0.6f, 0.9f );
		Drop.Gravity = 260.0f;
		Drop.Heat = Random.Range( 0.75f, 1.0f );
		Drop.Size = 1.0f;
		Drop.bFade = false;
		Drop.Shape = Random.Chance( 0.3f ) ? EPixelParticleShape::Disc : EPixelParticleShape::Dot;
		Drops.Add( Drop );
	}
}

void CPixelSplashEffect::OnStep( float32 InStepTime )
{
	Drops.Step( InStepTime );
	// 水面より下へ落ちた滴は消す
	Drops.RemoveIf( []( const FPixelParticle& Drop )
	{
		return Drop.Position.y > 0.5f && Drop.Velocity.y > 0.0f;
	} );
}

void CPixelSplashEffect::OnDraw( const CPixelPainter& Painter, EPixelLayer Layer ) const
{
	const float32 Progress = GetProgress();
	if ( Layer == EPixelLayer::Back )
	{
		for ( int32 Index = 0; Index < 2; ++Index )
		{
			const float32 Ripple = ( Progress - SecondRipple * static_cast<float32>( Index ) ) / ( 1.0f - SecondRipple * static_cast<float32>( Index ) );
			if ( Ripple <= 0.0f )
			{
				continue;
			}
			const float32 Radius = 3.0f + NPixelMath::EaseOutQuad( Ripple ) * 26.0f;
			Painter.Ellipse( FVector2(), FVector2( Radius, Radius * 0.28f ), NPixelColor::SampleStep( GetPalette(), 0.8f ), FPixelStroke { 1.0f - Ripple, 1, 0.1f } );
		}
		return;
	}
	Drops.Draw( Painter, GetPalette(), Layer );
}
