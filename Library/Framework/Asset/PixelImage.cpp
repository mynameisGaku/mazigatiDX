#include "PixelImage.h"

#include "Library/Core/Memory/Memory.h"
#include "Library/Core/Types/Numeric.h"

namespace
{
	// 大きさが、作れる範囲か
	bool IsValidSize( int32 Width, int32 Height )
	{
		return Width >= 1 && Width <= FPixelImage::MaxSize && Height >= 1 && Height <= FPixelImage::MaxSize && static_cast<int64>( Width ) * Height <= FPixelImage::MaxPixels;
	}

	// 0..255 に収めて、四捨五入した値
	uint8 ToByte( float64 Value )
	{
		const float64 Clamped = Value < 0.0 ? 0.0 : ( Value > 255.0 ? 255.0 : Value );
		return static_cast<uint8>( Clamped + 0.5 );
	}
}

FPixelImage::FPixelImage( int32 InWidth, int32 InHeight, const FColor32& FillColor )
{
	if ( !IsValidSize( InWidth, InHeight ) )
	{
		return;
	}
	Width = InWidth;
	Height = InHeight;
	Pixels.SetNum( Width * Height );
	Fill( FillColor );
}

FPixelImage FPixelImage::FromPixels( int32 InWidth, int32 InHeight, const FColor32* Source )
{
	FPixelImage Result;
	if ( Source == nullptr || !IsValidSize( InWidth, InHeight ) )
	{
		return Result;
	}
	Result.Width = InWidth;
	Result.Height = InHeight;
	Result.Pixels.SetNum( InWidth * InHeight );
	FMemory::Memcpy( Result.Pixels.GetData(), Source, static_cast<usize>( InWidth ) * static_cast<usize>( InHeight ) * sizeof( FColor32 ) );
	return Result;
}

FColor32 FPixelImage::Get( int32 X, int32 Y ) const
{
	if ( !IsValid() || !IsInside( X, Y ) )
	{
		return FColor32( 0, 0, 0, 0 );
	}
	return Pixels[Y * Width + X];
}

bool FPixelImage::Set( int32 X, int32 Y, const FColor32& Color )
{
	if ( !IsValid() || !IsInside( X, Y ) )
	{
		return false;
	}
	Pixels[Y * Width + X] = Color;
	return true;
}

void FPixelImage::Fill( const FColor32& Color )
{
	for ( FColor32& Pixel : Pixels )
	{
		Pixel = Color;
	}
}

void FPixelImage::FillRect( int32 X, int32 Y, int32 W, int32 H, const FColor32& Color )
{
	if ( !IsValid() || W <= 0 || H <= 0 )
	{
		return;
	}
	const int32 Left = NMath::Max( X, 0 );
	const int32 Top = NMath::Max( Y, 0 );
	// 右と下の端（含まない）。足し算のあふれを避けるため、64 ビットで
	const int32 Right = static_cast<int32>( NMath::Min<int64>( static_cast<int64>( X ) + W, Width ) );
	const int32 Bottom = static_cast<int32>( NMath::Min<int64>( static_cast<int64>( Y ) + H, Height ) );
	for ( int32 Row = Top; Row < Bottom; ++Row )
	{
		for ( int32 Column = Left; Column < Right; ++Column )
		{
			Pixels[Row * Width + Column] = Color;
		}
	}
}

FColor32 FPixelImage::Combine( const FColor32& Dest, const FColor32& Source, EPixelBlend Blend )
{
	if ( Blend == EPixelBlend::Copy || Source.a == 255 )
	{
		return Source;
	}
	if ( Source.a == 0 )
	{
		return Dest;
	}
	// 乗算されていないアルファの over: 結果の透明度 = Sa + Da ( 1 - Sa )、色 = ( Sc Sa + Dc Da ( 1 - Sa ) ) / 結果の透明度
	const float64 SourceAlpha = static_cast<float64>( Source.a ) / 255.0;
	const float64 DestAlpha = static_cast<float64>( Dest.a ) / 255.0;
	const float64 OutAlpha = SourceAlpha + DestAlpha * ( 1.0 - SourceAlpha );
	if ( OutAlpha <= 0.0 )
	{
		return FColor32( 0, 0, 0, 0 );
	}
	const auto Mix = [&]( uint8 SourceChannel, uint8 DestChannel )
	{
		return ToByte( ( static_cast<float64>( SourceChannel ) * SourceAlpha + static_cast<float64>( DestChannel ) * DestAlpha * ( 1.0 - SourceAlpha ) ) / OutAlpha );
	};
	return FColor32( Mix( Source.r, Dest.r ), Mix( Source.g, Dest.g ), Mix( Source.b, Dest.b ), ToByte( OutAlpha * 255.0 ) );
}

