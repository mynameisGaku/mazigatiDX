// SPDX-License-Identifier: MIT
#include "Library/Framework/PixelEffect/Effects/PixelWarpEffect.h"

#include "Library/Framework/PixelEffect/Canvas/PixelColor.h"
#include "Library/Framework/PixelEffect/Math/PixelMath.h"
#include "Library/Math/MathUtils.h"

namespace
{
	constexpr float32 ColumnHeight = 56.0f;
	constexpr float32 MaxHalfWidth = 9.0f;
	constexpr int32 MoteCount = 18;
	constexpr int32 ScanCount = 3;

	FPixelEffectMeta MakeMeta()
	{
		FPixelEffectMeta Meta;
		Meta.Name = "Warp";
		Meta.Category = "Magic";
		Meta.Anchor = FVector2( 48.0f, 82.0f );
		Meta.Duration = 1.0f;
		Meta.DefaultPalette = EPixelPalette::Cyber;
		return Meta;
	}
}

CPixelWarpEffect::CPixelWarpEffect()
	: CPixelTimedEffect( MakeMeta() )
{
}

void CPixelWarpEffect::OnStart( FPixelRandom& Random )
{
	Motes.Clear();
	for ( int32 Index = 0; Index < MoteCount; ++Index )
	{
		FPixelParticle Mote;
		Mote.Position = FVector2( Random.Range( -MaxHalfWidth, MaxHalfWidth ), Random.Range( -6.0f, 0.0f ) );
		Mote.Velocity = FVector2( 0.0f, Random.Range( -70.0f, -30.0f ) );
		Mote.Life = Random.Range( 0.5f, 0.95f );
		Mote.Heat = Random.Range( 0.7f, 1.0f );
		Mote.Glow = 0.4f;
		Mote.Shape = Random.Chance( 0.5f ) ? EPixelParticleShape::Streak : EPixelParticleShape::Dot;
		Motes.Add( Mote );
	}
}

void CPixelWarpEffect::OnStep( float32 InStepTime )
{
	Motes.Step( InStepTime );
}

void CPixelWarpEffect::OnDraw( const CPixelPainter& Painter, EPixelLayer Layer ) const
{
	const float32 Progress = GetProgress();
	// 柱の太さ: 前半で開き、後半で閉じる
	const float32 Open = NMath::Sin( Progress * NMath::PI );
	const float32 HalfWidth = NMath::Floor( MaxHalfWidth * Open + 0.5f );
	if ( Layer == EPixelLayer::Back )
	{
		Painter.Ellipse( FVector2(), FVector2( MaxHalfWidth + 4.0f, 4.0f ), NPixelColor::SampleStep( GetPalette(), 0.7f ), FPixelStroke { Open, 1, 0.3f } );
		return;
	}
	if ( HalfWidth >= 1.0f )
	{
		// 柱は、外側を薄く、芯を明るく
		Painter.Rect( FVector2( -HalfWidth, -ColumnHeight ), FVector2( HalfWidth * 2.0f, ColumnHeight ), NPixelColor::SampleStep( GetPalette(), 0.7f ), FPixelStroke { 0.4f, 1, 0.2f } );
		const float32 Core = NMath::Max( 1.0f, NMath::Floor( HalfWidth * 0.4f ) );
		Painter.Rect( FVector2( -Core, -ColumnHeight ), FVector2( Core * 2.0f, ColumnHeight ), NPixelColor::White(), FPixelStroke { 1.0f, 1, 0.3f } );
	}
	// 走る輪: 間をあけて、下から上へ
	for ( int32 Index = 0; Index < ScanCount; ++Index )
	{
		const float32 Scan = NPixelMath::Wrap( Progress * 2.0f + static_cast<float32>( Index ) / static_cast<float32>( ScanCount ), 1.0f );
		Painter.Ellipse( FVector2( 0.0f, -Scan * ColumnHeight ), FVector2( MaxHalfWidth + 3.0f, 3.0f ), NPixelColor::SampleStep( GetPalette(), 0.9f ), FPixelStroke { Open * ( 1.0f - Scan ), 1, 0.3f } );
	}
	Motes.Draw( Painter, GetPalette(), Layer );
	Painter.Glow( FVector2( 0.0f, -ColumnHeight * 0.4f ), 30.0f, NPixelColor::SampleStep( GetPalette(), 0.8f ), 0.4f * Open );
}
