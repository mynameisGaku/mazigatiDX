// SPDX-License-Identifier: MIT
#include "Library/Framework/Capture/Encoding/GifEncoder.h"

#include "Library/Framework/Capture/Encoding/ColorQuantizer.h"
#include "Library/Framework/Capture/Encoding/GifLzw.h"
#include "Library/Framework/Capture/Encoding/PaletteMapper.h"
#include "Library/Math/MathUtils.h"

namespace
{
	constexpr int32 MaxSize = 65535;

	void AppendU16( TArray<uint8>& Out, int32 Value )
	{
		Out.Add( static_cast<uint8>( Value & 0xFF ) );
		Out.Add( static_cast<uint8>( ( Value >> 8 ) & 0xFF ) );
	}

	void AppendText( TArray<uint8>& Out, const char* Text )
	{
		for ( const char* Cursor = Text; *Cursor != '\0'; ++Cursor )
		{
			Out.Add( static_cast<uint8>( *Cursor ) );
		}
	}

	FORCEINLINE bool SameRgb( const FColor32& A, const FColor32& B )
	{
		return A.r == B.r && A.g == B.g && A.b == B.b;
	}

	// 色の表の大きさを表す 3 ビット（表は 2^(Bits + 1) 色）
	int32 TableSizeBits( int32 Colors )
	{
		int32 Bits = 0;
		while ( ( 2 << Bits ) < Colors && Bits < 7 )
		{
			++Bits;
		}
		return Bits;
	}
}

FGifEncoder::FGifEncoder( int32 InWidth, int32 InHeight, bool bLoop )
	: Width( InWidth >= 1 && InWidth <= MaxSize && InHeight >= 1 && InHeight <= MaxSize ? InWidth : 0 )
	, Height( InWidth >= 1 && InWidth <= MaxSize && InHeight >= 1 && InHeight <= MaxSize ? InHeight : 0 )
{
	if ( !IsValid() )
	{
		return;
	}
	// 頭と、画面全体の大きさ（全体の色の表は使わない。1 枚ごとの表を使う）
	AppendText( Bytes, "GIF89a" );
	AppendU16( Bytes, Width );
	AppendU16( Bytes, Height );
	Bytes.Add( 0 );
	Bytes.Add( 0 );
	Bytes.Add( 0 );
	if ( bLoop )
	{
		// 繰り返し（NETSCAPE2.0 の拡張。回数 0 = ずっと）
		Bytes.Add( 0x21 );
		Bytes.Add( 0xFF );
		Bytes.Add( 11 );
		AppendText( Bytes, "NETSCAPE2.0" );
		Bytes.Add( 3 );
		Bytes.Add( 1 );
		AppendU16( Bytes, 0 );
		Bytes.Add( 0 );
	}
}

