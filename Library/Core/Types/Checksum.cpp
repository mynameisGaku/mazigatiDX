// SPDX-License-Identifier: MIT
#include "Checksum.h"

namespace
{
	// 反転した多項式 0xEDB88320 の表（最初に使うときに 1 度だけ作られる。関数内の static の初期化は、スレッドから同時に呼ばれても安全）
	struct FCrcTable
	{
		uint32 Values[256];

		FCrcTable()
		{
			for ( uint32 Index = 0; Index < 256; ++Index )
			{
				uint32 Value = Index;
				for ( int32 Bit = 0; Bit < 8; ++Bit )
				{
					Value = ( Value & 1u ) != 0 ? ( 0xEDB88320u ^ ( Value >> 1 ) ) : ( Value >> 1 );
				}
				Values[Index] = Value;
			}
		}
	};

	const uint32* GetTable()
	{
		static const FCrcTable Table;
		return Table.Values;
	}
}

uint32 NChecksum::Crc32( const void* Data, usize Size, uint32 Previous )
{
	const uint32* Table = GetTable();
	const uint8* Bytes = static_cast<const uint8*>( Data );
	uint32 Crc = ~Previous;
	for ( usize Index = 0; Index < Size; ++Index )
	{
		Crc = Table[( Crc ^ Bytes[Index] ) & 0xFFu] ^ ( Crc >> 8 );
	}
	return ~Crc;
}
