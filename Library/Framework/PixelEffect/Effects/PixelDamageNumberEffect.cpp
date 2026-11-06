// SPDX-License-Identifier: MIT
#include "Library/Framework/PixelEffect/Effects/PixelDamageNumberEffect.h"

#include "Library/Framework/PixelEffect/Canvas/PixelColor.h"
#include "Library/Framework/PixelEffect/Math/PixelMath.h"
#include "Library/Math/MathUtils.h"

namespace
{
	// 跳ね上がる高さ（画素）と、その時間（秒）
	constexpr float32 JumpHeight = 16.0f;
	constexpr float32 JumpTime = 0.22f;
	// 落ちて落ち着く高さ
	constexpr float32 RestHeight = 11.0f;
	// 薄れ始める時刻
	constexpr float32 FadeStart = 0.7f;

	FPixelEffectMeta MakeMeta()
	{
		FPixelEffectMeta Meta;
		Meta.Name = "DamageNumber";
		Meta.Category = "Text";
		Meta.Anchor = FVector2( 48.0f, 64.0f );
		Meta.Duration = 1.0f;
		Meta.DefaultPalette = EPixelPalette::Blood;
		return Meta;
	}
}

CPixelDamageNumberEffect::CPixelDamageNumberEffect( int32 InValue, bool bInCritical )
	: CPixelTimedEffect( MakeMeta() )
	, Text( FString::Printf( "%d", InValue ) )
	, bCritical( bInCritical )
{
}

void CPixelDamageNumberEffect::OnStart( FPixelRandom& Random )
{
	OffsetX = static_cast<float32>( Random.RangeInt( -4, 4 ) );
}

void CPixelDamageNumberEffect::OnDraw( const CPixelPainter& Painter, EPixelLayer Layer ) const
{
	if ( Layer != EPixelLayer::Front )
	{
		return;
	}
	const float32 Time = GetTime();
	float32 Height = 0.0f;
	if ( Time < JumpTime )
	{
		Height = JumpHeight * NPixelMath::EaseOutQuad( Time / JumpTime );
	}
	else
	{
		const float32 Fall = NMath::Min( 1.0f, ( Time - JumpTime ) / 0.2f );
		Height = NPixelMath::Lerp( JumpHeight, RestHeight, NPixelMath::EaseInQuad( Fall ) );
	}
	FPixelTextStyle Style;
	Style.Font = bCritical ? EPixelFont::Big : EPixelFont::Mini;
	Style.Scale = bCritical ? 2 : 1;
	Style.Alignment = EPixelTextAlign::Center;
	Style.bOutline = true;
	Style.OutlineColor = NPixelColor::Ink();
	const FColor32 Color = bCritical ? NPixelColor::FromHex( "#ffd84a" ) : NPixelColor::SampleStep( GetPalette(), 0.85f );
	const float32 Top = -Height - static_cast<float32>( Style.Scale * ( bCritical ? 7 : 5 ) );
	// 薄れるのは、文字の画素をディザで間引く代わりに、点滅で表す（文字はディザで欠けると読めない）
	if ( Time > FadeStart && GetStepCount() % 8 < 3 )
	{
		return;
	}
	Painter.Text( Text, FVector2( OffsetX, Top ), Color, Style );
}
