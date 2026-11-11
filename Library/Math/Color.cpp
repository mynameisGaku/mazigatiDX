#include "Color.h"


FColor FColor::FromHSV( float32 H, float32 S, float32 V, float32 A )
{
	const float32 C  = V * S;
	const float32 Hp = NMath::Fmod( NMath::Fmod( H, 360.0f ) + 360.0f, 360.0f ) / 60.0f;
	const float32 X  = C * ( 1.0f - NMath::Abs( NMath::Fmod( Hp, 2.0f ) - 1.0f ) );
	const float32 M  = V - C;

	float32 R = 0.0f, G = 0.0f, B = 0.0f;
	if      ( Hp < 1.0f )
	{
		R = C; G = X;
	}
	else if ( Hp < 2.0f )
	{
		R = X; G = C;
	}
	else if ( Hp < 3.0f )
	{
		G = C; B = X;
	}
	else if ( Hp < 4.0f )
	{
		G = X; B = C;
	}
	else if ( Hp < 5.0f )
	{
		R = X; B = C;
	}
	else
	{
		R = C; B = X;
	}
	return { R + M, G + M, B + M, A };
}

namespace
{
	int32 HexDigitValue( char Ch )
	{
		if ( Ch >= '0' && Ch <= '9' )
		{
			return Ch - '0';
		}
		if ( Ch >= 'a' && Ch <= 'f' )
		{
			return Ch - 'a' + 10;
		}
		if ( Ch >= 'A' && Ch <= 'F' )
		{
			return Ch - 'A' + 10;
		}
		return -1;
	}
}

bool FColor32::TryParseHex( const FString& Text, FColor32& OutColor )
{
	const FString Trimmed = Text.TrimStartAndEnd();
	if ( Trimmed.Len() < 2 || ( *Trimmed )[0] != '#' )
	{
		return false;
	}
	const int32 Digits = Trimmed.Len() - 1;
	if ( Digits != 3 && Digits != 6 && Digits != 8 )
	{
		return false;
	}
	int32 Values[8] = {};
	for ( int32 Index = 0; Index < Digits; ++Index )
	{
		Values[Index] = HexDigitValue( ( *Trimmed )[Index + 1] );
		if ( Values[Index] < 0 )
		{
			return false;
		}
	}
	if ( Digits == 3 )
	{
		// 1 桁を、2 桁に広げる（F → FF）
		OutColor = FColor32( static_cast<uint8>( Values[0] * 17 ), static_cast<uint8>( Values[1] * 17 ), static_cast<uint8>( Values[2] * 17 ), 255 );
		return true;
	}
	OutColor = FColor32( static_cast<uint8>( Values[0] * 16 + Values[1] ), static_cast<uint8>( Values[2] * 16 + Values[3] ), static_cast<uint8>( Values[4] * 16 + Values[5] ),
						 Digits == 8 ? static_cast<uint8>( Values[6] * 16 + Values[7] ) : static_cast<uint8>( 255 ) );
	return true;
}
