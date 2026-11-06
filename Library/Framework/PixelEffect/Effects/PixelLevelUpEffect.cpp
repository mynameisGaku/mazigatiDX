// SPDX-License-Identifier: MIT
#include "Library/Framework/PixelEffect/Effects/PixelLevelUpEffect.h"

#include "Library/Framework/PixelEffect/Canvas/PixelColor.h"
#include "Library/Framework/PixelEffect/Math/PixelMath.h"
#include "Library/Math/MathUtils.h"

namespace
{
	constexpr int32 PillarCount = 6;
	// 光の柱 1 本が昇って消えるまで（秒）
	constexpr float32 PillarTime = 0.6f;
	// 文字が現れる時刻と、跳ねる長さ（秒）
	constexpr float32 TextStart = 0.15f;
	constexpr float32 TextBounce = 0.25f;
	// 文字の置き場所（足元からの高さ）
	constexpr float32 TextHeight = 34.0f;
	constexpr int32 SparkCount = 10;

	FPixelEffectMeta MakeMeta()
	{
		FPixelEffectMeta Meta;
		Meta.Name = "LevelUp";
		Meta.Category = "Text";
		Meta.Anchor = FVector2( 48.0f, 82.0f );
		Meta.Duration = 1.6f;
		Meta.DefaultPalette = EPixelPalette::Gold;
		return Meta;
	}
}

CPixelLevelUpEffect::CPixelLevelUpEffect()
	: CPixelTimedEffect( MakeMeta() )
{
}

void CPixelLevelUpEffect::OnDraw( const CPixelPainter& Painter, EPixelLayer Layer ) const
{
	const float32 Time = GetTime();
	const int32 Seed = static_cast<int32>( GetPlacement().Seed );
	const float32 Fade = NPixelMath::Clamp01( ( GetMeta().Duration - Time ) / 0.3f );
	if ( Layer == EPixelLayer::Back )
	{
		for ( int32 Index = 0; Index < PillarCount; ++Index )
		{
			// 柱ごとに、始まる時刻と横の位置をずらす
			const float32 Start = static_cast<float32>( Index ) * 0.12f;
			const float32 Rise = ( Time - Start ) / PillarTime;
			if ( Rise <= 0.0f || Rise >= 1.0f )
			{
				continue;
			}
			const float32 X = ( NPixelMath::RandomUnit( Index, Seed ) - 0.5f ) * 22.0f;
			const float32 Top = -NPixelMath::EaseOutQuad( Rise ) * 40.0f;
			const float32 Bottom = Top + 12.0f * ( 1.0f - Rise ) + 4.0f;
			Painter.Line( FVector2( X, Bottom ), FVector2( X, Top ), NPixelColor::SampleStep( GetPalette(), 0.65f ), FPixelStroke { 1.0f - Rise, 2, 0.4f } );
		}
		Painter.Glow( FVector2( 0.0f, -12.0f ), 26.0f, NPixelColor::SampleStep( GetPalette(), 0.8f ), 0.4f * Fade );
		return;
	}
	if ( Time < TextStart )
	{
		return;
	}
	// 文字: 下から跳ね上がって、少し戻って止まる
	const float32 Bounce = NPixelMath::Clamp01( ( Time - TextStart ) / TextBounce );
	const float32 Overshoot = NMath::Sin( Bounce * NMath::PI ) * 5.0f;
	FPixelTextStyle Style;
	Style.Font = EPixelFont::Big;
	Style.Alignment = EPixelTextAlign::Center;
	Style.bOutline = true;
	Style.OutlineColor = NPixelColor::Ink();
	if ( Fade < 1.0f && GetStepCount() % 6 < 3 )
	{
		return;
	}
	Painter.Text( "LEVEL UP", FVector2( 0.0f, -TextHeight - Bounce * 6.0f - Overshoot ), NPixelColor::SampleStep( GetPalette(), 0.75f ), Style );
	// きらめき: 文字の周りに、時刻と番号で瞬く十字
	for ( int32 Index = 0; Index < SparkCount; ++Index )
	{
		const float32 Blink = NPixelMath::Wrap( Time * 2.5f + NPixelMath::RandomUnit( Index, Seed, 3 ), 1.0f );
		if ( Blink > 0.3f )
		{
			continue;
		}
		const FVector2 Position( ( NPixelMath::RandomUnit( Index, Seed, 4 ) - 0.5f ) * 56.0f, -TextHeight - 4.0f + ( NPixelMath::RandomUnit( Index, Seed, 5 ) - 0.5f ) * 22.0f );
		const float32 Arm = Blink < 0.15f ? 2.0f : 1.0f;
		Painter.Line( Position - FVector2( Arm, 0.0f ), Position + FVector2( Arm, 0.0f ), NPixelColor::White(), FPixelStroke { 1.0f, 1, 0.4f } );
		Painter.Line( Position - FVector2( 0.0f, Arm ), Position + FVector2( 0.0f, Arm ), NPixelColor::White(), FPixelStroke { 1.0f, 1, 0.4f } );
	}
}
