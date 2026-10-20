#pragma once
#include "Library/Core/Types/CompilerMacros.h"
#include <cmath>

#include "Library/Platform/DxLib/DxLibInclude.h"   // DxLib の型 (VECTOR / MATRIX など) との変換用
#include "Library/Core/Collection/String.h"       // ToString 用
#include "Library/Math/MathConstants.h"

//=============================================================================
// 数学ユーティリティ
//   角度はすべてラジアン。
//=============================================================================
namespace NMath
{
	//-------------------------------------------------------------------------
	// 基本
	//-------------------------------------------------------------------------
	template<typename T>
	FORCEINLINE constexpr T Abs( T Value ) { return Value < T( 0 ) ? -Value : Value; }

	template<typename T>
	FORCEINLINE constexpr T Min( T A, T B ) { return B < A ? B : A; }

	template<typename T>
	FORCEINLINE constexpr T Max( T A, T B ) { return A < B ? B : A; }

	template<typename T>
	FORCEINLINE constexpr T Clamp( T Value, T Low, T High ) { return Value < Low ? Low : ( High < Value ? High : Value ); }

	FORCEINLINE constexpr float32 Saturate( float32 Value ) { return Clamp( Value, 0.0f, 1.0f ); }

	template<typename T>
	FORCEINLINE constexpr T Sign( T Value ) { return Value > T( 0 ) ? T( 1 ) : ( Value < T( 0 ) ? T( -1 ) : T( 0 ) ); }

	template<typename T>
	FORCEINLINE constexpr T Square( T Value ) { return Value * Value; }

	//-------------------------------------------------------------------------
	// 補間
	//-------------------------------------------------------------------------
	template<typename T>
	FORCEINLINE constexpr T Lerp( const T& A, const T& B, float32 Alpha ) { return A + ( B - A ) * Alpha; }

	// Lerp の逆。Value が [A, B] のどこにあるか (0..1)
	FORCEINLINE constexpr float32 InverseLerp( float32 A, float32 B, float32 Value )
	{
		return ( B - A ) != 0.0f ? ( Value - A ) / ( B - A ) : 0.0f;
	}

	// [InMin, InMax] を [OutMin, OutMax] に写像
	FORCEINLINE constexpr float32 Remap( float32 Value, float32 InMin, float32 InMax, float32 OutMin, float32 OutMax )
	{
		return Lerp( OutMin, OutMax, InverseLerp( InMin, InMax, Value ) );
	}

	FORCEINLINE constexpr float32 SmoothStep( float32 Edge0, float32 Edge1, float32 Value )
	{
		const float32 t = Saturate( InverseLerp( Edge0, Edge1, Value ) );
		return t * t * ( 3.0f - 2.0f * t );
	}

	//-------------------------------------------------------------------------
	// 比較
	//-------------------------------------------------------------------------
	FORCEINLINE bool IsNearlyEqual( float32 A, float32 B, float32 Tolerance = Epsilon ) { return std::fabs( A - B ) <= Tolerance; }
	FORCEINLINE bool IsNearlyZero( float32 Value, float32 Tolerance = Epsilon ) { return std::fabs( Value ) <= Tolerance; }

	//-------------------------------------------------------------------------
	// 冪・平方根
	//-------------------------------------------------------------------------
	FORCEINLINE float32 Sqrt( float32 Value ) { return std::sqrt( Value ); }
	FORCEINLINE float32 InvSqrt( float32 Value ) { return 1.0f / std::sqrt( Value ); }
	FORCEINLINE float32 Pow( float32 Base, float32 Exp ) { return std::pow( Base, Exp ); }
	FORCEINLINE float32 Exp( float32 Value ) { return std::exp( Value ); }
	FORCEINLINE float32 Log( float32 Value ) { return std::log( Value ); }

	//-------------------------------------------------------------------------
	// 三角関数（ラジアン）
	//-------------------------------------------------------------------------
	FORCEINLINE float32 Sin( float32 Rad ) { return std::sin( Rad ); }
	FORCEINLINE float32 Cos( float32 Rad ) { return std::cos( Rad ); }
	FORCEINLINE float32 Tan( float32 Rad ) { return std::tan( Rad ); }
	FORCEINLINE float32 Asin( float32 Value ) { return std::asin( Clamp( Value, -1.0f, 1.0f ) ); }
	FORCEINLINE float32 Acos( float32 Value ) { return std::acos( Clamp( Value, -1.0f, 1.0f ) ); }
	FORCEINLINE float32 Atan( float32 Value ) { return std::atan( Value ); }
	FORCEINLINE float32 Atan2( float32 Y, float32 X ) { return std::atan2( Y, X ); }

	FORCEINLINE constexpr float32 ToRadians( float32 Deg ) { return Deg * DEG2RAD; }
	FORCEINLINE constexpr float32 ToDegrees( float32 Rad ) { return Rad * RAD2DEG; }

	// 角度を [-PI, PI] に正規化
	FORCEINLINE float32 WrapAngle( float32 Rad )
	{
		Rad = std::fmod( Rad + PI, TWO_PI );
		if ( Rad < 0.0f ) Rad += TWO_PI;
		return Rad - PI;
	}

	//-------------------------------------------------------------------------
	// 丸め
	//-------------------------------------------------------------------------
	FORCEINLINE float32 Floor( float32 Value ) { return std::floor( Value ); }
	FORCEINLINE float32 Ceil( float32 Value ) { return std::ceil( Value ); }
	FORCEINLINE float32 Round( float32 Value ) { return std::round( Value ); }
	FORCEINLINE float32 Frac( float32 Value ) { return Value - std::floor( Value ); }
	FORCEINLINE float32 Fmod( float32 X, float32 Y ) { return std::fmod( X, Y ); }
}
