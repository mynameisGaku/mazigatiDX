#include "FilterImages.h"

#include "Library/Math/Math.h"

FPixelImage NFilterImages::MakeSource()
{
	FPixelImage Image( SourceSize, SourceSize, FColor32( 0, 0, 0, 255 ) );
	const int32 Half = SourceSize / 2;
	Image.FillRect( 0, 0, Half, Half, FColor32( 255, 0, 0, 255 ) );
	Image.FillRect( Half, 0, Half, Half, FColor32( 0, 255, 0, 255 ) );
	Image.FillRect( 0, Half, Half, Half, FColor32( 0, 0, 255, 255 ) );
	// 右下: 左から右へ、黒から白へ
	for ( int32 Y = Half; Y < SourceSize; ++Y )
	{
		for ( int32 X = Half; X < SourceSize; ++X )
		{
			const uint8 Level = static_cast<uint8>( ( X - Half ) * 255 / ( Half - 1 ) );
			Image.Set( X, Y, FColor32( Level, Level, Level, 255 ) );
		}
	}
	return Image;
}

FPixelImage NFilterImages::MakeOverlay()
{
	FPixelImage Image( SourceSize, SourceSize, FColor32( 0, 0, 0, 255 ) );
	for ( int32 Y = 0; Y < SourceSize; ++Y )
	{
		const float32 Ratio = static_cast<float32>( Y ) / static_cast<float32>( SourceSize - 1 );
		const FColor32 Color( static_cast<uint8>( 255.0f * ( 1.0f - Ratio ) ), static_cast<uint8>( 160.0f * ( 1.0f - Ratio ) + 80.0f * Ratio ), static_cast<uint8>( 255.0f * Ratio ), 255 );
		Image.FillRect( 0, Y, SourceSize, 1, Color );
	}
	return Image;
}

FPixelImage NFilterImages::MakeHeatMap()
{
	FPixelImage Image( 5, 1 );
	Image.Set( 0, 0, FColor32( 0, 0, 0, 255 ) );
	Image.Set( 1, 0, FColor32( 120, 0, 160, 255 ) );
	Image.Set( 2, 0, FColor32( 255, 0, 0, 255 ) );
	Image.Set( 3, 0, FColor32( 255, 220, 0, 255 ) );
	Image.Set( 4, 0, FColor32( 255, 255, 255, 255 ) );
	return Image;
}

FPixelImage NFilterImages::MakeGlow( int32 Size )
{
	FPixelImage Image( Size, Size, FColor32( 255, 255, 255, 0 ) );
	const float32 Center = static_cast<float32>( Size - 1 ) * 0.5f;
	for ( int32 Y = 0; Y < Size; ++Y )
	{
		for ( int32 X = 0; X < Size; ++X )
		{
			const float32 DeltaX = static_cast<float32>( X ) - Center;
			const float32 DeltaY = static_cast<float32>( Y ) - Center;
			const float32 Distance = NMath::Sqrt( DeltaX * DeltaX + DeltaY * DeltaY ) / Center;
			// 縁（距離 1）で 0、中心から距離 0.5 までは 1、そのあいだは、なめらかに（smoothstep）
			const float32 Ratio = NMath::Clamp( ( 1.0f - Distance ) / 0.5f, 0.0f, 1.0f );
			const float32 Smooth = Ratio * Ratio * ( 3.0f - 2.0f * Ratio );
			Image.Set( X, Y, FColor32( 255, 255, 255, static_cast<uint8>( Smooth * 255.0f ) ) );
		}
	}
	return Image;
}
