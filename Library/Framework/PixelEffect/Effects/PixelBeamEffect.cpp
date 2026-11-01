// SPDX-License-Identifier: MIT
#include "Library/Framework/PixelEffect/Effects/PixelBeamEffect.h"

#include "Library/Framework/PixelEffect/PixelColor.h"
#include "Library/Framework/PixelEffect/PixelMath.h"
#include "Library/Math/MathUtils.h"

namespace
{
	// 光線の長さ（画素）と、溜め・放つ・消えるの区切り（秒）
	constexpr float32 Length = 68.0f;
	constexpr float32 ChargeEnd = 0.2f;
	constexpr float32 FireEnd = 0.6f;

	FPixelEffectMeta MakeMeta()
	{
		FPixelEffectMeta Meta;
		Meta.Name = "Beam";
		Meta.Category = "Combat";
		Meta.Anchor = FVector2( 14.0f, 48.0f );
		Meta.Duration = 0.8f;
		Meta.DefaultPalette = EPixelPalette::Neon;
		return Meta;
	}
}

CPixelBeamEffect::CPixelBeamEffect()
	: CPixelTimedEffect( MakeMeta() )
{
}

void CPixelBeamEffect::OnDraw( const CPixelPainter& Painter, EPixelLayer Layer ) const
{
	if ( Layer != EPixelLayer::Front )
	{
		return;
	}
	const float32 Time = GetTime();
	const FColor32 Edge = NPixelColor::SampleStep( GetPalette(), 0.7f );
	if ( Time < ChargeEnd )
	{
		// 溜め: 光の玉が膨らむ
		const float32 Charge = Time / ChargeEnd;
		Painter.Disc( FVector2(), 1.0f + Charge * 4.0f, Edge, FPixelStroke { 1.0f, 1, 0.5f } );
		Painter.Disc( FVector2(), Charge * 2.0f, NPixelColor::White() );
		Painter.Glow( FVector2(), 6.0f + Charge * 8.0f, Edge, 0.6f * Charge );
		return;
	}
	// 放つ: 前半は太く、消えるときは細くなる
	const float32 Fade = Time < FireEnd ? 0.0f : ( Time - FireEnd ) / ( GetMeta().Duration - FireEnd );
	const float32 Reach = Length * NPixelMath::EaseOutCubic( NPixelMath::Clamp01( ( Time - ChargeEnd ) / 0.08f ) );
	const int32 Width = NMath::Max( 1, static_cast<int32>( 7.0f * ( 1.0f - Fade ) ) );
	// 太さが刻みごとに 1 画素ゆらぐ
	const int32 Wobble = GetStepCount() / 3 % 2;
	const FVector2 End( Reach, 0.0f );
	Painter.Line( FVector2(), End, Edge, FPixelStroke { 1.0f, Width + Wobble, 0.5f } );
	Painter.Line( FVector2(), End, NPixelColor::White(), FPixelStroke { 1.0f, NMath::Max( 1, Width / 3 ), 0.3f } );
	Painter.Disc( FVector2(), 3.0f + static_cast<float32>( Width ) * 0.5f, NPixelColor::White(), FPixelStroke { 1.0f - Fade, 1, 0.3f } );
	Painter.Glow( End, 12.0f, Edge, 0.8f * ( 1.0f - Fade ) );
	Painter.Glow( FVector2( Reach * 0.5f, 0.0f ), Reach * 0.5f, Edge, 0.25f * ( 1.0f - Fade ) );
}
