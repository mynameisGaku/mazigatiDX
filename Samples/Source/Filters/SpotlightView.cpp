#include "SpotlightView.h"

#include "Library/Framework/Render/DrawContext.h"

namespace
{
	// 見出しから、絵までの高さ（ピクセル）
	constexpr int32 TitleHeight = 24;
	// スポットライトの動く範囲の半径（ピクセル。描画先の中心から）
	constexpr float32 MoveRadius = 40.0f;
	// 暗い部屋の色
	const FColor32 RoomColor( 16, 16, 28, 255 );

	// 画面の座標（float）を、整数の座標を取る描画に渡すために、一番近いピクセルに丸める
	FORCEINLINE int32 Px( float32 Value )
	{
		return NMath::RoundToInt( Value );
	}
}

CSpotlightView::CSpotlightView( const FVector2& InOrigin, const TSharedPtr<CDynamicTexture>& InScene, const TSharedPtr<CDynamicTexture>& InGlow, const TSharedPtr<CRenderTarget>& InMask, const TSharedPtr<CRenderTarget>& InView )
	: Origin( InOrigin )
	, Scene( InScene )
	, Glow( InGlow )
	, Mask( InMask )
	, View( InView )
{
}

void CSpotlightView::Tick( float32 DeltaTime )
{
	Time += DeltaTime;
}

void CSpotlightView::Draw( FDrawContext& CommandList )
{
	const int32 Left = Px( Origin.x );
	const int32 Top = Px( Origin.y ) + TitleHeight;
	CommandList.Text( Left, Px( Origin.y ), "FMaskScope( CommandList, MaskImage )", FColor32::White );
	if ( !Scene.IsValid() || !Glow.IsValid() || !Mask.IsValid() || !View.IsValid() )
	{
		return;
	}

	// 光の位置（マスクの画像の中の座標）。描画先の中心のまわりを回る
	const float32 Center = static_cast<float32>( ViewSize ) * 0.5f;
	const int32 GlowX = Px( Center + MoveRadius * NMath::Cos( Time * 1.2f ) - static_cast<float32>( GlowSize ) * 0.5f );
	const int32 GlowY = Px( Center + MoveRadius * NMath::Sin( Time * 1.7f ) - static_cast<float32>( GlowSize ) * 0.5f );

	// 1. マスクの画像: 透明に消して、光の画像を、そのまま（透明度も）置く
	{
		FTargetScope Scope( CommandList, Mask );
		CommandList.Clear();
		CommandList.CustomBlend( FCustomBlend() );
		CommandList.Image( static_cast<float32>( GlowX ), static_cast<float32>( GlowY ), Glow );
		CommandList.Blend( EDrawBlend::Opaque );
	}

	// 2. 画面に出す描画先: 暗い部屋の色で塗って、マスクの中に絵を描く（マスクの値が大きいほど濃く見える）。描画先を替えてから、マスクを掛ける
	{
		FTargetScope Scope( CommandList, View );
		CommandList.Clear( RoomColor );
		FMaskScope Spot( CommandList, Mask );
		CommandList.ImageStretch( 0.0f, 0.0f, static_cast<float32>( ViewSize ), static_cast<float32>( ViewSize ), Scene );
	}

	// 3. ふつうの画像として貼る
	CommandList.Image( static_cast<float32>( Left ), static_cast<float32>( Top ), View );
	CommandList.Box( Left, Top, Left + ViewSize, Top + ViewSize, FColor32( 90, 90, 90 ), false );
	CommandList.Text( Left, Top + ViewSize + 4, "Mask (alpha): the light image moves in it", FColor32::Gray );
}
