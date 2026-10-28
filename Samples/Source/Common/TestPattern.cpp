#include "TestPattern.h"

namespace
{
	// 1 マスの大きさ（ピクセル）と、1 辺のマスの数
	constexpr int32 CellSize = 8;
	constexpr int32 NumCells = NTestPattern::Size / CellSize;

	// マス (Column, Row) の色（R, G, B）
	void CellColor( int32 Column, int32 Row, uint8& OutR, uint8& OutG, uint8& OutB )
	{
		const bool bLeft = Column == 0;
		const bool bRight = Column == NumCells - 1;
		const bool bTop = Row == 0;
		const bool bBottom = Row == NumCells - 1;
		if ( bLeft && bTop )
		{
			OutR = 255;
			OutG = 255;
			OutB = 255;
		}
		else if ( bRight && bTop )
		{
			OutR = 255;
			OutG = 0;
			OutB = 0;
		}
		else if ( bLeft && bBottom )
		{
			OutR = 0;
			OutG = 255;
			OutB = 0;
		}
		else if ( bRight && bBottom )
		{
			OutR = 0;
			OutG = 0;
			OutB = 255;
		}
		else
		{
			OutR = static_cast<uint8>( 40 + Column * 12 );
			OutG = static_cast<uint8>( 40 + Row * 12 );
			OutB = static_cast<uint8>( 200 - ( Column + Row ) * 8 );
		}
	}

	// 整数を、リトルエンディアンで足す
	void PutU32( TArray<uint8>& Bytes, uint32 Value )
	{
		for ( int32 Shift = 0; Shift < 32; Shift += 8 )
		{
			Bytes.Add( static_cast<uint8>( ( Value >> Shift ) & 0xFFu ) );
		}
	}

	void PutU16( TArray<uint8>& Bytes, uint32 Value )
	{
		Bytes.Add( static_cast<uint8>( Value & 0xFFu ) );
		Bytes.Add( static_cast<uint8>( ( Value >> 8 ) & 0xFFu ) );
	}
}

TArray<uint8> NTestPattern::MakeBitmap()
{
	// 24 ビットの BMP。1 行は 4 バイトの倍数（64 x 3 = 192 なので、そのまま）。行は下から並べる
	const int32 RowBytes = Size * 3;
	const uint32 ImageBytes = static_cast<uint32>( RowBytes * Size );
	TArray<uint8> Bytes;
	Bytes.Add( 'B' );
	Bytes.Add( 'M' );
	PutU32( Bytes, 54 + ImageBytes );
	PutU32( Bytes, 0 );
	PutU32( Bytes, 54 );
	PutU32( Bytes, 40 );
	PutU32( Bytes, static_cast<uint32>( Size ) );
	PutU32( Bytes, static_cast<uint32>( Size ) );
	PutU16( Bytes, 1 );
	PutU16( Bytes, 24 );
	PutU32( Bytes, 0 );
	PutU32( Bytes, ImageBytes );
	PutU32( Bytes, 2835 );
	PutU32( Bytes, 2835 );
	PutU32( Bytes, 0 );
	PutU32( Bytes, 0 );
	for ( int32 Y = Size - 1; Y >= 0; --Y )
	{
		for ( int32 X = 0; X < Size; ++X )
		{
			uint8 R = 0;
			uint8 G = 0;
			uint8 B = 0;
			CellColor( X / CellSize, Y / CellSize, R, G, B );
			Bytes.Add( B );
			Bytes.Add( G );
			Bytes.Add( R );
		}
	}
	return Bytes;
}