void FPixelImage::Blit( const FPixelImage& Source, int32 SrcX, int32 SrcY, int32 SrcW, int32 SrcH, int32 DestX, int32 DestY, EPixelBlend Blend )
{
	if ( !IsValid() || !Source.IsValid() || SrcW <= 0 || SrcH <= 0 )
	{
		return;
	}
	// 自分自身から写すときも、重ねる前の絵が写るよう、先に写す範囲を取り出す
	if ( &Source == this )
	{
		const FPixelImage Copy = Crop( SrcX, SrcY, SrcW, SrcH );
		Blit( Copy, 0, 0, SrcW, SrcH, DestX, DestY, Blend );
		return;
	}
	// 元の矩形を、元の画像に収める（切った分、行き先もずらす）
	int32 X0 = SrcX;
	int32 Y0 = SrcY;
	int64 X1 = static_cast<int64>( SrcX ) + SrcW;
	int64 Y1 = static_cast<int64>( SrcY ) + SrcH;
	// 行き先のずれは、足し引きであふれないよう、64 ビットで持つ
	int64 OffsetX = DestX;
	int64 OffsetY = DestY;
	if ( X0 < 0 )
	{
		OffsetX -= X0;
		X0 = 0;
	}
	if ( Y0 < 0 )
	{
		OffsetY -= Y0;
		Y0 = 0;
	}
	X1 = NMath::Min<int64>( X1, Source.Width );
	Y1 = NMath::Min<int64>( Y1, Source.Height );
	if ( X1 <= X0 || Y1 <= Y0 )
	{
		return;
	}
	for ( int32 Row = Y0; Row < Y1; ++Row )
	{
		const int64 DestRow = OffsetY + ( Row - Y0 );
		if ( DestRow < 0 || DestRow >= Height )
		{
			continue;
		}
		for ( int32 Column = X0; Column < X1; ++Column )
		{
			const int64 DestColumn = OffsetX + ( Column - X0 );
			if ( DestColumn < 0 || DestColumn >= Width )
			{
				continue;
			}
			FColor32& Target = Pixels[static_cast<int32>( DestRow ) * Width + static_cast<int32>( DestColumn )];
			Target = Combine( Target, Source.Pixels[Row * Source.Width + Column], Blend );
		}
	}
}

void FPixelImage::Blit( const FPixelImage& Source, int32 DestX, int32 DestY, EPixelBlend Blend )
{
	Blit( Source, 0, 0, Source.Width, Source.Height, DestX, DestY, Blend );
}

FPixelImage FPixelImage::Crop( int32 X, int32 Y, int32 W, int32 H ) const
{
	FPixelImage Result( W, H );
	if ( !Result.IsValid() || !IsValid() )
	{
		return Result;
	}
	for ( int32 Row = 0; Row < H; ++Row )
	{
		for ( int32 Column = 0; Column < W; ++Column )
		{
			const int64 SourceX = static_cast<int64>( X ) + Column;
			const int64 SourceY = static_cast<int64>( Y ) + Row;
			if ( SourceX >= 0 && SourceY >= 0 && SourceX < Width && SourceY < Height )
			{
				Result.Pixels[Row * W + Column] = Pixels[static_cast<int32>( SourceY ) * Width + static_cast<int32>( SourceX )];
			}
		}
	}
	return Result;
}

void FPixelImage::FlipHorizontal()
{
	for ( int32 Row = 0; Row < Height; ++Row )
	{
		for ( int32 Column = 0; Column < Width / 2; ++Column )
		{
			const FColor32 Left = Pixels[Row * Width + Column];
			Pixels[Row * Width + Column] = Pixels[Row * Width + ( Width - 1 - Column )];
			Pixels[Row * Width + ( Width - 1 - Column )] = Left;
		}
	}
}

void FPixelImage::FlipVertical()
{
	for ( int32 Row = 0; Row < Height / 2; ++Row )
	{
		for ( int32 Column = 0; Column < Width; ++Column )
		{
			const FColor32 Top = Pixels[Row * Width + Column];
			Pixels[Row * Width + Column] = Pixels[( Height - 1 - Row ) * Width + Column];
			Pixels[( Height - 1 - Row ) * Width + Column] = Top;
		}
	}
}

bool FPixelImage::operator==( const FPixelImage& Other ) const
{
	if ( Width != Other.Width || Height != Other.Height || Pixels.Num() != Other.Pixels.Num() )
	{
		return false;
	}
	for ( int32 Index = 0; Index < Pixels.Num(); ++Index )
	{
		if ( !( Pixels[Index] == Other.Pixels[Index] ) )
		{
			return false;
		}
	}
	return true;
}
