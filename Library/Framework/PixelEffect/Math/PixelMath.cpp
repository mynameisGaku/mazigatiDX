// SPDX-License-Identifier: MIT
#include "Library/Framework/PixelEffect/Math/PixelMath.h"

namespace
{
	constexpr double InvUint32Range = 1.0 / 4294967296.0;

	uint32 MixHashStep( uint32 HashValue, int32 Key )
	{
		uint32 H = ( HashValue ^ static_cast<uint32>( Key ) ) * 0x85ebca6bu;
		H ^= ( H >> 13 );
		H *= 0xc2b2ae35u;
		H ^= ( H >> 16 );
		return H;
	}
}

namespace NPixelMath
{
	uint32 Hash( int32 KeyA )
	{
		return MixHashStep( 0x9e3779b9u, KeyA );
	}

	uint32 Hash( int32 KeyA, int32 KeyB )
	{
		uint32 H = MixHashStep( 0x9e3779b9u, KeyA );
		return MixHashStep( H, KeyB );
	}

	uint32 Hash( int32 KeyA, int32 KeyB, int32 KeyC )
	{
		uint32 H = MixHashStep( 0x9e3779b9u, KeyA );
		H = MixHashStep( H, KeyB );
		return MixHashStep( H, KeyC );
	}

	uint32 HashArray( const int32* Keys, int32 Count )
	{
		uint32 H = 0x9e3779b9u;
		if ( Keys == nullptr || Count <= 0 )
		{
			return H;
		}
		for ( int32 Index = 0; Index < Count; ++Index )
		{
			H = MixHashStep( H, Keys[Index] );
		}
		return H;
	}

	float RandomUnit( int32 KeyA )
	{
		return static_cast<float>( static_cast<double>( Hash( KeyA ) ) * InvUint32Range );
	}

	float RandomUnit( int32 KeyA, int32 KeyB )
	{
		return static_cast<float>( static_cast<double>( Hash( KeyA, KeyB ) ) * InvUint32Range );
	}

	float RandomUnit( int32 KeyA, int32 KeyB, int32 KeyC )
	{
		return static_cast<float>( static_cast<double>( Hash( KeyA, KeyB, KeyC ) ) * InvUint32Range );
	}

	float InverseLerp( float Start, float End, float Value )
	{
		const float Span = End - Start;
		if ( NMath::Abs( Span ) <= 1e-9f )
		{
			return 0.0f;
		}
		return ( Value - Start ) / Span;
	}

	float Remap( float Value, float InStart, float InEnd, float OutStart, float OutEnd )
	{
		return Lerp( OutStart, OutEnd, InverseLerp( InStart, InEnd, Value ) );
	}

	float Fraction( float Value )
	{
		return Value - NMath::Floor( Value );
	}

	float Wrap( float Value, float Period )
	{
		if ( Period <= 0.0f )
		{
			return 0.0f;
		}
		const float Remainder = NMath::Fmod( Value, Period );
		if ( Remainder < 0.0f )
		{
			return Remainder + Period;
		}
		return Remainder;
	}

	int32 WrapInt( int32 Value, int32 Period )
	{
		if ( Period <= 0 )
		{
			return 0;
		}
		const int32 Remainder = Value % Period;
		if ( Remainder < 0 )
		{
			return Remainder + Period;
		}
		return Remainder;
	}

	float SegmentProgress( float Time, float StartTime, float EndTime )
	{
		const float Span = EndTime - StartTime;
		if ( Span <= 1e-9f )
		{
			if ( Time >= EndTime )
			{
				return 1.0f;
			}
			return 0.0f;
		}
		return Clamp01( ( Time - StartTime ) / Span );
	}

	float SmoothStep( float Edge0, float Edge1, float Value )
	{
		const float T = SegmentProgress( Value, Edge0, Edge1 );
		return T * T * ( 3.0f - 2.0f * T );
	}

	float SineBump( float Time, float StartTime, float EndTime )
	{
		if ( Time <= StartTime || Time >= EndTime )
		{
			return 0.0f;
		}
		return NMath::Sin( NMath::PI * SegmentProgress( Time, StartTime, EndTime ) );
	}

	float PingPong( float Value )
	{
		return 1.0f - NMath::Abs( Wrap( Value, 2.0f ) - 1.0f );
	}

