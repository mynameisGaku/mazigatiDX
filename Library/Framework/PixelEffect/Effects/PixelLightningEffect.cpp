// SPDX-License-Identifier: MIT
#include "Library/Framework/PixelEffect/Effects/PixelLightningEffect.h"

#include "Library/Framework/PixelEffect/Canvas/PixelColor.h"
#include "Library/Framework/PixelEffect/Math/PixelMath.h"
#include "Library/Math/MathUtils.h"

namespace
{
	constexpr float32 BoltHeight = 64.0f;
	constexpr int32 SegmentCount = 9;
	// 形が変わる間隔（刻み）
	constexpr int32 StepsPerFlicker = 6;

	FPixelEffectMeta MakeMeta()
	{
		FPixelEffectMeta Meta;
		Meta.Name = "Lightning";
		Meta.Category = "Element";
		Meta.Anchor = FVector2( 48.0f, 84.0f );
		Meta.Duration = 0.5f;
		Meta.DefaultPalette = EPixelPalette::Volt;
		return Meta;
	}
}

CPixelLightningEffect::CPixelLightningEffect()
	: CPixelTimedEffect( MakeMeta() )
{
}

void CPixelLightningEffect::OnDraw( const CPixelPainter& Painter, EPixelLayer Layer ) const
{
	if ( Layer != EPixelLayer::Front )
	{
		return;
	}
	const float32 Progress = GetProgress();
	const int32 Flicker = GetStepCount() / StepsPerFlicker;
	// 後半は、ちらつきの半分を消して、消えかけを表す
	if ( Progress > 0.5f && Flicker % 2 == 1 )
	{
		return;
	}
	FPixelRandom Random = FPixelRandom::FromKeys( static_cast<int32>( GetPlacement().Seed ), Flicker );
	const FColor32 Core = NPixelColor::White();
	const FColor32 Edge = NPixelColor::SampleStep( GetPalette(), 0.8f );
	FVector2 From( Random.Range( -6.0f, 6.0f ), -BoltHeight );
	for ( int32 Segment = 1; Segment <= SegmentCount; ++Segment )
	{
		const float32 Along = static_cast<float32>( Segment ) / static_cast<float32>( SegmentCount );
		// 最後の点は、必ず基準点に落ちる
		const FVector2 To = Segment == SegmentCount ? FVector2() : FVector2( Random.Range( -7.0f, 7.0f ) * ( 1.0f - Along ), -BoltHeight * ( 1.0f - Along ) );
		Painter.Line( From, To, Edge, FPixelStroke { 1.0f, 3, 0.6f } );
		Painter.Line( From, To, Core );
		// 枝: ときどき、斜め下へ短く分かれる
		if ( Segment < SegmentCount - 1 && Random.Chance( 0.35f ) )
		{
			const FVector2 Branch = To + FVector2( static_cast<float32>( Random.Sign() ) * Random.Range( 5.0f, 11.0f ), Random.Range( 4.0f, 9.0f ) );
			Painter.Line( To, Branch, Edge, FPixelStroke { 0.8f, 1, 0.3f } );
		}
		From = To;
	}
	// 落ちた所の光と、地面の火花の輪
	Painter.Glow( FVector2(), 18.0f, Edge, 0.9f * ( 1.0f - Progress ) );
	Painter.Arc( FVector2(), 3.0f + Progress * 10.0f, NMath::PI, NMath::TWO_PI, Edge, FPixelStroke { 1.0f - Progress, 1, 0.3f } );
}
