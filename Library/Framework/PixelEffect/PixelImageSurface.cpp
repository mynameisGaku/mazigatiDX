// SPDX-License-Identifier: MIT
#include "Library/Framework/PixelEffect/PixelImageSurface.h"

#include "Library/Math/MathUtils.h"

namespace
{
	uint8 ClampChannelInt( int32 Value )
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
}

FPixelImageSurface::FPixelImageSurface( FPixelImage& InImage )
	: Image( &InImage )
{
}

int32 FPixelImageSurface::GetWidth() const
{
	if ( Image == nullptr )
	{
		return 0;
	}
	return Image->GetWidth();
}

int32 FPixelImageSurface::GetHeight() const
{
	if ( Image == nullptr )
	{
		return 0;
	}
	return Image->GetHeight();
}

void FPixelImageSurface::SetPixel( int32 PixelX, int32 PixelY, const FColor32& Color )
{
	if ( Image == nullptr || Color.a == 0 )
	{
		return;
	}
	Image->Set( PixelX, PixelY, FColor32( Color.r, Color.g, Color.b, 255 ) );
}

void FPixelImageSurface::BlendPixel( int32 PixelX, int32 PixelY, const FColor32& Color, float32 Alpha )
{
	if ( Image == nullptr || Color.a == 0 )
	{
		return;
	}
	const float EffectiveAlpha = NMath::Clamp( Alpha, 0.0f, 1.0f );
	if ( EffectiveAlpha <= 0.004f )
	{
		return;
	}
	const int32 X = PixelX;
	const int32 Y = PixelY;
	if ( X < 0 || Y < 0 || X >= Image->GetWidth() || Y >= Image->GetHeight() )
	{
		return;
	}
	const FColor32 Dest = Image->Get( X, Y );
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
	Image->Set( X, Y, FColor32( ClampChannelInt( R ), ClampChannelInt( G ), ClampChannelInt( B ), ClampChannelInt( A ) ) );
}

void FPixelImageSurface::AddLight( int32 PixelX, int32 PixelY, const FColor32& Color, float32 Strength )
{
	if ( Image == nullptr || Color.a == 0 || Strength <= 0.004f )
	{
		return;
	}
	const int32 X = PixelX;
	const int32 Y = PixelY;
	if ( X < 0 || Y < 0 || X >= Image->GetWidth() || Y >= Image->GetHeight() )
	{
		return;
	}
	const FColor32 Dest = Image->Get( X, Y );
	const int32 R = static_cast<int32>( static_cast<float>( Dest.r ) + static_cast<float>( Color.r ) * Strength );
	const int32 G = static_cast<int32>( static_cast<float>( Dest.g ) + static_cast<float>( Color.g ) * Strength );
	const int32 B = static_cast<int32>( static_cast<float>( Dest.b ) + static_cast<float>( Color.b ) * Strength );
	const int32 LightAlpha = static_cast<int32>( NMath::Max( static_cast<float>( Color.r ), NMath::Max( static_cast<float>( Color.g ), static_cast<float>( Color.b ) ) ) * Strength );
	const int32 A = NMath::Max( static_cast<int32>( Dest.a ), LightAlpha );
	Image->Set( X, Y, FColor32( ClampChannelInt( R ), ClampChannelInt( G ), ClampChannelInt( B ), ClampChannelInt( A ) ) );
}

FColor32 FPixelImageSurface::GetPixel( int32 PixelX, int32 PixelY ) const
{
	if ( Image == nullptr )
	{
		return FColor32( 0, 0, 0, 0 );
	}
	return Image->Get( PixelX, PixelY );
}