	float Distance( float X0, float Y0, float X1, float Y1 )
	{
		return NMath::Hypot( X1 - X0, Y1 - Y0 );
	}

	float AngleBetween( float X0, float Y0, float X1, float Y1 )
	{
		return NMath::Atan2( Y1 - Y0, X1 - X0 );
	}

	float EaseLinear( float T )
	{
		return T;
	}

	float EaseInQuad( float T )
	{
		return T * T;
	}

	float EaseOutQuad( float T )
	{
		const float Inv = 1.0f - T;
		return 1.0f - Inv * Inv;
	}

	float EaseInOutQuad( float T )
	{
		if ( T < 0.5f )
		{
			return 2.0f * T * T;
		}
		const float U = -2.0f * T + 2.0f;
		return 1.0f - ( U * U ) * 0.5f;
	}

	float EaseInCubic( float T )
	{
		return T * T * T;
	}

	float EaseOutCubic( float T )
	{
		const float Inv = 1.0f - T;
		return 1.0f - Inv * Inv * Inv;
	}

	float EaseInOutCubic( float T )
	{
		if ( T < 0.5f )
		{
			return 4.0f * T * T * T;
		}
		const float U = -2.0f * T + 2.0f;
		return 1.0f - ( U * U * U ) * 0.5f;
	}

	float EaseInQuart( float T )
	{
		return T * T * T * T;
	}

	float EaseOutQuart( float T )
	{
		const float Inv = 1.0f - T;
		const float Inv2 = Inv * Inv;
		return 1.0f - Inv2 * Inv2;
	}

	float EaseInExpo( float T )
	{
		if ( T <= 0.0f )
		{
			return 0.0f;
		}
		return NMath::Pow( 2.0f, 10.0f * T - 10.0f );
	}

	float EaseOutExpo( float T )
	{
		if ( T >= 1.0f )
		{
			return 1.0f;
		}
		return 1.0f - NMath::Pow( 2.0f, -10.0f * T );
	}

	float EaseInSine( float T )
	{
		return 1.0f - NMath::Cos( T * NMath::HALF_PI );
	}

	float EaseOutSine( float T )
	{
		return NMath::Sin( T * NMath::HALF_PI );
	}

	float EaseInOutSine( float T )
	{
		return -( NMath::Cos( NMath::PI * T ) - 1.0f ) * 0.5f;
	}

	float EaseInBack( float T, float Overshoot )
	{
		if ( T <= 0.0f )
		{
			return 0.0f;
		}
		if ( T >= 1.0f )
		{
			return 1.0f;
		}
		return ( Overshoot + 1.0f ) * T * T * T - Overshoot * T * T;
	}

	float EaseOutBack( float T, float Overshoot )
	{
		if ( T <= 0.0f )
		{
			return 0.0f;
		}
		if ( T >= 1.0f )
		{
			return 1.0f;
		}
		const float U = T - 1.0f;
		return 1.0f + ( Overshoot + 1.0f ) * U * U * U + Overshoot * U * U;
	}

	float EaseOutElastic( float T )
	{
		if ( T <= 0.0f )
		{
			return 0.0f;
		}
		if ( T >= 1.0f )
		{
			return 1.0f;
		}
		const float Angle = ( T * 10.0f - 0.75f ) * ( NMath::TWO_PI / 3.0f );
		return NMath::Pow( 2.0f, -10.0f * T ) * NMath::Sin( Angle ) + 1.0f;
	}

	float EaseOutBounce( float T )
	{
		constexpr float Factor = 7.5625f;
		constexpr float Divisor = 2.75f;
		if ( T < 1.0f / Divisor )
		{
			return Factor * T * T;
		}
		if ( T < 2.0f / Divisor )
		{
			const float U = T - 1.5f / Divisor;
			return Factor * U * U + 0.75f;
		}
		if ( T < 2.5f / Divisor )
		{
			const float U = T - 2.25f / Divisor;
			return Factor * U * U + 0.9375f;
		}
		const float U = T - 2.625f / Divisor;
		return Factor * U * U + 0.984375f;
	}

	float Noise1D( float X, int32 Seed )
	{
		const int32 Index = FloorToInt( X );
		const float Frac = X - static_cast<float>( Index );
		const float Smooth = Frac * Frac * ( 3.0f - 2.0f * Frac );
		const float Left = RandomUnit( Index, Seed );
		const float Right = RandomUnit( Index + 1, Seed );
		return Lerp( Left, Right, Smooth ) * 2.0f - 1.0f;
	}

