// SPDX-License-Identifier: MIT
#include "Library/Framework/PixelEffect/Effects/PixelHealEffect.h"

#include "Library/Framework/PixelEffect/Canvas/PixelColor.h"
#include "Library/Framework/PixelEffect/Math/PixelMath.h"
#include "Library/Math/MathUtils.h"

namespace
{
	constexpr int32 CrossCount = 10;
	// 足元の輪が広がりきる時間（秒）
	constexpr float32 RingTime = 0.6f;

	FPixelEffectMeta MakeMeta()
	{
		FPixelEffectMeta Meta;
		Meta.Name = "Heal";
		Meta.Category = "Status";
		Meta.Anchor = FVector2( 48.0f, 80.0f );
		Meta.Duration = 1.4f;
		Meta.DefaultPalette = EPixelPalette::Heal;
		return Meta;
	}
}

CPixelHealEffect::CPixelHealEffect()
	: CPixelTimedEffect( MakeMeta() )
{
}

void CPixelHealEffect::OnStart( FPixelRandom& Random )
{
	Crosses.Clear();
	for ( int32 Index = 0; Index < CrossCount; ++Index )
	{
		FPixelParticle Cross;
		Cross.Position = FVector2( Random.Range( -12.0f, 12.0f ), Random.Range( -6.0f, 0.0f ) );
		Cross.Velocity = FVector2( Random.Range( -4.0f, 4.0f ), Random.Range( -34.0f, -18.0f ) );
		// 寿命をばらつかせて、消える時刻をずらす
		Cross.Life = Random.Range( 0.7f, 1.3f );
		Cross.Size = static_cast<float32>( Random.RangeInt( 1, 2 ) );
		Cross.Heat = 1.0f;
		Cross.Glow = 0.4f;
		Cross.Shape = Cross.Size > 1.0f ? EPixelParticleShape::Cross : EPixelParticleShape::Dot;
		Crosses.Add( Cross );
	}
}

void CPixelHealEffect::OnStep( float32 InStepTime )
{
	Crosses.Step( InStepTime );
}

void CPixelHealEffect::OnDraw( const CPixelPainter& Painter, EPixelLayer Layer ) const
{
	const float32 Time = GetTime();
	if ( Layer == EPixelLayer::Back )
	{
		if ( Time < RingTime )
		{
			const float32 Ring = Time / RingTime;
			FPixelStroke Stroke;
			Stroke.Alpha = 1.0f - Ring;
			Stroke.Glow = 0.3f * ( 1.0f - Ring );
			// 足元の輪は、斜め上から見た円（横長の楕円）
			const float32 Radius = 4.0f + NPixelMath::EaseOutQuad( Ring ) * 16.0f;
			Painter.Ellipse( FVector2(), FVector2( Radius, Radius * 0.3f ), NPixelColor::SampleStep( GetPalette(), 0.8f ), Stroke );
		}
		Painter.Glow( FVector2( 0.0f, -10.0f ), 20.0f, NPixelColor::SampleStep( GetPalette(), 0.7f ), 0.4f * ( 1.0f - GetProgress() ) );
		return;
	}
	Crosses.Draw( Painter, GetPalette(), Layer );
}
