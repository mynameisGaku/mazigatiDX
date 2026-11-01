// SPDX-License-Identifier: MIT
#include "Library/Framework/PixelEffect/PixelBackdrop.h"

#include "Library/Framework/PixelEffect/PixelColor.h"
#include "Library/Framework/PixelEffect/PixelMath.h"
#include "Library/Math/MathUtils.h"

namespace NPixelBackdrop
{
	FPixelImage MakeBackdrop( EPixelBackdrop Kind, int32 Width, int32 Height, int32 GroundY )
	{
		if ( Width <= 0 || Height <= 0 )
		{
			return FPixelImage();
		}
		if ( Kind == EPixelBackdrop::Light )
		{
			return FPixelImage( Width, Height, NPixelColor::FromHex( "#ebe8f0" ) );
		}
		if ( Kind == EPixelBackdrop::Black )
		{
			return FPixelImage( Width, Height, NPixelColor::FromHex( "#000000" ) );
		}

		static const int32 Bayer4x4[16] =
		{
			0, 8, 2, 10,
			12, 4, 14, 6,
			3, 11, 1, 9,
			15, 7, 13, 5,
		};

		const FColor32 SkyColor = NPixelColor::FromHex( "#15131d" );
		const FColor32 SkyTop = NPixelColor::FromHex( "#0d0b14" );
		const FColor32 SkyLow = NPixelColor::FromHex( "#221d33" );
		const FColor32 StarTint = NPixelColor::FromHex( "#cfc8ff" );
		const FColor32 FloorEdge = NPixelColor::FromHex( "#4a4263" );
		const FColor32 FloorMain = NPixelColor::FromHex( "#2b2639" );
		const FColor32 FloorDark = NPixelColor::FromHex( "#211d2d" );
		const FColor32 MortarColor = NPixelColor::FromHex( "#1a1724" );

		const int32 ResolvedGroundY = ( GroundY < 0 ) ? Height : GroundY;
		TArray<FColor32> Pixels;
		Pixels.SetNum( Width * Height );
		const float InvMaxY = 1.0f / static_cast<float>( NMath::Max( 1, Height - 1 ) );

		for ( int32 Y = 0; Y < Height; ++Y )
		{
			for ( int32 X = 0; X < Width; ++X )
			{
				FColor32 Color = SkyColor;
				if ( Kind == EPixelBackdrop::Sky )
				{
					const float NormalizedY = static_cast<float>( Y ) * InvMaxY;
					const float Dither = static_cast<float>( Bayer4x4[( ( Y & 3 ) << 2 ) | ( X & 3 )] ) / 16.0f;
					const int32 Step = static_cast<int32>( NormalizedY * 5.0f + Dither );
					Color = NPixelColor::Mix( SkyTop, SkyLow, NMath::Min( 1.0f, static_cast<float>( Step ) / 4.0f ) );
					const float StarRoll = NPixelMath::RandomUnit( X, Y, 77 );
					if ( StarRoll > 0.994f )
					{
						Color = NPixelColor::Mix( Color, StarTint, 0.35f + ( StarRoll - 0.994f ) * 90.0f );
					}
				}
				if ( Kind == EPixelBackdrop::Ground && Y >= ResolvedGroundY )
				{
					const int32 GroundOffsetY = Y - ResolvedGroundY;
					if ( GroundOffsetY == 0 )
					{
						Color = FloorEdge;
					}
					else
					{
						const int32 BrickRow = ( GroundOffsetY - 1 ) / 5;
						const int32 RowShift = ( ( BrickRow % 2 ) != 0 ) ? 6 : 0;
						const bool bIsMortar = ( ( ( GroundOffsetY - 1 ) % 5 ) == 4 ) || ( ( ( X + RowShift ) % 12 ) == 0 );
						if ( bIsMortar )
						{
							Color = MortarColor;
						}
						else if ( GroundOffsetY < 3 )
						{
							Color = FloorMain;
						}
						else
						{
							Color = FloorDark;
						}
					}
				}
				Pixels[Y * Width + X] = Color;
			}
		}

		return FPixelImage::FromPixels( Width, Height, Pixels.GetData() );
	}
}
