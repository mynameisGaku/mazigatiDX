// SPDX-License-Identifier: MIT
#include "Library/Framework/Capture/CaptureImage.h"

#include "Library/Math/MathUtils.h"

TArray<FColor32> NCaptureImage::Extract( TArrayView<const FColor32> Pixels, int32 Width, int32 Height, const FCaptureRegion& Region, int32 Downscale, int32& OutWidth, int32& OutHeight )
{
	OutWidth = 0;
	OutHeight = 0;
	TArray<FColor32> Out;
	if ( Width <= 0 || Height <= 0 || Pixels.Num() < Width * Height )
	{
		return Out;
	}
	// 画面の中に収めた矩形
	const int32 Left = Region.IsWholeScreen() ? 0 : NMath::Clamp( Region.X, 0, Width );
	const int32 Top = Region.IsWholeScreen() ? 0 : NMath::Clamp( Region.Y, 0, Height );
	const int32 Right = Region.IsWholeScreen() ? Width : NMath::Clamp( Region.X + Region.Width, 0, Width );
	const int32 Bottom = Region.IsWholeScreen() ? Height : NMath::Clamp( Region.Y + Region.Height, 0, Height );
	const int32 Scale = NMath::Max( 1, Downscale );
	const int32 ResultWidth = ( Right - Left ) / Scale;
	const int32 ResultHeight = ( Bottom - Top ) / Scale;
	if ( ResultWidth <= 0 || ResultHeight <= 0 )
	{
		return Out;
	}
	Out.SetNum( ResultWidth * ResultHeight );
	const int32 Samples = Scale * Scale;
	for ( int32 Y = 0; Y < ResultHeight; ++Y )
	{
		for ( int32 X = 0; X < ResultWidth; ++X )
		{
			int32 Sum[4] = { 0, 0, 0, 0 };
			for ( int32 SubY = 0; SubY < Scale; ++SubY )
			{
				for ( int32 SubX = 0; SubX < Scale; ++SubX )
				{
					const FColor32& Pixel = Pixels[( Top + Y * Scale + SubY ) * Width + Left + X * Scale + SubX];
					Sum[0] += Pixel.r;
					Sum[1] += Pixel.g;
					Sum[2] += Pixel.b;
					Sum[3] += Pixel.a;
				}
			}
			Out[Y * ResultWidth + X] = FColor32( static_cast<uint8>( ( Sum[0] + Samples / 2 ) / Samples ), static_cast<uint8>( ( Sum[1] + Samples / 2 ) / Samples ), static_cast<uint8>( ( Sum[2] + Samples / 2 ) / Samples ), static_cast<uint8>( ( Sum[3] + Samples / 2 ) / Samples ) );
		}
	}
	OutWidth = ResultWidth;
	OutHeight = ResultHeight;
	return Out;
}
