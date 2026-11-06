// SPDX-License-Identifier: MIT
#include "RandomStream.h"

#include "Library/Core/Types/Numeric.h"
#include "Library/Math/MathUtils.h"

namespace
{
	constexpr uint64 Multiplier = 6364136223846793005ULL;
	constexpr float32 Pi = 3.14159265358979f;
}

FRandomStream::FRandomStream()
{
	Reset( 0, 54 );
}

FRandomStream::FRandomStream( uint64 Seed, uint64 Sequence )
{
	Reset( Seed, Sequence );
}

void FRandomStream::Reset( uint64 Seed, uint64 Sequence )
{
	// pcg32_srandom_r と同じ手順
	State = 0;
	Increment = ( Sequence << 1u ) | 1u;
	NextUInt32();
	State += Seed;
	NextUInt32();
}

FRandomStream::FState FRandomStream::GetState() const
{
	FState Result;
	Result.State = State;
	Result.Increment = Increment;
	return Result;
}

void FRandomStream::SetState( const FState& InState )
{
	State = InState.State;
	// 増分は奇数でなければならない
	Increment = InState.Increment | 1u;
}

uint32 FRandomStream::NextUInt32()
{
	const uint64 Old = State;
	State = Old * Multiplier + Increment;
	const uint32 XorShifted = static_cast<uint32>( ( ( Old >> 18u ) ^ Old ) >> 27u );
	const uint32 Rotation = static_cast<uint32>( Old >> 59u );
	return ( XorShifted >> Rotation ) | ( XorShifted << ( ( 0u - Rotation ) & 31u ) );
}

uint32 FRandomStream::NextUInt32( uint32 Bound )
{
	if ( Bound == 0 )
	{
		return 0;
	}
	// 偏りをなくすため、割り切れない端の範囲は引き直す
	const uint32 Threshold = ( 0u - Bound ) % Bound;
	for ( ;; )
	{
		const uint32 Value = NextUInt32();
		if ( Value >= Threshold )
		{
			return Value % Bound;
		}
	}
}

int32 FRandomStream::NextInt( int32 Min, int32 MaxInclusive )
{
	if ( Min > MaxInclusive )
	{
		const int32 Temp = Min;
		Min = MaxInclusive;
		MaxInclusive = Temp;
	}
	// 幅は uint32 で数える（Min が最小値・Max が最大値でも溢れない）
	const uint32 Span = static_cast<uint32>( MaxInclusive ) - static_cast<uint32>( Min ) + 1u;
	const uint32 Offset = Span == 0u ? NextUInt32() : NextUInt32( Span );
	return static_cast<int32>( static_cast<uint32>( Min ) + Offset );
}

float32 FRandomStream::NextFloat()
{
	// 上位 24 ビットを使う（float32 の仮数部の幅。1 未満になる）
	return static_cast<float32>( NextUInt32() >> 8u ) * ( 1.0f / 16777216.0f );
}

float32 FRandomStream::NextFloat( float32 Min, float32 Max )
{
	return Min + ( Max - Min ) * NextFloat();
}

bool FRandomStream::NextBool( float32 TrueProbability )
{
	if ( !( TrueProbability > 0.0f ) )
	{
		return false;
	}
	if ( TrueProbability >= 1.0f )
	{
		return true;
	}
	return NextFloat() < TrueProbability;
}

float32 FRandomStream::NextGaussian( float32 Mean, float32 StdDev )
{
	// Box-Muller 法。対数の中が 0 にならないよう、0 を避ける
	float32 U1 = NextFloat();
	while ( U1 <= 0.0f )
	{
		U1 = NextFloat();
	}
	const float32 U2 = NextFloat();
	const float32 Radius = NMath::Sqrt( -2.0f * NMath::Log( U1 ) );
	return Mean + StdDev * Radius * NMath::Cos( 2.0f * Pi * U2 );
}

FVector3 FRandomStream::NextUnitVector()
{
	// 高さ Z を一様に、角度を一様に取ると、球面上に一様になる
	const float32 Z = NextFloat( -1.0f, 1.0f );
	const float32 Angle = NextFloat( 0.0f, 2.0f * Pi );
	const float32 Radius = NMath::Sqrt( NMath::Max( 0.0f, 1.0f - Z * Z ) );
	return FVector3( Radius * NMath::Cos( Angle ), Radius * NMath::Sin( Angle ), Z );
}

FVector2 FRandomStream::NextUnitVector2()
{
	const float32 Angle = NextFloat( 0.0f, 2.0f * Pi );
	return FVector2( NMath::Cos( Angle ), NMath::Sin( Angle ) );
}
