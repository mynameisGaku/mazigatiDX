// SPDX-License-Identifier: MIT
#include "Library/Framework/PixelEffect/Effects/PixelPoisonEffect.h"

#include "Library/Framework/PixelEffect/PixelColor.h"
#include "Library/Framework/PixelEffect/PixelMath.h"
#include "Library/Math/MathUtils.h"

namespace
{
	constexpr int32 BubbleCount = 7;
	// 泡が昇る高さ（画素）
	constexpr float32 RiseHeight = 30.0f;
	// 1 周のうち、泡が弾けている割合（最後の部分）
	constexpr float32 PopStart = 0.85f;

	FPixelEffectMeta MakeMeta()
	{
		FPixelEffectMeta Meta;
		Meta.Name = "Poison";
		Meta.Category = "Status";
		Meta.Anchor = FVector2( 48.0f, 80.0f );
		Meta.Duration = 1.5f;
		Meta.bLoop = true;
		Meta.DefaultPalette = EPixelPalette::Poison;
		return Meta;
	}
}

CPixelPoisonEffect::CPixelPoisonEffect()
	: CPixelTimedEffect( MakeMeta() )
{
}

void CPixelPoisonEffect::OnDraw( const CPixelPainter& Painter, EPixelLayer Layer ) const
{
	if ( Layer == EPixelLayer::Back )
	{
		Painter.Glow( FVector2( 0.0f, -10.0f ), 18.0f, NPixelColor::SampleStep( GetPalette(), 0.5f ), 0.3f );
		return;
	}
	const int32 Seed = static_cast<int32>( GetPlacement().Seed );
	for ( int32 Index = 0; Index < BubbleCount; ++Index )
	{
		// 泡ごとに、1 周の中で生まれる時刻をずらす
		const float32 Phase = NPixelMath::Wrap( GetProgress() + NPixelMath::RandomUnit( Index, Seed ), 1.0f );
		const float32 BaseX = ( NPixelMath::RandomUnit( Index, Seed, 1 ) - 0.5f ) * 16.0f;
		const FVector2 Center( BaseX + NMath::Sin( Phase * NMath::TWO_PI * 2.0f + static_cast<float32>( Index ) ) * 2.0f, -4.0f - Phase * RiseHeight );
		const FColor32 Color = NPixelColor::SampleStep( GetPalette(), 0.55f + Phase * 0.4f );
		if ( Phase < PopStart )
		{
			const float32 Radius = 1.0f + Phase * 2.0f;
			Painter.Ring( Center, Radius, Color, FPixelStroke { 1.0f, 1, 0.15f } );
			Painter.Dot( Center + FVector2( -Radius * 0.4f, -Radius * 0.4f ), NPixelColor::White() );
			continue;
		}
		// 弾ける: 4 方向へ飛ぶ飛沫
		const float32 Pop = ( Phase - PopStart ) / ( 1.0f - PopStart );
		const float32 Spread = 3.0f + Pop * 4.0f;
		const FPixelStroke Stroke { 1.0f - Pop, 1, 0.2f };
		Painter.Dot( Center + FVector2( -Spread, 0.0f ), Color, Stroke );
		Painter.Dot( Center + FVector2( Spread, 0.0f ), Color, Stroke );
		Painter.Dot( Center + FVector2( 0.0f, -Spread ), Color, Stroke );
		Painter.Dot( Center + FVector2( 0.0f, Spread ), Color, Stroke );
	}
}
