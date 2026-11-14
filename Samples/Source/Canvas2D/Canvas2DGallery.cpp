// SPDX-License-Identifier: MIT
#include "Canvas2DGallery.h"
#include "Library/Framework/Engine/Engine.h"
#include "Library/Framework/Asset/AssetSubsystem.h"
#include "Library/Framework/Render/Draw/DrawContext.h"

void CCanvas2DGallery::OnSpawn()
{
	if ( CAssetSubsystem* Assets = GetAppSubsystem<CAssetSubsystem>() )
	{
		FPixelImage Image( 64, 64, FColor32( 30, 80, 160 ) ); // GPUへ送る市松模様。
		for ( int32 Y = 0; Y < 64; ++Y )
		{
			for ( int32 X = 0; X < 64; ++X )
			{
				if ( ( X / 8 + Y / 8 ) % 2 == 0 )
				{
					Image.Set( X, Y, FColor32( 240, 220, 100 ) );
				}
			}
		}
		Texture = Assets->CreateDynamicTexture( "Canvas2D.Checker", Image );
	}
}

void CCanvas2DGallery::Draw( FDrawContext& Draw )
{
	Draw.Text( 40, 90, "Common canvas: shapes / clip / alpha / texture", FColor32( 255, 255, 255 ) );
	Draw.Box( 40, 140, 220, 260, FColor32( 30, 120, 230 ) );
	Draw.CircleAA( 310, 200, 60, FColor32( 240, 180, 40 ) );
	Draw.LineAA( 40, 300, 370, 340, FColor32( 255, 255, 255 ), 4 );
	Draw.PushClip( 440, 140, 600, 260 );
	Draw.Box( 400, 100, 650, 300, FColor32( 50, 200, 120 ) );
	Draw.PopClip();
	Draw.Blend( EDrawBlend::Alpha, 128 );
	Draw.Box( 160, 200, 400, 320, FColor32( 240, 50, 90 ) );
	Draw.Blend( EDrawBlend::Opaque );
	Draw.ImageStretch( 680, 140, 936, 396, Texture );
	Draw.Text( 440, 280, "Clipped rectangle", FColor32( 255, 255, 255 ) );
}
