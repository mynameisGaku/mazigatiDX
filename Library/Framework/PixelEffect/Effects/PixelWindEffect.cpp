// SPDX-License-Identifier: MIT
#include "Library/Framework/PixelEffect/Effects/PixelWindEffect.h"

#include "Library/Framework/PixelEffect/PixelColor.h"
#include "Library/Framework/PixelEffect/PixelMath.h"
#include "Library/Math/MathUtils.h"

namespace
{
	constexpr int32 StreakCount = 5;
	constexpr int32 LeafCount = 3;
	constexpr int32 StreakLength = 22;
	// 筋と葉が 1 周で渡る幅（描き先の幅 + 筋の長さ。基準点からの相対で -Span / 2 〜 Span / 2）
	constexpr float32 Span = 96.0f + static_cast<float32>( StreakLength ) * 2.0f;

	FPixelEffectMeta MakeMeta()
	{
		FPixelEffectMeta Meta;
		Meta.Name = "Wind";
		Meta.Category = "Element";
		Meta.Duration = 1.2f;
		Meta.bLoop = true;
		Meta.DefaultPalette = EPixelPalette::Ice;
		return Meta;
	}
}

CPixelWindEffect::CPixelWindEffect()
	: CPixelTimedEffect( MakeMeta() )
{
}

void CPixelWindEffect::OnDraw( const CPixelPainter& Painter, EPixelLayer Layer ) const
{
	if ( Layer != EPixelLayer::Front )
	{
		return;
	}
	const float32 Progress = GetProgress();
	const int32 Seed = static_cast<int32>( GetPlacement().Seed );
	for ( int32 Index = 0; Index < StreakCount; ++Index )
	{
		const float32 BaseY = ( NPixelMath::RandomUnit( Index, Seed ) - 0.5f ) * 56.0f;
		const float32 Head = NPixelMath::Wrap( Progress + NPixelMath::RandomUnit( Index, Seed, 1 ), 1.0f ) * Span - Span * 0.5f;
		// 筋は、頭から尾へ向かって薄くなる点の並び。y は x に沿った正弦でうねる
		for ( int32 Step = 0; Step < StreakLength; ++Step )
		{
			const float32 X = Head - static_cast<float32>( Step );
			const float32 Y = BaseY + NMath::Sin( X * 0.12f + static_cast<float32>( Index ) ) * 3.0f;
			const float32 Tail = static_cast<float32>( Step ) / static_cast<float32>( StreakLength );
			Painter.Dot( FVector2( X, Y ), NPixelColor::SampleStep( GetPalette(), 0.95f - Tail * 0.4f ), FPixelStroke { 1.0f - Tail, 1, 0.0f } );
		}
	}
	const FColor32 Leaf = NPixelColor::FromHex( "#5aa84a" );
	const FColor32 LeafDark = NPixelColor::FromHex( "#2f6b34" );
	for ( int32 Index = 0; Index < LeafCount; ++Index )
	{
		const float32 X = NPixelMath::Wrap( Progress + NPixelMath::RandomUnit( Index, Seed, 2 ), 1.0f ) * Span - Span * 0.5f;
		const float32 Y = ( NPixelMath::RandomUnit( Index, Seed, 3 ) - 0.5f ) * 40.0f + NMath::Sin( X * 0.08f + static_cast<float32>( Index ) * 2.0f ) * 6.0f;
		// 葉は 3 画素。1 周で 4 回、向きが回る
		const int32 Turn = static_cast<int32>( Progress * 16.0f + static_cast<float32>( Index ) ) % 4;
		const FVector2 Center( X, Y );
		const FVector2 Offset = Turn == 0 ? FVector2( 1.0f, 0.0f ) : ( Turn == 1 ? FVector2( 1.0f, 1.0f ) : ( Turn == 2 ? FVector2( 0.0f, 1.0f ) : FVector2( -1.0f, 1.0f ) ) );
		Painter.Dot( Center, Leaf );
		Painter.Dot( Center + Offset, Leaf );
		Painter.Dot( Center - Offset, LeafDark );
	}
}
