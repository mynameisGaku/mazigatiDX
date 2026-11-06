// SPDX-License-Identifier: MIT
#include "Library/Framework/PixelEffect/Effects/PixelCoinEffect.h"

#include "Library/Framework/PixelEffect/Canvas/PixelColor.h"
#include "Library/Framework/PixelEffect/Math/PixelMath.h"
#include "Library/Math/MathUtils.h"

namespace
{
	// 硬貨が頂点に届く時刻（秒）と高さ（画素）
	constexpr float32 RiseTime = 0.35f;
	constexpr float32 RiseHeight = 22.0f;
	// 硬貨の半径（横は回転で縮む）と、1 秒あたりの回転
	constexpr float32 CoinRadius = 5.0f;
	constexpr float32 SpinPerSecond = 3.0f;

	FPixelEffectMeta MakeMeta()
	{
		FPixelEffectMeta Meta;
		Meta.Name = "Coin";
		Meta.Category = "Pickup";
		Meta.Anchor = FVector2( 48.0f, 72.0f );
		Meta.Duration = 0.9f;
		Meta.DefaultPalette = EPixelPalette::Gold;
		return Meta;
	}
}

CPixelCoinEffect::CPixelCoinEffect()
	: CPixelTimedEffect( MakeMeta() )
{
}

void CPixelCoinEffect::OnDraw( const CPixelPainter& Painter, EPixelLayer Layer ) const
{
	if ( Layer != EPixelLayer::Front )
	{
		return;
	}
	const float32 Time = GetTime();
	if ( Time < RiseTime )
	{
		// 回る硬貨: 横の幅が |cos| で縮み、細いときは縁の暗い色だけ見える
		const float32 Height = RiseHeight * NPixelMath::EaseOutQuad( Time / RiseTime );
		const float32 Spin = NMath::Abs( NMath::Cos( Time * SpinPerSecond * NMath::TWO_PI ) );
		const float32 HalfWidth = NMath::Max( 1.0f, NMath::Floor( CoinRadius * Spin + 0.5f ) );
		const FVector2 Center( 0.0f, -CoinRadius - Height );
		Painter.Rect( Center - FVector2( HalfWidth, CoinRadius ), FVector2( HalfWidth * 2.0f, CoinRadius * 2.0f ), NPixelColor::SampleStep( GetPalette(), 0.45f ) );
		if ( HalfWidth > 1.0f )
		{
			Painter.Rect( Center - FVector2( HalfWidth - 1.0f, CoinRadius - 1.0f ), FVector2( HalfWidth * 2.0f - 2.0f, CoinRadius * 2.0f - 2.0f ), NPixelColor::SampleStep( GetPalette(), 0.8f ) );
			Painter.Line( Center + FVector2( -HalfWidth + 2.0f, -CoinRadius + 2.0f ), Center + FVector2( -HalfWidth + 2.0f, -1.0f ), NPixelColor::White() );
		}
		return;
	}
	// きらめき: 頂点で 4 方向に光が走る
	const float32 Burst = NPixelMath::Clamp01( ( Time - RiseTime ) / 0.25f );
	const FVector2 Top( 0.0f, -CoinRadius - RiseHeight );
	if ( Burst < 1.0f )
	{
		const float32 Arm = 2.0f + Burst * 7.0f;
		const FPixelStroke Stroke { 1.0f - Burst, 1, 0.5f };
		const FColor32 Bright = NPixelColor::SampleStep( GetPalette(), 1.0f );
		Painter.Line( Top - FVector2( Arm, 0.0f ), Top + FVector2( Arm, 0.0f ), Bright, Stroke );
		Painter.Line( Top - FVector2( 0.0f, Arm ), Top + FVector2( 0.0f, Arm ), Bright, Stroke );
		Painter.Glow( Top, 10.0f, Bright, 0.7f * ( 1.0f - Burst ) );
	}
	// 「+1」が昇って、最後は点滅で消える
	const float32 Float = ( Time - RiseTime ) / ( GetMeta().Duration - RiseTime );
	if ( Float > 0.7f && GetStepCount() % 6 < 3 )
	{
		return;
	}
	FPixelTextStyle Style;
	Style.Alignment = EPixelTextAlign::Center;
	Style.bOutline = true;
	Style.OutlineColor = NPixelColor::Ink();
	Painter.Text( "+1", Top - FVector2( 0.0f, 6.0f + Float * 10.0f ), NPixelColor::SampleStep( GetPalette(), 0.9f ), Style );
}