bool FGifEncoder::AddFrame( TArrayView<const FColor32> Pixels, int32 DelayCentiseconds )
{
	if ( !IsValid() || bFinished || Pixels.Num() != Width * Height )
	{
		return false;
	}
	const int32 Delay = NMath::Clamp( DelayCentiseconds, 1, 65535 );
	const bool bFirst = Previous.Num() == 0;

	// 前の 1 枚から変わった所を囲む矩形（最初の 1 枚は全体）
	int32 Left = 0;
	int32 Top = 0;
	int32 Right = Width - 1;
	int32 Bottom = Height - 1;
	if ( !bFirst )
	{
		Left = Width;
		Top = Height;
		Right = -1;
		Bottom = -1;
		for ( int32 Y = 0; Y < Height; ++Y )
		{
			for ( int32 X = 0; X < Width; ++X )
			{
				if ( !SameRgb( Pixels[Y * Width + X], Previous[Y * Width + X] ) )
				{
					Left = NMath::Min( Left, X );
					Right = NMath::Max( Right, X );
					Top = NMath::Min( Top, Y );
					Bottom = NMath::Max( Bottom, Y );
				}
			}
		}
		if ( Right < 0 )
		{
			// 何も変わらない: 前の 1 枚を長く見せる
			Pending.Delay = NMath::Min( 65535, Pending.Delay + Delay );
			return true;
		}
	}

	FPendingFrame Frame;
	Frame.Left = Left;
	Frame.Top = Top;
	Frame.Width = Right - Left + 1;
	Frame.Height = Bottom - Top + 1;
	Frame.Delay = Delay;
	// 色の表は、矩形の中の変わった画素だけから作る（変わらない画素は透明）
	TArray<FColor32> Changed;
	Changed.Reserve( Frame.Width * Frame.Height );
	for ( int32 Y = Top; Y <= Bottom; ++Y )
	{
		for ( int32 X = Left; X <= Right; ++X )
		{
			const FColor32& Pixel = Pixels[Y * Width + X];
			if ( bFirst || !SameRgb( Pixel, Previous[Y * Width + X] ) )
			{
				Changed.Add( Pixel );
			}
		}
	}
	Frame.Palette = NColorQuantizer::BuildPalette( Changed, bFirst ? 256 : 255 );
	// 色を選ぶのは、透明の番号を足す前の表から（透明の番号が、本物の画素に選ばれないように）
	FPaletteMapper Mapper( Frame.Palette );
	if ( !bFirst )
	{
		Frame.Transparent = Frame.Palette.Num();
		Frame.Palette.Add( FColor32( 0, 0, 0, 255 ) );
	}
	Frame.Indices.Reserve( Frame.Width * Frame.Height );
	for ( int32 Y = Top; Y <= Bottom; ++Y )
	{
		for ( int32 X = Left; X <= Right; ++X )
		{
			const FColor32& Pixel = Pixels[Y * Width + X];
			if ( !bFirst && SameRgb( Pixel, Previous[Y * Width + X] ) )
			{
				Frame.Indices.Add( static_cast<uint8>( Frame.Transparent ) );
			}
			else
			{
				Frame.Indices.Add( Mapper.Map( Pixel ) );
			}
		}
	}

	if ( bHasPending )
	{
		WritePending();
	}
	Pending = MoveTemp( Frame );
	bHasPending = true;
	Previous = TArray<FColor32>( Pixels.GetData(), Pixels.Num() );
	return true;
}

void FGifEncoder::WritePending()
{
	const FPendingFrame& Frame = Pending;
	// 表示時間と透明の番号（Graphic Control Extension）。前の 1 枚は消さずに重ねる（disposal 1）
	Bytes.Add( 0x21 );
	Bytes.Add( 0xF9 );
	Bytes.Add( 4 );
	Bytes.Add( static_cast<uint8>( ( 1 << 2 ) | ( Frame.Transparent >= 0 ? 1 : 0 ) ) );
	AppendU16( Bytes, Frame.Delay );
	Bytes.Add( static_cast<uint8>( Frame.Transparent >= 0 ? Frame.Transparent : 0 ) );
	Bytes.Add( 0 );

	// 位置と大きさ、1 枚ごとの色の表
	const int32 Bits = TableSizeBits( Frame.Palette.Num() );
	Bytes.Add( 0x2C );
	AppendU16( Bytes, Frame.Left );
	AppendU16( Bytes, Frame.Top );
	AppendU16( Bytes, Frame.Width );
	AppendU16( Bytes, Frame.Height );
	Bytes.Add( static_cast<uint8>( 0x80 | Bits ) );
	for ( int32 Index = 0; Index < ( 2 << Bits ); ++Index )
	{
		const FColor32 Color = Index < Frame.Palette.Num() ? Frame.Palette[Index] : FColor32( 0, 0, 0, 255 );
		Bytes.Add( Color.r );
		Bytes.Add( Color.g );
		Bytes.Add( Color.b );
	}

	// 画素（LZW で圧縮して、255 バイトずつに小分けする）
	const int32 MinCodeSize = NMath::Max( 2, Bits + 1 );
	Bytes.Add( static_cast<uint8>( MinCodeSize ) );
	const TArray<uint8> Compressed = NGifLzw::Compress( Frame.Indices, MinCodeSize );
	for ( int32 Offset = 0; Offset < Compressed.Num(); Offset += 255 )
	{
		const int32 Size = NMath::Min( 255, Compressed.Num() - Offset );
		Bytes.Add( static_cast<uint8>( Size ) );
		Bytes.Append( Compressed.GetData() + Offset, Size );
	}
	Bytes.Add( 0 );
	++FrameCount;
}

TArray<uint8> FGifEncoder::Finish()
{
	if ( !IsValid() || bFinished )
	{
		return TArray<uint8>();
	}
	bFinished = true;
	if ( !bHasPending )
	{
		return TArray<uint8>();
	}
	WritePending();
	bHasPending = false;
	Bytes.Add( 0x3B );
	return MoveTemp( Bytes );
}
