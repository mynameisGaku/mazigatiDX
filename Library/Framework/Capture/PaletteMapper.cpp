// SPDX-License-Identifier: MIT
#include "Library/Framework/Capture/PaletteMapper.h"

FPaletteMapper::FPaletteMapper( const TArray<FColor32>& InPalette )
	: Palette( InPalette )
{
}

uint8 FPaletteMapper::Map( const FColor32& Color )
{
	const uint32 Rgb = ( static_cast<uint32>( Color.r ) << 16 ) | ( static_cast<uint32>( Color.g ) << 8 ) | Color.b;
	if ( const uint8* Found = Cache.Find( Rgb ) )
	{
		return *Found;
	}
	int32 Best = 0;
	int32 BestDistance = 0x7FFFFFFF;
	for ( int32 Index = 0; Index < Palette.Num() && Index < 256; ++Index )
	{
		const int32 R = static_cast<int32>( Color.r ) - Palette[Index].r;
		const int32 G = static_cast<int32>( Color.g ) - Palette[Index].g;
		const int32 B = static_cast<int32>( Color.b ) - Palette[Index].b;
		const int32 Distance = R * R + G * G + B * B;
		if ( Distance < BestDistance )
		{
			BestDistance = Distance;
			Best = Index;
		}
	}
	Cache.Add( Rgb, static_cast<uint8>( Best ) );
	return static_cast<uint8>( Best );
}
