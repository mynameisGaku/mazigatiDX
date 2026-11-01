// SPDX-License-Identifier: MIT
#include "Library/Framework/PixelEffect/Effects/PixelAuraEffect.h"

#include "Library/Framework/PixelEffect/PixelColor.h"
#include "Library/Framework/PixelEffect/PixelMath.h"
#include "Library/Math/MathUtils.h"

namespace
{
	// 縁から上へ伸びる炎の長さ（画素）
	constexpr int32 MaxFlame = 7;
	// ノイズがこれより弱い縁からは、炎を出さない（すき間を作って、炎の舌に分ける）
	constexpr float32 FlameThreshold = 0.35f;
	// 揺らめきの模様の流れる速さ（1 周 1.2 秒で整数周になるよう 5 / 1.2）
	constexpr float32 FlowSpeed = 5.0f / 1.2f;

	FPixelEffectMeta MakeMeta()
	{
		FPixelEffectMeta Meta;
		Meta.Name = "Aura";
		Meta.Category = "Status";
		Meta.Anchor = FVector2( 48.0f, 80.0f );
		Meta.Duration = 1.2f;
		Meta.bLoop = true;
		Meta.DefaultPalette = EPixelPalette::Arcane;
		return Meta;
	}

	// (X, Y) がシルエットの外で、上下左右のどれかがシルエットか（縁のすぐ外側）
	bool IsOutline( const FPixelMask& Mask, int32 X, int32 Y )
	{
		return !Mask.IsSet( X, Y ) && ( Mask.IsSet( X - 1, Y ) || Mask.IsSet( X + 1, Y ) || Mask.IsSet( X, Y - 1 ) || Mask.IsSet( X, Y + 1 ) );
	}
}

CPixelAuraEffect::CPixelAuraEffect( const FPixelMask& InSilhouette )
	: CPixelTimedEffect( MakeMeta() )
	, Silhouette( InSilhouette )
{
}

void CPixelAuraEffect::OnDraw( const CPixelPainter& Painter, EPixelLayer Layer ) const
{
	if ( Layer != EPixelLayer::Back || !Silhouette.IsValid() )
	{
		return;
	}
	const float32 Flow = GetTime() * FlowSpeed;
	const int32 Seed = static_cast<int32>( GetPlacement().Seed );
	const FVector2 TopLeft( -static_cast<float32>( Silhouette.Width / 2 ), -static_cast<float32>( Silhouette.Height ) );
	const float32 MiddleX = static_cast<float32>( Silhouette.Width ) * 0.5f;
	Painter.Glow( FVector2( 0.0f, -static_cast<float32>( Silhouette.Height ) * 0.5f ), static_cast<float32>( Silhouette.Height ) * 0.8f, NPixelColor::SampleStep( GetPalette(), 0.6f ), 0.35f );
	for ( int32 Y = -1; Y <= Silhouette.Height; ++Y )
	{
		for ( int32 X = -1; X <= Silhouette.Width; ++X )
		{
			if ( !IsOutline( Silhouette, X, Y ) )
			{
				continue;
			}
			// 縁の 1 画素ごとに、上へ伸びる炎の長さをノイズで決める
			const float32 Noise = NPixelMath::TileNoise2D( static_cast<float32>( X ) * 0.45f, static_cast<float32>( Y ) * 0.2f + Flow, 0, 5, Seed ) * 0.5f + 0.5f;
			const float32 Strength = ( Noise - FlameThreshold ) / ( 1.0f - FlameThreshold );
			// 縁そのものは、いつも暗い色で 1 画素（シルエットを縁取る）
			Painter.Dot( TopLeft + FVector2( static_cast<float32>( X ), static_cast<float32>( Y ) ), NPixelColor::SampleStep( GetPalette(), 0.45f ), FPixelStroke { 1.0f, 1, 0.1f } );
			if ( Strength <= 0.0f )
			{
				continue;
			}
			// 横の縁の炎は、外へ傾けて昇る
			const float32 Lean = static_cast<float32>( X ) + 0.5f < MiddleX ? -1.0f : 1.0f;
			const int32 Length = 1 + static_cast<int32>( Strength * static_cast<float32>( MaxFlame ) );
			for ( int32 Step = 1; Step <= Length; ++Step )
			{
				const float32 Heat = 1.0f - static_cast<float32>( Step ) / static_cast<float32>( Length + 1 );
				FPixelStroke Stroke;
				Stroke.Alpha = 0.35f + Heat * 0.65f;
				Stroke.Glow = 0.25f * Heat;
				const FVector2 Position = TopLeft + FVector2( static_cast<float32>( X ) + Lean * NMath::Floor( static_cast<float32>( Step ) / 3.0f ), static_cast<float32>( Y - Step ) );
				Painter.Dot( Position, NPixelColor::SampleStep( GetPalette(), 0.35f + Heat * 0.5f ), Stroke );
			}
		}
	}
}
