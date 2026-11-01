// SPDX-License-Identifier: MIT
#include "Library/Framework/PixelEffect/Effects/PixelDashEffect.h"

#include "Library/Framework/PixelEffect/PixelColor.h"
#include "Library/Framework/PixelEffect/PixelMath.h"
#include "Library/Math/MathUtils.h"

namespace
{
	constexpr int32 GhostCount = 3;
	// 残像どうしの間隔（画素。始めの値）
	constexpr float32 GhostSpacing = 14.0f;
	constexpr int32 LineCount = 5;

	FPixelEffectMeta MakeMeta()
	{
		FPixelEffectMeta Meta;
		Meta.Name = "Dash";
		Meta.Category = "Combat";
		Meta.Anchor = FVector2( 64.0f, 80.0f );
		Meta.Duration = 0.5f;
		Meta.DefaultPalette = EPixelPalette::Cyber;
		return Meta;
	}
}

CPixelDashEffect::CPixelDashEffect( const FPixelMask& InSilhouette )
	: CPixelTimedEffect( MakeMeta() )
	, Silhouette( InSilhouette )
{
}

void CPixelDashEffect::OnDraw( const CPixelPainter& Painter, EPixelLayer Layer ) const
{
	if ( Layer != EPixelLayer::Back || !Silhouette.IsValid() )
	{
		return;
	}
	const float32 Progress = GetProgress();
	// 残像は、本体へ追いつくように間隔が縮み、薄れる
	const float32 Spacing = GhostSpacing * ( 1.0f - NPixelMath::EaseInQuad( Progress ) );
	const FVector2 TopLeft( -static_cast<float32>( Silhouette.Width / 2 ), -static_cast<float32>( Silhouette.Height ) );
	for ( int32 Ghost = GhostCount; Ghost >= 1; --Ghost )
	{
		const float32 Far = static_cast<float32>( Ghost ) / static_cast<float32>( GhostCount );
		const FColor32 Color = NPixelColor::SampleStep( GetPalette(), 0.9f - Far * 0.5f );
		const FPixelStroke Stroke { ( 1.0f - Far * 0.6f ) * ( 1.0f - Progress ), 1, 0.15f };
		Painter.Mask( Silhouette, TopLeft - FVector2( Spacing * static_cast<float32>( Ghost ), 0.0f ), Color, Stroke );
	}
	// 速さの線: 体の高さの中で、後ろへ流れる
	const int32 Seed = static_cast<int32>( GetPlacement().Seed );
	for ( int32 Index = 0; Index < LineCount; ++Index )
	{
		const float32 Y = -NPixelMath::RandomUnit( Index, Seed ) * static_cast<float32>( Silhouette.Height );
		const float32 Start = -8.0f - NPixelMath::RandomUnit( Index, Seed, 1 ) * 10.0f - Progress * 30.0f;
		const float32 LineLength = 10.0f + NPixelMath::RandomUnit( Index, Seed, 2 ) * 14.0f;
		Painter.Line( FVector2( Start, Y ), FVector2( Start - LineLength, Y ), NPixelColor::SampleStep( GetPalette(), 0.8f ), FPixelStroke { 1.0f - Progress, 1, 0.2f } );
	}
}
