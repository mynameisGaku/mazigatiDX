// SPDX-License-Identifier: MIT
#include "Library/Framework/Capture/AlphaMatte.h"

#include "Library/Math/MathUtils.h"

bool NAlphaMatte::FromBlackAndWhite( TArrayView<const FColor32> OnBlack, TArrayView<const FColor32> OnWhite, TArray<FColor32>& Out )
{
	if ( OnBlack.Num() != OnWhite.Num() )
	{
		return false;
	}
	Out.SetNum( OnBlack.Num() );
	for ( int32 Index = 0; Index < OnBlack.Num(); ++Index )
	{
		const FColor32& Black = OnBlack[Index];
		const FColor32& White = OnWhite[Index];
		// 白と黒の差（3 色の合計。0 = 不透明、765 = 何も描かれていない）
		const int32 Difference = NMath::Clamp( static_cast<int32>( White.r ) - Black.r, 0, 255 ) + NMath::Clamp( static_cast<int32>( White.g ) - Black.g, 0, 255 ) + NMath::Clamp( static_cast<int32>( White.b ) - Black.b, 0, 255 );
		const int32 Alpha = NMath::Clamp( 255 - ( Difference + 1 ) / 3, 0, 255 );
		if ( Alpha == 0 )
		{
			Out[Index] = FColor32( 0, 0, 0, 0 );
			continue;
		}
		// 黒の背景の色は、a を掛けたあとの色。a で割って、元の色へ戻す
		const auto Unpremultiply = [ Alpha ]( uint8 Value )
		{
			return static_cast<uint8>( NMath::Min( 255, ( static_cast<int32>( Value ) * 255 + Alpha / 2 ) / Alpha ) );
		};
		Out[Index] = FColor32( Unpremultiply( Black.r ), Unpremultiply( Black.g ), Unpremultiply( Black.b ), static_cast<uint8>( Alpha ) );
	}
	return true;
}

void NAlphaMatte::Flatten( TArray<FColor32>& InOut, const FColor32& Background )
{
	for ( FColor32& Pixel : InOut )
	{
		const int32 Alpha = Pixel.a;
		const auto Mix = [ Alpha ]( uint8 Front, uint8 Back )
		{
			return static_cast<uint8>( ( static_cast<int32>( Front ) * Alpha + static_cast<int32>( Back ) * ( 255 - Alpha ) + 127 ) / 255 );
		};
		Pixel = FColor32( Mix( Pixel.r, Background.r ), Mix( Pixel.g, Background.g ), Mix( Pixel.b, Background.b ), 255 );
	}
}
