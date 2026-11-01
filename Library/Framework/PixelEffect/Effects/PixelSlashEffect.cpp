// SPDX-License-Identifier: MIT
#include "Library/Framework/PixelEffect/Effects/PixelSlashEffect.h"

#include "Library/Framework/PixelEffect/PixelColor.h"
#include "Library/Framework/PixelEffect/PixelMath.h"
#include "Library/Math/MathUtils.h"

namespace
{
	constexpr float32 Radius = 24.0f;
	// 振りの始まりと終わりの角度（ラジアン。画面の下向きが正 = 時計回り）
	constexpr float32 SwingStart = -NMath::PI * 0.42f;
	constexpr float32 SwingEnd = NMath::PI * 0.62f;

	FPixelEffectMeta MakeMeta()
	{
		FPixelEffectMeta Meta;
		Meta.Name = "Slash";
		Meta.Category = "Combat";
		Meta.Duration = 0.35f;
		Meta.DefaultPalette = EPixelPalette::Metal;
		return Meta;
	}
}

CPixelSlashEffect::CPixelSlashEffect()
	: CPixelTimedEffect( MakeMeta() )
{
}

void CPixelSlashEffect::OnDraw( const CPixelPainter& Painter, EPixelLayer Layer ) const
{
	if ( Layer != EPixelLayer::Front )
	{
		return;
	}
	const float32 Progress = GetProgress();
	const float32 Span = SwingEnd - SwingStart;
	// 刃先は前半で振り切り、尾は後半で追いつく
	const float32 Head = SwingStart + Span * NPixelMath::EaseOutCubic( NMath::Min( 1.0f, Progress * 1.6f ) );
	const float32 Tail = SwingStart + Span * NPixelMath::EaseInQuad( Progress );
	if ( Head - Tail <= 0.01f )
	{
		return;
	}
	// 外側ほど明るく太い 3 本の弧を重ねて、三日月の形にする
	FPixelStroke Outer;
	Outer.Width = 2;
	Outer.Glow = 0.4f * ( 1.0f - Progress );
	Painter.Arc( FVector2(), Radius, Tail, Head, NPixelColor::SampleStep( GetPalette(), 0.75f ), Outer );
	Painter.Arc( FVector2(), Radius + 1.5f, Tail + ( Head - Tail ) * 0.3f, Head, NPixelColor::White(), FPixelStroke { 1.0f, 1, 0.5f } );
	Painter.Arc( FVector2(), Radius - 2.5f, Tail + ( Head - Tail ) * 0.5f, Head, NPixelColor::SampleStep( GetPalette(), 0.45f ), FPixelStroke { 0.6f, 1, 0.0f } );
}
