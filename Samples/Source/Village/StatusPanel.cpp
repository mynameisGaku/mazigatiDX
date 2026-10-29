#include "StatusPanel.h"

#include "Source/Village/TileMap.h"
#include "Source/Village/VillageArt.h"

namespace
{
	// 左上の板の位置と大きさ（画面の座標。ピクセル）
	constexpr int32 PanelLeft = 16;
	constexpr int32 PanelTop = FVillageState::ViewTop + 8;
	constexpr int32 PanelWidth = 230;
	constexpr int32 PanelHeight = 92;

	// ミニマップの大きさ（地図の 1 タイルを、3 画素にする）と、位置（右上）
	constexpr int32 MinimapScale = 3;
	constexpr int32 MinimapWidth = CTileMap::Columns * MinimapScale;
	constexpr int32 MinimapHeight = CTileMap::Rows * MinimapScale;
	constexpr int32 MinimapLeft = FVillageState::ViewLeft + FVillageState::ViewWidth - 16 - MinimapWidth;
	constexpr int32 MinimapTop = FVillageState::ViewTop + 8;

	// 体力の輪の中心と、大きさ（輪の画像の大きさ）
	constexpr int32 RingCenterX = PanelLeft + 36;
	constexpr int32 RingCenterY = PanelTop + 62;

	// 世界の座標を、ミニマップの座標にする
	FVector2 ToMinimap( const FVector2& World )
	{
		const float32 X = World.x / static_cast<float32>( CTileMap::Columns * CTileMap::TileSize ) * static_cast<float32>( MinimapWidth );
		const float32 Y = World.y / static_cast<float32>( CTileMap::Rows * CTileMap::TileSize ) * static_cast<float32>( MinimapHeight );
		return FVector2( static_cast<float32>( MinimapLeft ) + X, static_cast<float32>( MinimapTop ) + Y );
	}
}

CStatusPanel::CStatusPanel( const TSharedPtr<FVillageState>& InState, const TSharedPtr<CFont>& InFont, const TSharedPtr<CTexture>& InCoin, const TSharedPtr<CTexture>& InRing, const TSharedPtr<CTexture>& InOverview )
	: State( InState )
	, Font( InFont )
	, Coin( InCoin )
	, Ring( InRing )
	, Overview( InOverview )
{
}

void CStatusPanel::DrawCounters( FDrawContext& CommandList ) const
{
	// 板（少し透けた、黒い角丸の箱）
	CommandList.Blend( EDrawBlend::Alpha, 150 );
	CommandList.RoundBox( PanelLeft, PanelTop, PanelLeft + PanelWidth, PanelTop + PanelHeight, 10, 10, FColor32::Black );
	CommandList.Blend( EDrawBlend::Opaque );

	// 金貨の数
	FTextStyle Style;
	Style.bShadow = true;
	Style.ShadowColor = FColor32( 0, 0, 0 );
	if ( Coin.IsValid() )
	{
		CommandList.ImageStretch( static_cast<float32>( PanelLeft + 14 ), static_cast<float32>( PanelTop + 10 ), static_cast<float32>( PanelLeft + 14 + 28 ), static_cast<float32>( PanelTop + 10 + 28 ), Coin );
	}
	if ( Font.IsValid() )
	{
		const FString Counter = FString::Printf( "x %d / %d", State->Coins, State->CoinPositions.Num() );
		Font->RecordStyled( CommandList, static_cast<float32>( PanelLeft + 56 ), static_cast<float32>( PanelTop + 11 ), *Counter, FColor32( 255, 232, 130 ), Style );
	}

	// 走るための体力（円グラフ。輪の一部だけを、真上から時計回りに描く。減ると、色が赤くなる）
	if ( Ring.IsValid() )
	{
		const float32 Stamina = State->Stamina;
		CommandList.Tint( FColor32( static_cast<uint8>( 255.0f - 135.0f * Stamina ), static_cast<uint8>( 80.0f + 140.0f * Stamina ), 90 ) );
		CommandList.CircleGauge( static_cast<float32>( RingCenterX ), static_cast<float32>( RingCenterY ), Stamina, Ring );
		CommandList.Tint( FColor32::White );
	}
	if ( Font.IsValid() )
	{
		Font->RecordStyled( CommandList, static_cast<float32>( PanelLeft + 72 ), static_cast<float32>( PanelTop + 52 ), "Stamina", FColor32( 210, 214, 230 ), Style );
		Font->RecordStyled( CommandList, static_cast<float32>( PanelLeft + 72 ), static_cast<float32>( PanelTop + 52 + 24 ), "Shift: run", FColor32( 150, 156, 180 ), Style );
	}
}

