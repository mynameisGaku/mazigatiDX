// SPDX-License-Identifier: MIT
#include "UIEasing.h"

#include "Library/Core/Types/CString.h"
#include "Library/Math/MathUtils.h"

namespace
{
	constexpr float32 Pi = 3.14159265358979f;

	const char* const EasingNames[] = { "Linear", "QuadIn", "QuadOut", "QuadInOut", "CubicIn", "CubicOut", "CubicInOut", "SineInOut", "BackOut", "ElasticOut", "BounceOut" };
	static_assert( sizeof( EasingNames ) / sizeof( EasingNames[0] ) == static_cast<int32>( EUIEasing::Count ), "the names must cover every easing" );

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

float32 NUIEasing::Evaluate( EUIEasing Easing, float32 InT )
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
	case EUIEasing::QuadIn:
		return T * T;
	case EUIEasing::QuadOut:
		return 1.0f - ( 1.0f - T ) * ( 1.0f - T );
	case EUIEasing::QuadInOut:
		return T < 0.5f ? 2.0f * T * T : 1.0f - 2.0f * ( 1.0f - T ) * ( 1.0f - T );
	case EUIEasing::CubicIn:
		return T * T * T;
	case EUIEasing::CubicOut:
	{
		const float32 U = 1.0f - T;
		return 1.0f - U * U * U;
	}
	case EUIEasing::CubicInOut:
	{
		const float32 U = 1.0f - T;
		return T < 0.5f ? 4.0f * T * T * T : 1.0f - 4.0f * U * U * U;
	}
	case EUIEasing::SineInOut:
		return -( NMath::Cos( Pi * T ) - 1.0f ) * 0.5f;
	case EUIEasing::BackOut:
	{
		constexpr float32 C1 = 1.70158f;
		constexpr float32 C3 = C1 + 1.0f;
		const float32 U = T - 1.0f;
		return 1.0f + C3 * U * U * U + C1 * U * U;
	}
	case EUIEasing::ElasticOut:
	{
		constexpr float32 C4 = 2.0f * Pi / 3.0f;
		return NMath::Pow( 2.0f, -10.0f * T ) * NMath::Sin( ( T * 10.0f - 0.75f ) * C4 ) + 1.0f;
	}
	case EUIEasing::BounceOut:
		return BounceOut( T );
	default:
		return T;
	}
}

const char* NUIEasing::ToString( EUIEasing Easing )
{
	const int32 Index = static_cast<int32>( Easing );
	return Index >= 0 && Index < static_cast<int32>( EUIEasing::Count ) ? EasingNames[Index] : "Linear";
}

bool NUIEasing::FromString( const char* Name, EUIEasing& OutEasing )
{
	if ( Name == nullptr )
	{
		return false;
	}
	for ( int32 Index = 0; Index < static_cast<int32>( EUIEasing::Count ); ++Index )
	{
		if ( NCString::Compare( Name, EasingNames[Index] ) == 0 )
		{
			OutEasing = static_cast<EUIEasing>( Index );
			return true;
		}
	}
	return false;
}
