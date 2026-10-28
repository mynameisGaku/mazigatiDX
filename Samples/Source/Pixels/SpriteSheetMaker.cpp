#include "SpriteSheetMaker.h"

#include "Library/Math/Math.h"

FPixelImage NSpriteSheetMaker::Make()
{
	FPixelImage Sheet( Columns * CellSize, Rows * CellSize, FColor32( 0, 0, 0, 255 ) );
	const int32 NumCells = Columns * Rows;
	for ( int32 Index = 0; Index < NumCells; ++Index )
	{
		const int32 Left = ( Index % Columns ) * CellSize;
		const int32 Top = ( Index / Columns ) * CellSize;
		// 背景: コマごとに色相を変える
		const float32 Hue = static_cast<float32>( Index ) / static_cast<float32>( NumCells ) * NMath::PI * 2.0f;
		const FColor32 Background( static_cast<uint8>( 100 + 80 * NMath::Sin( Hue ) ), static_cast<uint8>( 100 + 80 * NMath::Sin( Hue + 2.1f ) ), static_cast<uint8>( 100 + 80 * NMath::Sin( Hue + 4.2f ) ) );
		Sheet.FillRect( Left, Top, CellSize, CellSize, Background );
		// 円: 半径がコマごとに大きくなる。縁は、半透明の白（重ねると、背景が透けて見える）
		const float32 Radius = 4.0f + 1.8f * static_cast<float32>( Index );
		const float32 Center = static_cast<float32>( CellSize ) * 0.5f - 0.5f;
		FPixelImage Disc( CellSize, CellSize, FColor32( 0, 0, 0, 0 ) );
		for ( int32 Y = 0; Y < CellSize; ++Y )
		{
			for ( int32 X = 0; X < CellSize; ++X )
			{
				const float32 DeltaX = static_cast<float32>( X ) - Center;
				const float32 DeltaY = static_cast<float32>( Y ) - Center;
				const float32 Distance = NMath::Sqrt( DeltaX * DeltaX + DeltaY * DeltaY );
				// 内側は不透明、縁の 2 ピクセルは、外へ向かって薄くなる
				const float32 Coverage = NMath::Clamp( ( Radius + 1.0f - Distance ) / 2.0f, 0.0f, 1.0f );
				Disc.Set( X, Y, FColor32( 255, 255, 255, static_cast<uint8>( Coverage * 255.0f ) ) );
			}
		}
		Sheet.Blit( Disc, Left, Top, EPixelBlend::Alpha );
	}
	return Sheet;
}
