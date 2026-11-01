// SPDX-License-Identifier: MIT
#include "Library/Framework/PixelEffect/PixelEffectFrame.h"

#include "Library/Math/MathUtils.h"

namespace
{
	uint8 ClampByteFloat( float Value )
	{
		const int32 Rounded = static_cast<int32>( Value );
		if ( Rounded <= 0 )
		{
			return 0;
		}
		if ( Rounded >= 255 )
		{
			return 255;
		}
		return static_cast<uint8>( Rounded );
	}

	uint8 ClampByteInt( int32 Value )
	{
		if ( Value <= 0 )
		{
			return 0;
		}
		if ( Value >= 255 )
		{
			return 255;
		}
		return static_cast<uint8>( Value );
	}

	FColor32 CompositeSinglePixel( const FColor32& Color, const FColor32& Light, bool bHasBackground, const FColor32& Background, float LightStrength )
	{
		float R = 0.0f;
		float G = 0.0f;
		float B = 0.0f;
		float A = 0.0f;

		if ( Color.a == 255 )
		{
			R = static_cast<float>( Color.r );
			G = static_cast<float>( Color.g );
			B = static_cast<float>( Color.b );
			A = 255.0f;
		}
		else if ( !bHasBackground )
		{
			R = static_cast<float>( Color.r );
			G = static_cast<float>( Color.g );
			B = static_cast<float>( Color.b );
			A = static_cast<float>( Color.a );
		}
		else
		{
			const float Factor = static_cast<float>( Color.a ) / 255.0f;
			const float InvFactor = 1.0f - Factor;
			R = static_cast<float>( Color.r ) * Factor + static_cast<float>( Background.r ) * InvFactor;
			G = static_cast<float>( Color.g ) * Factor + static_cast<float>( Background.g ) * InvFactor;
			B = static_cast<float>( Color.b ) * Factor + static_cast<float>( Background.b ) * InvFactor;
			A = 255.0f;
		}

		const bool bHasLight = ( Light.r != 0 || Light.g != 0 || Light.b != 0 ) && LightStrength > 0.0f;
		if ( bHasLight )
		{
			const float LightR = static_cast<float>( Light.r ) * LightStrength;
			const float LightG = static_cast<float>( Light.g ) * LightStrength;
			const float LightB = static_cast<float>( Light.b ) * LightStrength;
			if ( A < 255.0f )
			{
				const float PremulA = A / 255.0f;
				const float PremulR = R * PremulA + LightR;
				const float PremulG = G * PremulA + LightG;
				const float PremulB = B * PremulA + LightB;
				const float MaxLight = NMath::Max( LightR, NMath::Max( LightG, LightB ) ) / 255.0f;
				const float NewAlpha = NMath::Min( 1.0f, NMath::Max( PremulA, MaxLight ) );
				if ( NewAlpha > 0.0f )
				{
					R = NMath::Min( 255.0f, PremulR / NewAlpha );
					G = NMath::Min( 255.0f, PremulG / NewAlpha );
					B = NMath::Min( 255.0f, PremulB / NewAlpha );
					A = NewAlpha * 255.0f;
				}
			}
			else
			{
				R = NMath::Min( 255.0f, R + LightR );
				G = NMath::Min( 255.0f, G + LightG );
				B = NMath::Min( 255.0f, B + LightB );
			}
		}

		return FColor32( ClampByteFloat( R ), ClampByteFloat( G ), ClampByteFloat( B ), ClampByteFloat( A ) );
	}
}

FPixelEffectFrame::FPixelEffectFrame( int32 InWidth, int32 InHeight )
{
	Initialize( InWidth, InHeight );
}

void FPixelEffectFrame::Initialize( int32 InWidth, int32 InHeight )
{
	if ( InWidth <= 0 || InHeight <= 0 || InWidth > FPixelImage::MaxSize || InHeight > FPixelImage::MaxSize || static_cast<int64>( InWidth ) * static_cast<int64>( InHeight ) > FPixelImage::MaxPixels )
	{
		Width = 0;
		Height = 0;
		ColorBuffer.Reset();
		LightBuffer.Reset();
		return;
	}
	Width = InWidth;
	Height = InHeight;
	const int32 Total = Width * Height;
	ColorBuffer.Reset();
	ColorBuffer.SetNum( Total, FColor32( 0, 0, 0, 0 ) );
	LightBuffer.Reset();
	LightBuffer.SetNum( Total, FColor32( 0, 0, 0, 0 ) );
}

void FPixelEffectFrame::Clear()
{
	const int32 Total = Width * Height;
	for ( int32 Index = 0; Index < Total; ++Index )
	{
		ColorBuffer[Index] = FColor32( 0, 0, 0, 0 );
		LightBuffer[Index] = FColor32( 0, 0, 0, 0 );
	}
}

