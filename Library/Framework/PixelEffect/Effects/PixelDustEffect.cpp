// SPDX-License-Identifier: MIT
#include "Library/Framework/PixelEffect/Effects/PixelDustEffect.h"

#include "Library/Framework/PixelEffect/PixelColor.h"
#include "Library/Framework/PixelEffect/PixelMath.h"
#include "Library/Math/MathUtils.h"

namespace
{
	constexpr int32 PuffCount = 10;
	constexpr int32 PebbleCount = 6;

	FPixelEffectMeta MakeMeta()
	{
		FPixelEffectMeta Meta;
		Meta.Name = "Dust";
		Meta.Category = "Element";
		Meta.Anchor = FVector2( 48.0f, 76.0f );
		Meta.Duration = 0.8f;
		Meta.DefaultPalette = EPixelPalette::Dust;
		return Meta;
	}
}

CPixelDustEffect::CPixelDustEffect()
	: CPixelTimedEffect( MakeMeta() )
{
}

void CPixelDustEffect::OnStart( FPixelRandom& Random )
{
	Puffs.Clear();
	Pebbles.Clear();
	for ( int32 Index = 0; Index < PuffCount; ++Index )
	{
		// 左右に半分ずつ、地面すれすれを外へ
		const float32 Side = Index % 2 == 0 ? -1.0f : 1.0f;
		FPixelParticle Puff;
		Puff.Position = FVector2( Side * Random.Range( 1.0f, 4.0f ), Random.Range( -3.0f, -1.0f ) );
		Puff.Velocity = FVector2( Side * Random.Range( 25.0f, 60.0f ), Random.Range( -12.0f, -3.0f ) );
		Puff.Life = Random.Range( 0.5f, 0.8f );
		Puff.Drag = 3.0f;
		Puff.Size = Random.Range( 2.0f, 4.0f );
		Puff.Growth = 0.8f;
		Puff.Heat = Random.Range( 0.6f, 0.9f );
		Puff.Shape = EPixelParticleShape::Disc;
		Puffs.Add( Puff );
	}
	for ( int32 Index = 0; Index < PebbleCount; ++Index )
	{
		FPixelParticle Pebble;
		Pebble.Velocity = FVector2( static_cast<float32>( Random.Sign() ) * Random.Range( 15.0f, 40.0f ), Random.Range( -55.0f, -30.0f ) );
		Pebble.Life = Random.Range( 0.4f, 0.6f );
		Pebble.Gravity = 200.0f;
		Pebble.Heat = 0.35f;
		Pebble.bFade = false;
		Pebbles.Add( Pebble );
	}
}

void CPixelDustEffect::OnStep( float32 InStepTime )
{
	Puffs.Step( InStepTime );
	Pebbles.Step( InStepTime );
}

void CPixelDustEffect::OnDraw( const CPixelPainter& Painter, EPixelLayer Layer ) const
{
	if ( Layer != EPixelLayer::Front )
	{
		return;
	}
	Puffs.Draw( Painter, GetPalette(), Layer );
	Pebbles.Draw( Painter, GetPalette(), Layer );
}
