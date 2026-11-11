// SPDX-License-Identifier: MIT
#include "Easing.h"

#include "Library/Core/Types/CString.h"
#include "Library/Math/MathUtils.h"

namespace
{
	// Back の行き過ぎの強さ（終点の 1 割ほど）
	constexpr float32 BackStrength = 1.70158f;

	const char* const EasingNames[] = { "Linear",  "QuadIn",  "QuadOut",   "QuadInOut", "CubicIn", "CubicOut",  "CubicInOut", "QuartIn",    "QuartOut",  "QuartInOut", "SineIn",
										"SineOut", "SineInOut", "ExpoIn", "ExpoOut",   "ExpoInOut", "BackIn",  "BackOut",   "BackInOut",  "ElasticOut", "BounceOut" };
	static_assert( sizeof( EasingNames ) / sizeof( EasingNames[0] ) == static_cast<int32>( EEasing::Count ), "the names must cover every easing" );

	float32 PowerIn( float32 T, int32 Power )
	{
		float32 Result = T;
		for ( int32 Index = 1; Index < Power; ++Index )
		{
			Result *= T;
		}
		return Result;
	}

	float32 PowerInOut( float32 T, int32 Power )
	{
		// 前半は In を半分に縮めたもの、後半は、その点対称
		return T < 0.5f ? 0.5f * PowerIn( T * 2.0f, Power ) : 1.0f - 0.5f * PowerIn( ( 1.0f - T ) * 2.0f, Power );
	}

	float32 ExpoIn( float32 T )
	{
		// 2^(10(T-1)) は、T = 0 で 1/1024 になる。0 から始まるように、ずらして伸ばす
		constexpr float32 Floor = 1.0f / 1024.0f;
		return ( NMath::Pow( 2.0f, 10.0f * ( T - 1.0f ) ) - Floor ) / ( 1.0f - Floor );
	}

	float32 BackIn( float32 T )
	{
		return ( BackStrength + 1.0f ) * T * T * T - BackStrength * T * T;
	}

	float32 BounceOut( float32 T )
	{
		constexpr float32 N = 7.5625f;
		constexpr float32 D = 2.75f;
		if ( T < 1.0f / D )
		{
			return N * T * T;
		}
		if ( T < 2.0f / D )
		{
			const float32 U = T - 1.5f / D;
			return N * U * U + 0.75f;
		}
		if ( T < 2.5f / D )
		{
			const float32 U = T - 2.25f / D;
			return N * U * U + 0.9375f;
		}
		const float32 U = T - 2.625f / D;
		return N * U * U + 0.984375f;
	}
}

float32 NEasing::Evaluate( EEasing Easing, float32 InT )
{
	const float32 T = NMath::Clamp( InT, 0.0f, 1.0f );
	if ( T <= 0.0f )
	{
		return 0.0f;
	}
	if ( T >= 1.0f )
	{
		return 1.0f;
	}
	switch ( Easing )
	{
	case EEasing::QuadIn:
		return PowerIn( T, 2 );
	case EEasing::QuadOut:
		return 1.0f - PowerIn( 1.0f - T, 2 );
	case EEasing::QuadInOut:
		return PowerInOut( T, 2 );
	case EEasing::CubicIn:
		return PowerIn( T, 3 );
	case EEasing::CubicOut:
		return 1.0f - PowerIn( 1.0f - T, 3 );
	case EEasing::CubicInOut:
		return PowerInOut( T, 3 );
	case EEasing::QuartIn:
		return PowerIn( T, 4 );
	case EEasing::QuartOut:
		return 1.0f - PowerIn( 1.0f - T, 4 );
	case EEasing::QuartInOut:
		return PowerInOut( T, 4 );
	case EEasing::SineIn:
		return 1.0f - NMath::Cos( T * NMath::PI * 0.5f );
	case EEasing::SineOut:
		return NMath::Sin( T * NMath::PI * 0.5f );
	case EEasing::SineInOut:
		return -( NMath::Cos( NMath::PI * T ) - 1.0f ) * 0.5f;
	case EEasing::ExpoIn:
		return ExpoIn( T );
	case EEasing::ExpoOut:
		return 1.0f - ExpoIn( 1.0f - T );
	case EEasing::ExpoInOut:
		return T < 0.5f ? 0.5f * ExpoIn( T * 2.0f ) : 1.0f - 0.5f * ExpoIn( ( 1.0f - T ) * 2.0f );
	case EEasing::BackIn:
		return BackIn( T );
	case EEasing::BackOut:
		return 1.0f - BackIn( 1.0f - T );
	case EEasing::BackInOut:
		return T < 0.5f ? 0.5f * BackIn( T * 2.0f ) : 1.0f - 0.5f * BackIn( ( 1.0f - T ) * 2.0f );
	case EEasing::ElasticOut:
	{
		constexpr float32 C4 = 2.0f * NMath::PI / 3.0f;
		return NMath::Pow( 2.0f, -10.0f * T ) * NMath::Sin( ( T * 10.0f - 0.75f ) * C4 ) + 1.0f;
	}
	case EEasing::BounceOut:
		return BounceOut( T );
	default:
		return T;
	}
}

const char* NEasing::ToString( EEasing Easing )
{
	const int32 Index = static_cast<int32>( Easing );
	return Index >= 0 && Index < static_cast<int32>( EEasing::Count ) ? EasingNames[Index] : "Linear";
}

bool NEasing::FromString( const char* Name, EEasing& OutEasing )
{
	if ( Name == nullptr )
	{
		return false;
	}
	for ( int32 Index = 0; Index < static_cast<int32>( EEasing::Count ); ++Index )
	{
		if ( NCString::Compare( Name, EasingNames[Index] ) == 0 )
		{
			OutEasing = static_cast<EEasing>( Index );
			return true;
		}
	}
	return false;
}
