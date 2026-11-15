// SPDX-License-Identifier: MIT
#include "TgaDecoder.h"
#include "Library/Core/Types/Numeric.h"

namespace
{
	constexpr usize HeaderSize = 18;

	struct FTgaHeader
	{
		uint8 IdLength = 0;
		uint8 ColorMapType = 0;
		uint8 ImageType = 0;
		int32 ColorMapStart = 0;
		int32 ColorMapLength = 0;
		uint8 ColorMapDepth = 0;
		int32 Width = 0;
		int32 Height = 0;
		uint8 PixelDepth = 0;
		uint8 Descriptor = 0;
	};

	int32 Read16( const uint8* Bytes )
	{
		return static_cast<int32>( Bytes[0] ) | ( static_cast<int32>( Bytes[1] ) << 8 );
	}

	bool ReadHeader( const uint8* Bytes, usize Size, FTgaHeader& Out )
	{
		if ( !Bytes || Size < HeaderSize )
		{
			return false;
		}
		Out.IdLength = Bytes[0];
		Out.ColorMapType = Bytes[1];
		Out.ImageType = Bytes[2];
		Out.ColorMapStart = Read16( Bytes + 3 );
		Out.ColorMapLength = Read16( Bytes + 5 );
		Out.ColorMapDepth = Bytes[7];
		Out.Width = Read16( Bytes + 12 );
		Out.Height = Read16( Bytes + 14 );
		Out.PixelDepth = Bytes[16];
		Out.Descriptor = Bytes[17];
		return true;
	}

	// 1 つの画素（BGR / BGRA / 5-5-5-1 / 明るさ）をバイト列から RGBA にする。Depth は、ビット数
	FColor32 ToColor( const uint8* Pixel, int32 Depth, bool bGray )
	{
		if ( Depth == 8 )
		{
			return bGray ? FColor32( Pixel[0], Pixel[0], Pixel[0], 255 ) : FColor32( 0, 0, 0, 255 );
		}
		if ( Depth == 16 )
		{
			if ( bGray )
			{
				return FColor32( Pixel[0], Pixel[0], Pixel[0], Pixel[1] ); // 明るさとアルファ
			}
			const int32 Value = Read16( Pixel );
			const auto Expand = [&]( int32 Shift ) { const int32 Five = ( Value >> Shift ) & 31; return static_cast<uint8>( ( Five << 3 ) | ( Five >> 2 ) ); };
			return FColor32( Expand( 10 ), Expand( 5 ), Expand( 0 ), ( Value & 0x8000 ) != 0 ? 255 : 0 );
		}
		if ( Depth == 24 )
		{
			return FColor32( Pixel[2], Pixel[1], Pixel[0], 255 );
		}
		return FColor32( Pixel[2], Pixel[1], Pixel[0], Pixel[3] );
	}
}

bool NTgaDecoder::LooksLikeTga( const void* Data, usize Size )
{
	FTgaHeader Header;
	if ( !ReadHeader( static_cast<const uint8*>( Data ), Size, Header ) )
	{
		return false;
	}
	const bool bTypeKnown = Header.ImageType == 1 || Header.ImageType == 2 || Header.ImageType == 3 || Header.ImageType == 9 || Header.ImageType == 10 || Header.ImageType == 11;
	const bool bDepthKnown = Header.PixelDepth == 8 || Header.PixelDepth == 16 || Header.PixelDepth == 24 || Header.PixelDepth == 32;
	return bTypeKnown && bDepthKnown && Header.ColorMapType <= 1 && Header.Width > 0 && Header.Height > 0;
}