	float Noise2D( float X, float Y, int32 Seed )
	{
		const int32 IndexX = FloorToInt( X );
		const int32 IndexY = FloorToInt( Y );
		const float FracX = X - static_cast<float>( IndexX );
		const float FracY = Y - static_cast<float>( IndexY );
		const float SmoothX = FracX * FracX * ( 3.0f - 2.0f * FracX );
		const float SmoothY = FracY * FracY * ( 3.0f - 2.0f * FracY );
		const float A = RandomUnit( IndexX, IndexY, Seed );
		const float B = RandomUnit( IndexX + 1, IndexY, Seed );
		const float C = RandomUnit( IndexX, IndexY + 1, Seed );
		const float D = RandomUnit( IndexX + 1, IndexY + 1, Seed );
		return Lerp( Lerp( A, B, SmoothX ), Lerp( C, D, SmoothX ), SmoothY ) * 2.0f - 1.0f;
	}

	float FractalNoise2D( float X, float Y, int32 Seed, int32 Octaves )
	{
		if ( Octaves <= 0 )
		{
			return 0.0f;
		}
		float Sum = 0.0f;
		float Amplitude = 0.5f;
		float WeightSum = 0.0f;
		float SampleX = X;
		float SampleY = Y;
		for ( int32 Octave = 0; Octave < Octaves; ++Octave )
		{
			Sum += Noise2D( SampleX, SampleY, Seed + Octave * 101 ) * Amplitude;
			WeightSum += Amplitude;
			SampleX *= 2.03f;
			SampleY *= 2.03f;
			Amplitude *= 0.5f;
		}
		return Sum / WeightSum;
	}

	float TileNoise2D( float X, float Y, int32 PeriodX, int32 PeriodY, int32 Seed )
	{
		const int32 IndexX = FloorToInt( X );
		const int32 IndexY = FloorToInt( Y );
		const float FracX = X - static_cast<float>( IndexX );
		const float FracY = Y - static_cast<float>( IndexY );
		const float SmoothX = FracX * FracX * ( 3.0f - 2.0f * FracX );
		const float SmoothY = FracY * FracY * ( 3.0f - 2.0f * FracY );
		const int32 X0 = PeriodX > 0 ? WrapInt( IndexX, PeriodX ) : IndexX;
		const int32 X1 = PeriodX > 0 ? WrapInt( IndexX + 1, PeriodX ) : ( IndexX + 1 );
		const int32 Y0 = PeriodY > 0 ? WrapInt( IndexY, PeriodY ) : IndexY;
		const int32 Y1 = PeriodY > 0 ? WrapInt( IndexY + 1, PeriodY ) : ( IndexY + 1 );
		const float A = RandomUnit( X0, Y0, Seed );
		const float B = RandomUnit( X1, Y0, Seed );
		const float C = RandomUnit( X0, Y1, Seed );
		const float D = RandomUnit( X1, Y1, Seed );
		return Lerp( Lerp( A, B, SmoothX ), Lerp( C, D, SmoothX ), SmoothY ) * 2.0f - 1.0f;
	}

	float TileFractalNoise2D( float X, float Y, int32 PeriodX, int32 PeriodY, int32 Seed, int32 Octaves )
	{
		if ( Octaves <= 0 )
		{
			return 0.0f;
		}
		float Sum = 0.0f;
		float Amplitude = 0.5f;
		float WeightSum = 0.0f;
		int32 Scale = 1;
		for ( int32 Octave = 0; Octave < Octaves; ++Octave )
		{
			const float Freq = static_cast<float>( Scale );
			Sum += TileNoise2D( X * Freq, Y * Freq, PeriodX * Scale, PeriodY * Scale, Seed + Octave * 101 ) * Amplitude;
			WeightSum += Amplitude;
			Amplitude *= 0.5f;
			Scale *= 2;
		}
		return Sum / WeightSum;
	}

	float LoopNoise( float Phase, float Radius, int32 Seed )
	{
		const float Angle = Phase * NMath::TWO_PI;
		return Noise2D( NMath::Cos( Angle ) * Radius + 50.0f, NMath::Sin( Angle ) * Radius + 50.0f, Seed );
	}
}
