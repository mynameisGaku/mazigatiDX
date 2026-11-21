// SPDX-License-Identifier: MIT
#include "DX12HalfFloat.h"
#include "Library/Core/Memory/Memory.h"

uint16 NDX12HalfFloat::FromFloat32( float32 Value )
{
	uint32 Bits = 0;
	FMemory::Memcpy( &Bits, &Value, sizeof( Bits ) );
	const uint32 Sign = ( Bits >> 16 ) & 0x8000u;
	const int32 Exp = static_cast<int32>( ( Bits >> 23 ) & 0xFFu ) - 127 + 15;
	uint32 Mant = Bits & 0x007FFFFFu;
	if ( ( Bits & 0x7FFFFFFFu ) == 0 )
	{
		return static_cast<uint16>( Sign );
	}
	if ( Exp >= 31 )
	{
		return static_cast<uint16>( Sign | 0x7C00u );
	}
	if ( Exp <= 0 )
	{
		if ( Exp < -10 )
		{
			return static_cast<uint16>( Sign );
		}
		Mant |= 0x00800000u;
		const uint32 Shift = static_cast<uint32>( 14 - Exp );
		uint32 HalfMant = Mant >> Shift;
		const uint32 Remainder = Mant & ( ( 1u << Shift ) - 1u );
		const uint32 Halfway = 1u << ( Shift - 1u );
		if ( Remainder > Halfway || ( Remainder == Halfway && ( HalfMant & 1u ) != 0 ) )
		{
			++HalfMant;
		}
		return static_cast<uint16>( Sign | HalfMant );
	}
	uint32 Half = Sign | ( static_cast<uint32>( Exp ) << 10 ) | ( Mant >> 13 );
	const uint32 RoundBits = Mant & 0x1FFFu;
	if ( RoundBits > 0x1000u || ( RoundBits == 0x1000u && ( Half & 1u ) != 0 ) )
	{
		++Half;
	}
	return static_cast<uint16>( Half );
}

float32 NDX12HalfFloat::ToFloat32( uint16 Half )
{
	const uint32 Sign = static_cast<uint32>( Half & 0x8000u ) << 16;
	const uint32 Exp = ( Half >> 10 ) & 0x1Fu;
	const uint32 Mant = Half & 0x03FFu;
	uint32 Bits = 0;
	if ( Exp == 0 )
	{
		if ( Mant == 0 )
		{
			Bits = Sign;
		}
		else
		{
			uint32 M = Mant;
			int32 E = 127 - 14;
			while ( ( M & 0x0400u ) == 0 )
			{
				M <<= 1;
				--E;
			}
			M &= 0x03FFu;
			Bits = Sign | ( static_cast<uint32>( E ) << 23 ) | ( M << 13 );
		}
	}
	else if ( Exp == 31 )
	{
		Bits = Sign | 0x7F800000u | ( Mant << 13 );
	}
	else
	{
		Bits = Sign | ( ( Exp + ( 127 - 15 ) ) << 23 ) | ( Mant << 13 );
	}
	float32 Out = 0.0f;
	FMemory::Memcpy( &Out, &Bits, sizeof( Out ) );
	return Out;
}
