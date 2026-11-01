// SPDX-License-Identifier: MIT
#include "Library/Framework/PixelEffect/Effects/PixelExplosionEffect.h"

#include "Library/Framework/PixelEffect/PixelColor.h"
#include "Library/Framework/PixelEffect/PixelMath.h"
#include "Library/Math/MathUtils.h"

namespace
{
	constexpr int32 SparkCount = 36;
	constexpr int32 SmokeCount = 9;
	// 火の玉が膨らんで消えるまで（秒）
	constexpr float32 FlashTime = 0.18f;

	FPixelEffectMeta MakeMeta()
	{
		FPixelEffectMeta Meta;
		Meta.Name = "Explosion";
		Meta.Category = "Combat";
		Meta.Anchor = FVector2( 48.0f, 60.0f );
		Meta.Duration = 1.2f;
		Meta.DefaultPalette = EPixelPalette::Fire;
		return Meta;
	}
}

CPixelExplosionEffect::CPixelExplosionEffect()
	: CPixelTimedEffect( MakeMeta() )
{
	SmokePalette = NPixelColor::GetPalette( EPixelPalette::Smoke );
}

void CPixelExplosionEffect::OnStart( FPixelRandom& Random )
{
	Sparks.Clear();
	Smoke.Clear();
	for ( int32 Index = 0; Index < SparkCount; ++Index )
	{
		// 上向きに偏らせて、四方へ
		const float32 Angle = Random.Range( -NMath::PI, NMath::PI );
		const float32 Speed = Random.Range( 45.0f, 115.0f );
		FPixelParticle Spark;
		Spark.Velocity = FVector2( NMath::Cos( Angle ) * Speed, NMath::Sin( Angle ) * Speed - 30.0f );
		Spark.Life = Random.Range( 0.35f, 0.9f );
		Spark.Gravity = 140.0f;
		Spark.Drag = 1.6f;
		Spark.Heat = Random.Range( 0.7f, 1.0f );
		Spark.Glow = 0.5f;
		Spark.Shape = Random.Chance( 0.6f ) ? EPixelParticleShape::Streak : EPixelParticleShape::Dot;
		Sparks.Add( Spark );
	}
	for ( int32 Index = 0; Index < SmokeCount; ++Index )
	{
		FPixelParticle Puff;
		Puff.Position = FVector2( Random.Range( -6.0f, 6.0f ), Random.Range( -6.0f, 2.0f ) );
		Puff.Velocity = FVector2( Random.Range( -10.0f, 10.0f ), Random.Range( -22.0f, -8.0f ) );
		Puff.Life = Random.Range( 0.8f, 1.2f );
		Puff.Drag = 0.8f;
		Puff.Size = Random.Range( 3.0f, 6.0f );
		Puff.Heat = Random.Range( 0.5f, 0.8f );
		Puff.Shape = EPixelParticleShape::Disc;
		Puff.Layer = EPixelLayer::Back;
		Smoke.Add( Puff );
	}
}

void CPixelExplosionEffect::OnStep( float32 InStepTime )
{
	Sparks.Step( InStepTime );
	Smoke.Step( InStepTime );
}

void CPixelExplosionEffect::OnDraw( const CPixelPainter& Painter, EPixelLayer Layer ) const
{
	if ( Layer == EPixelLayer::Back )
	{
		Smoke.Draw( Painter, SmokePalette, Layer );
		return;
	}
	const float32 Time = GetTime();
	if ( Time < FlashTime )
	{
		// 火の玉: 膨らみながら、明るい段から暗い段へ
		const float32 Flash = Time / FlashTime;
		const float32 Radius = NPixelMath::Lerp( 4.0f, 13.0f, NPixelMath::EaseOutCubic( Flash ) );
		Painter.Disc( FVector2(), Radius, NPixelColor::SampleStep( GetPalette(), 1.0f - Flash * 0.5f ), FPixelStroke { 1.0f - Flash * 0.6f, 1, 0.0f } );
		Painter.Disc( FVector2(), Radius * 0.55f, NPixelColor::White(), FPixelStroke { 1.0f - Flash, 1, 0.0f } );
	}
	// 周りを照らす光は、火の玉より少し長く残る
	const float32 Light = 1.0f - NMath::Min( 1.0f, Time / ( FlashTime * 2.0f ) );
	Painter.Glow( FVector2(), 26.0f, NPixelColor::SampleStep( GetPalette(), 0.9f ), Light * 0.9f );
	Sparks.Draw( Painter, GetPalette(), Layer );
}