bool NTgaDecoder::Decode( const void* Data, usize Size, int32& OutWidth, int32& OutHeight, TArray<FColor32>& OutPixels )
{
	const uint8* Bytes = static_cast<const uint8*>( Data );
	FTgaHeader Header;
	if ( !LooksLikeTga( Data, Size ) || !ReadHeader( Bytes, Size, Header ) )
	{
		return false;
	}
	const int32 Width = Header.Width, Height = Header.Height;
	if ( Width > 16384 || Height > 16384 || static_cast<int64>( Width ) * Height > 64 * 1024 * 1024 )
	{
		return false;
	}
	const bool bColorMapped = Header.ImageType == 1 || Header.ImageType == 9;
	const bool bGray = Header.ImageType == 3 || Header.ImageType == 11;
	const bool bRle = Header.ImageType >= 9;
	if ( bColorMapped && ( Header.ColorMapType != 1 || Header.PixelDepth != 8 || ( Header.ColorMapDepth != 24 && Header.ColorMapDepth != 32 ) ) )
	{
		return false;
	}
	if ( bGray && Header.PixelDepth != 8 && Header.PixelDepth != 16 )
	{
		return false;
	}
	if ( !bColorMapped && !bGray && Header.PixelDepth == 8 )
	{
		return false;
	}
	usize Offset = HeaderSize + Header.IdLength;
	// パレット
	TArray<FColor32> Palette;
	if ( Header.ColorMapType == 1 )
	{
		const usize EntryBytes = static_cast<usize>( Header.ColorMapDepth / 8 );
		if ( Header.ColorMapDepth != 24 && Header.ColorMapDepth != 32 )
		{
			return false;
		}
		const usize PaletteBytes = EntryBytes * Header.ColorMapLength;
		if ( Offset + PaletteBytes > Size )
		{
			return false;
		}
		Palette.SetNum( Header.ColorMapStart + Header.ColorMapLength );
		for ( int32 Index = 0; Index < Header.ColorMapLength; ++Index )
		{
			Palette[Header.ColorMapStart + Index] = ToColor( Bytes + Offset + EntryBytes * Index, Header.ColorMapDepth, false );
		}
		Offset += PaletteBytes;
	}
	const int32 PixelBytes = Header.PixelDepth / 8;
	TArray<FColor32> Decoded; // 成功するまで出力から分離する。
	Decoded.SetNum( Width * Height );
	const auto ReadPixel = [&]( const uint8* Pixel ) -> FColor32
	{
		if ( bColorMapped )
		{
			return Pixel[0] < Palette.Num() ? Palette[Pixel[0]] : FColor32( 0, 0, 0, 255 );
		}
		return ToColor( Pixel, Header.PixelDepth, bGray );
	};
	// ファイルの順（原点から）に画素を並べてから、向きを直す
	TArray<FColor32> InOrder;
	InOrder.SetNum( Width * Height );
	const int64 Total = static_cast<int64>( Width ) * Height;
	int64 Count = 0;
	if ( !bRle )
	{
		if ( Offset + static_cast<usize>( Total ) * PixelBytes > Size )
		{
			return false;
		}
		for ( ; Count < Total; ++Count )
		{
			InOrder[static_cast<int32>( Count )] = ReadPixel( Bytes + Offset + static_cast<usize>( Count ) * PixelBytes );
		}
	}
	else
	{
		while ( Count < Total )
		{
			if ( Offset >= Size )
			{
				return false;
			}
			const uint8 Packet = Bytes[Offset++];
			const int32 Run = ( Packet & 0x7F ) + 1;
			if ( Packet & 0x80 )
			{
				if ( Offset + PixelBytes > Size )
				{
					return false;
				}
				const FColor32 Value = ReadPixel( Bytes + Offset );
				Offset += PixelBytes;
				for ( int32 Index = 0; Index < Run && Count < Total; ++Index, ++Count )
				{
					InOrder[static_cast<int32>( Count )] = Value;
				}
			}
			else
			{
				if ( Offset + static_cast<usize>( Run ) * PixelBytes > Size )
				{
					return false;
				}
				for ( int32 Index = 0; Index < Run && Count < Total; ++Index, ++Count )
				{
					InOrder[static_cast<int32>( Count )] = ReadPixel( Bytes + Offset );
					Offset += PixelBytes;
				}
			}
		}
	}
	const bool bTop = ( Header.Descriptor & 0x20 ) != 0;   // 上の行から並んでいる
	const bool bRight = ( Header.Descriptor & 0x10 ) != 0; // 右の列から並んでいる
	for ( int32 Y = 0; Y < Height; ++Y )
	{
		const int32 DestinationY = bTop ? Y : Height - 1 - Y;
		for ( int32 X = 0; X < Width; ++X )
		{
			Decoded[DestinationY * Width + ( bRight ? Width - 1 - X : X )] = InOrder[Y * Width + X];
		}
	}
	OutWidth = Width;
	OutHeight = Height;
	OutPixels = MoveTemp( Decoded );
	return true;
}