void CStatusPanel::DrawMinimap( FDrawContext& CommandList ) const
{
	// 地図の全体（1 タイル 1 画素の画像を、3 倍に拡大する。拡大しても、ぼかさない）
	if ( Overview.IsValid() )
	{
		CommandList.ImageStretch( static_cast<float32>( MinimapLeft ), static_cast<float32>( MinimapTop ), static_cast<float32>( MinimapLeft + MinimapWidth ), static_cast<float32>( MinimapTop + MinimapHeight ), Overview );
	}

	// まだ拾っていない金貨
	for ( int32 Index = 0; Index < State->CoinPositions.Num(); ++Index )
	{
		if ( State->CoinCollected.IsValidIndex( Index ) && State->CoinCollected[Index] )
		{
			continue;
		}
		const FVector2 Point = ToMinimap( State->CoinPositions[Index] );
		CommandList.Box( NMath::RoundToInt( Point.x ) - 1, NMath::RoundToInt( Point.y ) - 1, NMath::RoundToInt( Point.x ) + 2, NMath::RoundToInt( Point.y ) + 2, FColor32( 255, 224, 60 ) );
	}

	// いま見えている範囲の枠
	const FVector2 ViewMin = ToMinimap( State->CameraPosition );
	const FVector2 ViewMax = ToMinimap( State->CameraPosition + FVector2( static_cast<float32>( FVillageState::ViewWidth ), static_cast<float32>( FVillageState::ViewHeight ) ) );
	CommandList.Box( NMath::RoundToInt( ViewMin.x ), NMath::RoundToInt( ViewMin.y ), NMath::RoundToInt( ViewMax.x ), NMath::RoundToInt( ViewMax.y ), FColor32( 255, 255, 255 ), false );

	// 主人公（点滅する赤い点）
	const FVector2 Player = ToMinimap( State->PlayerPosition );
	const bool bBlink = ( static_cast<int32>( State->Time * 3.0f ) % 2 ) == 0;
	CommandList.Circle( NMath::RoundToInt( Player.x ), NMath::RoundToInt( Player.y ), 3, bBlink ? FColor32( 255, 60, 60 ) : FColor32( 255, 160, 160 ) );

	// 枠と、昼か夜か
	CommandList.Box( MinimapLeft - 1, MinimapTop - 1, MinimapLeft + MinimapWidth + 1, MinimapTop + MinimapHeight + 1, FColor32( 220, 224, 240 ), false );
	if ( Font.IsValid() )
	{
		FTextStyle Style;
		Style.bShadow = true;
		Style.ShadowColor = FColor32( 0, 0, 0 );
		Font->RecordStyled( CommandList, static_cast<float32>( MinimapLeft ), static_cast<float32>( MinimapTop + MinimapHeight + 6 ), State->Darkness > 0.5f ? "Night  (N: day)" : "Day  (N: night)", FColor32( 210, 214, 230 ), Style );
	}
}

void CStatusPanel::Draw( FDrawContext& CommandList )
{
	if ( !State.IsValid() )
	{
		return;
	}
	DrawCounters( CommandList );
	DrawMinimap( CommandList );
}
