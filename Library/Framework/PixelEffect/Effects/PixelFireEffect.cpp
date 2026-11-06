// SPDX-License-Identifier: MIT
#include "Library/Framework/PixelEffect/Effects/PixelFireEffect.h"

#include "Library/Framework/PixelEffect/Canvas/PixelColor.h"
#include "Library/Framework/PixelEffect/Math/PixelMath.h"
#include "Library/Math/MathUtils.h"

namespace
{
	constexpr int32 FlameHeight = 30;
	constexpr int32 FlameHalfWidth = 9;
	// 炎の模様が上へ流れる速さ（ノイズの目 / 秒）。1 周（1 秒）で整数になるので、繰り返しの継ぎ目が出ない
	constexpr float32 RiseSpeed = 6.0f;
	constexpr int32 EmberCount = 6;

	FPixelEffectMeta MakeMeta()
	{
		FPixelEffectMeta Meta;
		Meta.Name = "Fire";
		Meta.Category = "Element";
		Meta.Anchor = FVector2( 48.0f, 80.0f );
		Meta.Duration = 1.0f;
		Meta.bLoop = true;
		Meta.DefaultPalette = EPixelPalette::Fire;
		return Meta;
	}
}

CPixelFireEffect::CPixelFireEffect()
	: CPixelTimedEffect( MakeMeta() )
{
}

void CPixelFireEffect::OnDraw( const CPixelPainter& Painter, EPixelLayer Layer ) const
{
	if ( Layer != EPixelLayer::Front )
	{
		return;
	}
	const float32 Time = GetTime();
	const int32 Seed = static_cast<int32>( GetPlacement().Seed );
	for ( int32 Y = 0; Y < FlameHeight; ++Y )
	{
		// 高さ（0 = 根元、1 = 先）で、炎の幅を細くする
		const float32 Height = static_cast<float32>( Y ) / static_cast<float32>( FlameHeight );
		const float32 HalfWidth = static_cast<float32>( FlameHalfWidth ) * ( 1.0f - Height * Height );
		for ( int32 X = -FlameHalfWidth; X <= FlameHalfWidth; ++X )
		{
			const float32 Edge = NMath::Abs( static_cast<float32>( X ) + 0.5f ) / NMath::Max( 0.5f, HalfWidth );
			const float32 Noise = NPixelMath::TileNoise2D( static_cast<float32>( X ) * 0.3f, static_cast<float32>( Y ) * 0.3f + Time * RiseSpeed, 0, static_cast<int32>( RiseSpeed ), Seed ) * 0.5f + 0.5f;
			const float32 Heat = ( 1.0f - Height ) * ( 1.0f - Edge * 0.8f ) * ( 0.55f + Noise * 0.7f ) - Height * 0.25f;
			if ( Heat < 0.18f )
			{
				continue;
			}
			FPixelStroke Stroke;
			Stroke.Glow = Heat > 0.7f ? 0.25f : 0.0f;
			Painter.Dot( FVector2( static_cast<float32>( X ), static_cast<float32>( -1 - Y ) ), NPixelColor::SampleStep( GetPalette(), Heat ), Stroke );
		}
	}
	// 火の粉: 番号ごとに決まった横の位置から、1 周の間に昇って消える
	for ( int32 Index = 0; Index < EmberCount; ++Index )
	{
		const float32 Phase = NPixelMath::Wrap( Time + NPixelMath::RandomUnit( Index, Seed ), 1.0f );
		const float32 Drift = NPixelMath::Noise1D( Phase * 3.0f + static_cast<float32>( Index ) * 7.0f, Seed ) * 4.0f;
		const float32 X = ( NPixelMath::RandomUnit( Index, Seed, 1 ) - 0.5f ) * 12.0f + Drift;
		const float32 Y = -8.0f - Phase * 34.0f;
		FPixelStroke Stroke;
		Stroke.Alpha = 1.0f - Phase;
		Stroke.Glow = 0.4f;
		Painter.Dot( FVector2( X, Y ), NPixelColor::SampleStep( GetPalette(), 1.0f - Phase * 0.5f ), Stroke );
	}
	Painter.Glow( FVector2( 0.0f, -8.0f ), 22.0f, NPixelColor::SampleStep( GetPalette(), 0.8f ), 0.35f );
}
