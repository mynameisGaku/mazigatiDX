// SPDX-License-Identifier: MIT
#include "Utf8ByteAssembler.h"

namespace
{
	// 先頭バイトから、その文字の全体のバイト数を返す。先頭バイトとして使えなければ 0
	int32 GetSequenceLength( uint8 Lead )
	{
		if ( Lead >= 0xC2 && Lead <= 0xDF )
		{
			return 2;
		}
		if ( Lead >= 0xE0 && Lead <= 0xEF )
		{
			return 3;
		}
		if ( Lead >= 0xF0 && Lead <= 0xF4 )
		{
			return 4;
		}
		return 0;
	}

	// 揃ったバイト列を、コードポイントにする。不正（短すぎる表現・サロゲート・範囲の外）なら false
	bool Decode( const uint8* Bytes, int32 Count, uint32& OutCodePoint )
	{
		uint32 Value = 0;
		if ( Count == 2 )
		{
			Value = static_cast<uint32>( Bytes[0] & 0x1F );
		}
		else if ( Count == 3 )
		{
			Value = static_cast<uint32>( Bytes[0] & 0x0F );
		}
		else
		{
			Value = static_cast<uint32>( Bytes[0] & 0x07 );
		}
		for ( int32 Index = 1; Index < Count; ++Index )
		{
			Value = ( Value << 6 ) | static_cast<uint32>( Bytes[Index] & 0x3F );
		}
		const uint32 MinimumValues[5] = { 0, 0, 0x80, 0x800, 0x10000 };
		if ( Value < MinimumValues[Count] || Value > 0x10FFFF || ( Value >= 0xD800 && Value <= 0xDFFF ) )
		{
			return false;
		}
		OutCodePoint = Value;
		return true;
	}
}

bool FUtf8ByteAssembler::Push( uint8 Byte, FString& OutText )
{
	// ASCII: 途中のものがあれば捨てる。制御文字は捨てる
	if ( Byte < 0x80 )
	{
		Reset();
		if ( Byte < 0x20 || Byte == 0x7F )
		{
			return false;
		}
		const char Character = static_cast<char>( Byte );
		OutText.Append( &Character, 1 );
		return true;
	}

	// 続きのバイト
	if ( Byte < 0xC0 )
	{
		if ( PendingCount <= 0 )
		{
			return false;
		}
		Pending[PendingCount++] = Byte;
		if ( PendingCount < ExpectedCount )
		{
			return false;
		}
		uint32 CodePoint = 0;
		const bool bValid = Decode( Pending, PendingCount, CodePoint );
		if ( bValid )
		{
			OutText.Append( reinterpret_cast<const char*>( Pending ), PendingCount );
		}
		Reset();
		return bValid;
	}

	// 先頭のバイト: 途中のものを捨てて、数え直す
	Reset();
	const int32 Length = GetSequenceLength( Byte );
	if ( Length == 0 )
	{
		return false;
	}
	Pending[0] = Byte;
	PendingCount = 1;
	ExpectedCount = Length;
	return false;
}

void FUtf8ByteAssembler::Reset()
{
	PendingCount = 0;
	ExpectedCount = 0;
}
