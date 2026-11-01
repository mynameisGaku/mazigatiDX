// SPDX-License-Identifier: MIT
#include "Library/Framework/PixelEffect/PixelEffectView.h"

#include "Library/Framework/Asset/AssetManager.h"
#include "Library/Framework/Render/DrawContext.h"

namespace
{
	// 画像の名前に付ける通し番号（同じ名前の画像は共有されるので、見本ごとに別の名前にする。メインスレッドだけ）
	int32 NextSerial = 0;

	// 層の画像を、基準点が Anchor に来るよう Zoom 倍で描く
	void DrawLayer( FDrawContext& CommandList, const TSharedPtr<CDynamicTexture>& Texture, const FPixelEffectMeta& Meta, const FVector2& Anchor, float32 Zoom )
	{
		if ( !Texture.IsValid() || !( Zoom > 0.0f ) )
		{
			return;
		}
		const FVector2 TopLeft = Anchor - FVector2( Meta.Anchor.x * Zoom, Meta.Anchor.y * Zoom );
		CommandList.ImageStretch( TopLeft.x, TopLeft.y, TopLeft.x + static_cast<float32>( Meta.Width ) * Zoom, TopLeft.y + static_cast<float32>( Meta.Height ) * Zoom, Texture );
	}
}

CPixelEffectView::CPixelEffectView( CAssetManager& Assets, TUniquePtr<IPixelEffect> InEffect )
	: Player( MoveTemp( InEffect ) )
{
	if ( !Player.IsValid() )
	{
		return;
	}
	++NextSerial;
	const FString Name = FString::Printf( "PixelEffect/%s#%d", *Player.GetMeta().Name, NextSerial );
	BackTexture = Assets.CreateDynamicTexture( Name + "/Back", Player.GetLayerImage( EPixelLayer::Back ) );
	FrontTexture = Assets.CreateDynamicTexture( Name + "/Front", Player.GetLayerImage( EPixelLayer::Front ) );
}

void CPixelEffectView::Play( uint32 Seed, bool bFlipHorizontal, const TArray<FColor32>& Palette )
{
	Player.Play( Seed, bFlipHorizontal, Palette );
	Update( 0.0f );
}

void CPixelEffectView::Update( float32 DeltaTime )
{
	Player.Update( DeltaTime );
	if ( BackTexture.IsValid() )
	{
		BackTexture->Update( Player.GetLayerImage( EPixelLayer::Back ) );
	}
	if ( FrontTexture.IsValid() )
	{
		FrontTexture->Update( Player.GetLayerImage( EPixelLayer::Front ) );
	}
}

void CPixelEffectView::DrawBack( FDrawContext& CommandList, const FVector2& Anchor, float32 Zoom ) const
{
	DrawLayer( CommandList, BackTexture, Player.GetMeta(), Anchor, Zoom );
}

void CPixelEffectView::DrawFront( FDrawContext& CommandList, const FVector2& Anchor, float32 Zoom ) const
{
	DrawLayer( CommandList, FrontTexture, Player.GetMeta(), Anchor, Zoom );
}
