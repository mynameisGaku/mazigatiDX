// SPDX-License-Identifier: MIT
#include "Library/Framework/Capture/Encoding/GifLzw.h"

#include "Library/Math/MathUtils.h"

namespace
{
	constexpr int32 MaxCodes = 4096;

	// 符号を、最下位ビットから順にバイト列へ詰める
	class FBitWriter
	{
	public:
		explicit FBitWriter( TArray<uint8>& InOut )
			: Out( InOut )
		{
		}

		void Write( uint32 Code, int32 Bits )
		{
			Buffer |= Code << Count;
			Count += Bits;
			while ( Count >= 8 )
			{
				Out.Add( static_cast<uint8>( Buffer & 0xFF ) );
				Buffer >>= 8;
				Count -= 8;
			}
		}

		void Flush()
		{
			if ( Count > 0 )
			{
				Out.Add( static_cast<uint8>( Buffer & 0xFF ) );
			}
			Buffer = 0;
			Count = 0;
		}

	private:
		TArray<uint8>& Out;
		uint32 Buffer = 0;
		int32 Count = 0;
	};

	// 符号の表: (いまの符号, 次の色) -> 新しい符号。世代の番号で、表を一瞬で空にする
	class FCodeTable
	{
	public:
		FCodeTable()
		{
			Codes.SetNum( MaxCodes * 256, 0 );
			Stamps.SetNum( MaxCodes * 256, 0 );
		}

		void Clear()
		{
			++Generation;
		}

		int32 Find( int32 Prefix, uint8 Next ) const
		{
			const int32 Slot = Prefix * 256 + Next;
			return Stamps[Slot] == Generation ? static_cast<int32>( Codes[Slot] ) : -1;
		}

		void Add( int32 Prefix, uint8 Next, int32 Code )
		{
			const int32 Slot = Prefix * 256 + Next;
			Codes[Slot] = static_cast<uint16>( Code );
			Stamps[Slot] = Generation;
		}

	private:
		TArray<uint16> Codes;
		TArray<uint32> Stamps;
		uint32 Generation = 1;
	};
}

TArray<uint8> NGifLzw::Compress( TArrayView<const uint8> Indices, int32 MinCodeSize )
{
	TArray<uint8> Out;
	const int32 Minimum = NMath::Clamp( MinCodeSize, 2, 8 );
	const int32 ClearCode = 1 << Minimum;
	const int32 EndCode = ClearCode + 1;
	FBitWriter Writer( Out );
	FCodeTable Table;
	int32 CodeSize = Minimum + 1;
	int32 LastCode = EndCode;
	Writer.Write( static_cast<uint32>( ClearCode ), CodeSize );
	int32 Current = -1;
	for ( int32 Index = 0; Index < Indices.Num(); ++Index )
	{
		// 2^MinCodeSize 以上の番号は、表に収まるよう切り詰める
		const uint8 Next = static_cast<uint8>( Indices[Index] & ( ClearCode - 1 ) );
		if ( Current < 0 )
		{
			Current = Next;
			continue;
		}
		const int32 Found = Table.Find( Current, Next );
		if ( Found >= 0 )
		{
			Current = Found;
			continue;
		}
		Writer.Write( static_cast<uint32>( Current ), CodeSize );
		++LastCode;
		Table.Add( Current, Next, LastCode );
		if ( LastCode >= ( 1 << CodeSize ) )
		{
			++CodeSize;
		}
		if ( LastCode == MaxCodes - 1 )
		{
			// 表がいっぱい: 初めからやり直す
			Writer.Write( static_cast<uint32>( ClearCode ), CodeSize );
			Table.Clear();
			CodeSize = Minimum + 1;
			LastCode = EndCode;
		}
		Current = Next;
	}
	if ( Current >= 0 )
	{
		Writer.Write( static_cast<uint32>( Current ), CodeSize );
	}
	Writer.Write( static_cast<uint32>( EndCode ), CodeSize );
	Writer.Flush();
	return Out;
}
