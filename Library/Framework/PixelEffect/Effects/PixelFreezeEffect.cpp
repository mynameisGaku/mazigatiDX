// SPDX-License-Identifier: MIT
#include "Library/Framework/PixelEffect/Effects/PixelFreezeEffect.h"

#include "Library/Framework/PixelEffect/PixelColor.h"
#include "Library/Framework/PixelEffect/PixelMath.h"
#include "Library/Math/MathUtils.h"

namespace
{
	constexpr int32 ShardCount = 7;
	// 1 本の結晶が伸びきるまで（秒）
	constexpr float32 GrowTime = 0.15f;
	// 薄れ始める時刻（秒）
	constexpr float32 FadeStart = 0.85f;

	FPixelEffectMeta MakeMeta()
	{
		FPixelEffectMeta Meta;
		Meta.Name = "Freeze";
		Meta.Category = "Element";
		Meta.Anchor = FVector2( 48.0f, 80.0f );
		Meta.Duration = 1.2f;
		Meta.DefaultPalette = EPixelPalette::Ice;
		return Meta;
	}
}

CPixelFreezeEffect::CPixelFreezeEffect()
	: CPixelTimedEffect( MakeMeta() )
{
}

void CPixelFreezeEffect::OnStart( FPixelRandom& Random )
{
	Shards.Reset();
	for ( int32 Index = 0; Index < ShardCount; ++Index )
	{
		// 真ん中ほど高く、外ほど低く、外から内へ順に突き出す
		const float32 Along = ( static_cast<float32>( Index ) + 0.5f ) / static_cast<float32>( ShardCount ) * 2.0f - 1.0f;
		FShard Shard;
		Shard.X = Along * 16.0f + Random.Range( -1.5f, 1.5f );
		Shard.Height = ( 1.0f - NMath::Abs( Along ) * 0.55f ) * Random.Range( 16.0f, 24.0f );
		Shard.HalfWidth = Random.Range( 2.0f, 3.5f );
		Shard.Lean = Along * Random.Range( 3.0f, 6.0f );
		Shard.Delay = ( 1.0f - NMath::Abs( Along ) ) * 0.12f;
		Shards.Add( Shard );
	}
}

void CPixelFreezeEffect::OnDraw( const CPixelPainter& Painter, EPixelLayer Layer ) const
{
	if ( Layer != EPixelLayer::Front )
	{
		return;
	}
	const float32 Time = GetTime();
	const float32 Alpha = Time < FadeStart ? 1.0f : 1.0f - ( Time - FadeStart ) / ( GetMeta().Duration - FadeStart );
	for ( const FShard& Shard : Shards )
	{
		const float32 Grow = NPixelMath::EaseOutCubic( NPixelMath::Clamp01( ( Time - Shard.Delay ) / GrowTime ) );
		if ( Grow <= 0.0f )
		{
			continue;
		}
		// 結晶は、先端から左右の根元への 2 枚の三角（影の面と光の面）
		const FVector2 Tip( Shard.X + Shard.Lean * Grow, -Shard.Height * Grow );
		const FVector2 Left( Shard.X - Shard.HalfWidth, 0.0f );
		const FVector2 Middle( Shard.X, 0.0f );
		const FVector2 Right( Shard.X + Shard.HalfWidth, 0.0f );
		const FVector2 ShadowFace[] = { Tip, Left, Middle };
		const FVector2 LightFace[] = { Tip, Middle, Right };
		const FPixelStroke Stroke { Alpha, 1, 0.0f };
		Painter.Polygon( ShadowFace, NPixelColor::SampleStep( GetPalette(), 0.45f ), Stroke );
		Painter.Polygon( LightFace, NPixelColor::SampleStep( GetPalette(), 0.8f ), Stroke );
		Painter.Line( Tip, Middle, NPixelColor::White(), FPixelStroke { Alpha, 1, 0.3f } );
	}
	// 冷気のきらめき: 時刻と番号で、結晶の周りに瞬く
	const int32 Seed = static_cast<int32>( GetPlacement().Seed );
	for ( int32 Index = 0; Index < 6; ++Index )
	{
		const float32 Blink = NPixelMath::Wrap( Time * 3.0f + NPixelMath::RandomUnit( Index, Seed ), 1.0f );
		if ( Blink > 0.4f )
		{
			continue;
		}
		const FVector2 Position( ( NPixelMath::RandomUnit( Index, Seed, 1 ) - 0.5f ) * 36.0f, -NPixelMath::RandomUnit( Index, Seed, 2 ) * 24.0f );
		Painter.Dot( Position, NPixelColor::White(), FPixelStroke { Alpha, 1, 0.5f } );
	}
	Painter.Glow( FVector2( 0.0f, -8.0f ), 22.0f, NPixelColor::SampleStep( GetPalette(), 0.7f ), 0.3f * Alpha );
}
