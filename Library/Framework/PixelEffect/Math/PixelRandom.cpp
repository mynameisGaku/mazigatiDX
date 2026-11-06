// SPDX-License-Identifier: MIT
#include "Library/Framework/PixelEffect/Math/PixelRandom.h"

#include "Library/Framework/PixelEffect/Math/PixelMath.h"
#include "Library/Math/MathUtils.h"

namespace
{
	constexpr double InvUint32Range = 1.0 / 4294967296.0;
}

FPixelRandom::FPixelRandom( uint32 InSeed )
	: State( InSeed != 0u ? InSeed : 1u )
{
}

FPixelRandom FPixelRandom::FromKeys( int32 KeyA, int32 KeyB, int32 KeyC )
{
	return FPixelRandom( NPixelMath::Hash( KeyA, KeyB, KeyC ) );
}

float FPixelRandom::NextFloat()
{
	State += 0x6d2b79f5u;
	uint32 T = State;
	T = ( T ^ ( T >> 15 ) ) * ( T | 1u );
	T ^= T + ( ( T ^ ( T >> 7 ) ) * ( T | 61u ) );
	const uint32 Bits = ( T ^ ( T >> 14 ) );
	return static_cast<float>( static_cast<double>( Bits ) * InvUint32Range );
}

float FPixelRandom::Range( float MinValue, float MaxValue )
{
	return MinValue + ( MaxValue - MinValue ) * NextFloat();
}

int32 FPixelRandom::RangeInt( int32 MinInclusive, int32 MaxInclusive )
{
	if ( MinInclusive > MaxInclusive )
	{
		const int32 Temp = MinInclusive;
		MinInclusive = MaxInclusive;
		MaxInclusive = Temp;
	}
	const double Span = static_cast<double>( static_cast<int64>( MaxInclusive ) - static_cast<int64>( MinInclusive ) + 1 );
	const int64 Offset = static_cast<int64>( NMath::Floor( static_cast<double>( NextFloat() ) * Span ) );
	const int64 Result = static_cast<int64>( MinInclusive ) + Offset;
	if ( Result > static_cast<int64>( MaxInclusive ) )
	{
		return MaxInclusive;
	}
	return static_cast<int32>( Result );
}

bool FPixelRandom::Chance( float Probability )
{
	if ( Probability <= 0.0f )
	{
		return false;
	}
	if ( Probability >= 1.0f )
	{
		return true;
	}
	return NextFloat() < Probability;
}

int32 FPixelRandom::Sign()
{
	if ( NextFloat() < 0.5f )
	{
		return -1;
	}
	return 1;
}

float FPixelRandom::Gaussian()
{
	const float Sum = NextFloat() + NextFloat() + NextFloat() + NextFloat() - 2.0f;
	return Sum * 1.732f;
}