void FPixelEffectFrame::SetPixel( int32 X, int32 Y, const FColor32& Color )
{
	if ( Color.a == 0 )
	{
		return;
	}
	const int32 BufferX = X;
	const int32 BufferY = Y;
	if ( BufferX < 0 || BufferY < 0 || BufferX >= Width || BufferY >= Height )
	{
		return;
	}
	ColorBuffer[BufferY * Width + BufferX] = FColor32( Color.r, Color.g, Color.b, 255 );
}

void FPixelEffectFrame::BlendPixel( int32 X, int32 Y, const FColor32& Color, float32 Alpha )
{
	if ( Color.a == 0 )
	{
		return;
	}
	const float EffectiveAlpha = NMath::Clamp( Alpha, 0.0f, 1.0f );
	if ( EffectiveAlpha <= 0.004f )
	{
		return;
	}
	const int32 BufferX = X;
	const int32 BufferY = Y;
	if ( BufferX < 0 || BufferY < 0 || BufferX >= Width || BufferY >= Height )
	{
		return;
	}
	FColor32& Dest = ColorBuffer[BufferY * Width + BufferX];
	const float DestAlpha = static_cast<float>( Dest.a ) / 255.0f;
	const float OutAlpha = EffectiveAlpha + DestAlpha * ( 1.0f - EffectiveAlpha );
	if ( OutAlpha <= 0.0f )
	{
		return;
	}
	const float Weight = EffectiveAlpha / OutAlpha;
	const float InvWeight = 1.0f - Weight;
	const int32 R = static_cast<int32>( static_cast<float>( Color.r ) * Weight + static_cast<float>( Dest.r ) * InvWeight );
	const int32 G = static_cast<int32>( static_cast<float>( Color.g ) * Weight + static_cast<float>( Dest.g ) * InvWeight );
	const int32 B = static_cast<int32>( static_cast<float>( Color.b ) * Weight + static_cast<float>( Dest.b ) * InvWeight );
	const int32 A = static_cast<int32>( OutAlpha * 255.0f );
	Dest = FColor32( ClampByteInt( R ), ClampByteInt( G ), ClampByteInt( B ), ClampByteInt( A ) );
}

void FPixelEffectFrame::AddLight( int32 X, int32 Y, const FColor32& Color, float32 Strength )
{
	if ( Color.a == 0 || Strength <= 0.004f )
	{
		return;
	}
	const int32 BufferX = X;
	const int32 BufferY = Y;
	if ( BufferX < 0 || BufferY < 0 || BufferX >= Width || BufferY >= Height )
	{
		return;
	}
	FColor32& Dest = LightBuffer[BufferY * Width + BufferX];
	const int32 R = static_cast<int32>( static_cast<float>( Dest.r ) + static_cast<float>( Color.r ) * Strength );
	const int32 G = static_cast<int32>( static_cast<float>( Dest.g ) + static_cast<float>( Color.g ) * Strength );
	const int32 B = static_cast<int32>( static_cast<float>( Dest.b ) + static_cast<float>( Color.b ) * Strength );
	Dest = FColor32( ClampByteInt( R ), ClampByteInt( G ), ClampByteInt( B ), 255 );
}

FColor32 FPixelEffectFrame::GetPixel( int32 X, int32 Y ) const
{
	const int32 BufferX = X;
	const int32 BufferY = Y;
	if ( BufferX < 0 || BufferY < 0 || BufferX >= Width || BufferY >= Height )
	{
		return FColor32( 0, 0, 0, 0 );
	}
	return ColorBuffer[BufferY * Width + BufferX];
}

FPixelImage FPixelEffectFrame::CompositeToImage( const FColor32* BackgroundPixels, float LightStrength ) const
{
	if ( !IsValid() )
	{
		return FPixelImage();
	}
	const int32 Total = Width * Height;
	TArray<FColor32> OutPixels;
	OutPixels.SetNum( Total );
	const bool bHasBackground = ( BackgroundPixels != nullptr );
	for ( int32 Index = 0; Index < Total; ++Index )
	{
		const FColor32 BgColor = bHasBackground ? BackgroundPixels[Index] : FColor32( 0, 0, 0, 0 );
		OutPixels[Index] = CompositeSinglePixel( ColorBuffer[Index], LightBuffer[Index], bHasBackground, BgColor, LightStrength );
	}
	return FPixelImage::FromPixels( Width, Height, OutPixels.GetData() );
}

FPixelImage FPixelEffectFrame::CompositeOverColor( const FColor32& BackgroundColor, float LightStrength ) const
{
	if ( !IsValid() )
	{
		return FPixelImage();
	}
	const int32 Total = Width * Height;
	TArray<FColor32> OutPixels;
	OutPixels.SetNum( Total );
	for ( int32 Index = 0; Index < Total; ++Index )
	{
		OutPixels[Index] = CompositeSinglePixel( ColorBuffer[Index], LightBuffer[Index], true, BackgroundColor, LightStrength );
	}
	return FPixelImage::FromPixels( Width, Height, OutPixels.GetData() );
}
