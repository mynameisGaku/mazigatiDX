#include "Coin.h"

#include "Source/Village/VillageArt.h"

namespace
{
	// 拾える距離（ピクセル）
	constexpr float32 PickupDistance = 20.0f;

	// 拾われたあと、輪が広がって消えるまでの時間（秒）と、輪の最後の半径（ピクセル）
	constexpr float32 RingDuration = 0.4f;
	constexpr float32 RingRadius = 44.0f;

	// 金貨の見た目の大きさ（ピクセル。画像を拡大して描く）と、浮き沈みの大きさ
	constexpr float32 DrawSize = 24.0f;
	constexpr float32 BobHeight = 3.0f;
}

CCoin::CCoin( const TSharedPtr<FVillageState>& InState, int32 InIndex, const FVector2& InPosition, const TSharedPtr<CTexture>& InCoin, const TSharedPtr<CTexture>& InGlow )
	: State( InState )
	, Index( InIndex )
	, Position( InPosition )
	, Coin( InCoin )
	, Glow( InGlow )
	, Phase( static_cast<float32>( InIndex ) * 0.7f )
{
}

void CCoin::Tick( float32 DeltaTime )
{
	Phase += DeltaTime;
	if ( bCollected )
	{
		CollectedTime += DeltaTime;
		if ( CollectedTime >= RingDuration )
		{
			Destroy();
		}
		return;
	}
	if ( !State.IsValid() )
	{
		return;
	}
	const FVector2 Delta = State->PlayerPosition - FVector2( Position.x, Position.y + 8.0f );
	if ( Delta.x * Delta.x + Delta.y * Delta.y <= PickupDistance * PickupDistance )
	{
		bCollected = true;
		State->Coins += 1;
		if ( State->CoinCollected.IsValidIndex( Index ) )
		{
			State->CoinCollected[Index] = true;
		}
	}
}

void CCoin::Draw( FDrawContext& CommandList )
{
	if ( !State.IsValid() || !Coin.IsValid() || !Glow.IsValid() )
	{
		return;
	}
	FWorldScope World( CommandList, *State );

	if ( bCollected )
	{
		// 拾われた: 輪が広がりながら薄くなる（加算ブレンド）
		const float32 Progress = NMath::Saturate( CollectedTime / RingDuration );
		CommandList.Blend( EDrawBlend::Add, NMath::RoundToInt( 255.0f * ( 1.0f - Progress ) ) );
		CommandList.CircleAA( Position.x, Position.y, 8.0f + ( RingRadius - 8.0f ) * Progress, FColor32( 255, 224, 110 ), false, 3.0f );
		return;
	}

	// 光（金色にして、加算ブレンドで重ねる）
	const float32 Bob = NMath::Sin( Phase * 3.0f ) * BobHeight;
	const float32 CenterY = Position.y - 10.0f + Bob;
	CommandList.Tint( FColor32( 255, 208, 90 ) );
	CommandList.Blend( EDrawBlend::Add, 110 );
	CommandList.ImageStretch( Position.x - 22.0f, CenterY - 22.0f, Position.x + 22.0f, CenterY + 22.0f, Glow );
	CommandList.Blend( EDrawBlend::Opaque );
	CommandList.Tint( FColor32::White );

	// 金貨: 幅を縮めて、回っているように見せる（細くなりすぎないようにする）
	const float32 HalfWidth = DrawSize * 0.5f * NMath::Max( 0.18f, NMath::Abs( NMath::Cos( Phase * 3.0f ) ) );
	const float32 HalfHeight = DrawSize * 0.5f;
	CommandList.ImageStretch( Position.x - HalfWidth, CenterY - HalfHeight, Position.x + HalfWidth, CenterY + HalfHeight, Coin );
}
