#pragma once
#include "Library/Core/Types/CompilerMacros.h"
#include <cmath>

#include "Library/Core/Types/Numeric.h"
#include "Library/Platform/DxLib/DxLibInclude.h"   // DxLib の型 (VECTOR / MATRIX など) との変換用
#include "Library/Core/Collection/String.h"       // ToString 用
#include "Library/Core/Types/Numeric.h"   // NMath::Abs / Min / Max / Clamp / Sign / Square / IsFinite など
#include "Library/Math/MathConstants.h"

//=============================================================================
// 数学ユーティリティ
//   角度はすべてラジアン。
//=============================================================================
namespace NMath
{
	//-------------------------------------------------------------------------
	// 基本（Abs / Min / Max / Clamp / Sign / Square は Core/Types/Numeric.h）
	//-------------------------------------------------------------------------
	FORCEINLINE constexpr float32 Saturate( float32 Value ) { return Clamp( Value, 0.0f, 1.0f ); }

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
	FORCEINLINE bool IsNearlyEqual( float32 A, float32 B, float32 Tolerance = Epsilon ) { return NMath::Abs( A - B ) <= Tolerance; }
	FORCEINLINE bool IsNearlyZero( float32 Value, float32 Tolerance = Epsilon ) { return NMath::Abs( Value ) <= Tolerance; }

	//-------------------------------------------------------------------------
	// 冪・平方根・指数・対数（このあとの三角関数・丸めも同じ決まり）
	//   float32 と float64 の版がある。引数と同じ型で答える（<cmath> の std::sqrt などと同じ）。
	//   整数を渡すと、float32 版と float64 版のどちらも同じ強さで合うため曖昧になり、コンパイルできない。<cmath> は黙って double にするので、型が静かに変わるのを防げる。float32 / float64 へ明示的に変換して渡す。
	//   二引数の関数は、float32 と float64 を混ぜると float64 で答える（<cmath> と同じ）。
	//-------------------------------------------------------------------------
	FORCEINLINE float32 Sqrt( float32 Value ) { return std::sqrt( Value ); }
	FORCEINLINE float64 Sqrt( float64 Value ) { return std::sqrt( Value ); }
	FORCEINLINE float32 InvSqrt( float32 Value ) { return 1.0f / std::sqrt( Value ); }
	FORCEINLINE float32 Pow( float32 Base, float32 Exp ) { return std::pow( Base, Exp ); }
	FORCEINLINE float64 Pow( float64 Base, float64 Exp ) { return std::pow( Base, Exp ); }
	FORCEINLINE float64 Pow( float32 Base, float64 Exp ) { return std::pow( static_cast<float64>( Base ), Exp ); }
	FORCEINLINE float64 Pow( float64 Base, float32 Exp ) { return std::pow( Base, static_cast<float64>( Exp ) ); }
	FORCEINLINE float32 Exp( float32 Value ) { return std::exp( Value ); }
	FORCEINLINE float64 Exp( float64 Value ) { return std::exp( Value ); }
	FORCEINLINE float32 Log( float32 Value ) { return std::log( Value ); }
	FORCEINLINE float64 Log( float64 Value ) { return std::log( Value ); }
	FORCEINLINE float32 Log10( float32 Value ) { return std::log10( Value ); }
	FORCEINLINE float64 Log10( float64 Value ) { return std::log10( Value ); }

	//-------------------------------------------------------------------------
	// 三角関数（ラジアン）
	//-------------------------------------------------------------------------
	FORCEINLINE float32 Sin( float32 Value ) { return std::sin( Value ); }
	FORCEINLINE float64 Sin( float64 Value ) { return std::sin( Value ); }
	FORCEINLINE float32 Cos( float32 Value ) { return std::cos( Value ); }
	FORCEINLINE float64 Cos( float64 Value ) { return std::cos( Value ); }
	FORCEINLINE float32 Tan( float32 Value ) { return std::tan( Value ); }
	FORCEINLINE float64 Tan( float64 Value ) { return std::tan( Value ); }
	// 注意: Asin / Acos は入力を [-1, 1] に切り詰める。std::asin / std::acos は範囲外で NaN を返すので、同じ動きではない。
	FORCEINLINE float32 Asin( float32 Value ) { return std::asin( Clamp( Value, -1.0f, 1.0f ) ); }
	FORCEINLINE float32 Acos( float32 Value ) { return std::acos( Clamp( Value, -1.0f, 1.0f ) ); }
	FORCEINLINE float32 Atan( float32 Value ) { return std::atan( Value ); }
	FORCEINLINE float64 Atan( float64 Value ) { return std::atan( Value ); }
	FORCEINLINE float32 Atan2( float32 Y, float32 X ) { return std::atan2( Y, X ); }
	FORCEINLINE float64 Atan2( float64 Y, float64 X ) { return std::atan2( Y, X ); }
	FORCEINLINE float64 Atan2( float32 Y, float64 X ) { return std::atan2( static_cast<float64>( Y ), X ); }
	FORCEINLINE float64 Atan2( float64 Y, float32 X ) { return std::atan2( Y, static_cast<float64>( X ) ); }

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
	// Floor / Ceil は Core/Types/Numeric.h
	FORCEINLINE float32 Round( float32 Value ) { return std::round( Value ); }
	FORCEINLINE float64 Round( float64 Value ) { return std::round( Value ); }
	// 四捨五入して整数にする（0.5 は 0 から遠い方へ。std::lround と同じ。範囲を超えた値・NaN の結果は決まっていない）
	FORCEINLINE int32 RoundToInt( float32 Value ) { return static_cast<int32>( std::lround( Value ) ); }
	FORCEINLINE int32 RoundToInt( float64 Value ) { return static_cast<int32>( std::lround( Value ) ); }
	FORCEINLINE float32 Frac( float32 Value ) { return Value - Floor( Value ); }
	FORCEINLINE float32 Fmod( float32 X, float32 Y ) { return std::fmod( X, Y ); }
	FORCEINLINE float64 Fmod( float64 X, float64 Y ) { return std::fmod( X, Y ); }
	FORCEINLINE float64 Fmod( float32 X, float64 Y ) { return std::fmod( static_cast<float64>( X ), Y ); }
	FORCEINLINE float64 Fmod( float64 X, float32 Y ) { return std::fmod( X, static_cast<float64>( Y ) ); }

	//-------------------------------------------------------------------------
	// その他（型が混ざるときは float64 で答える。<cmath> と同じ）
	//-------------------------------------------------------------------------
	// X から Target の向きに、表せる隣の値（Target が X と同じなら Target）。
	FORCEINLINE float32 NextAfter( float32 X, float32 Target ) { return std::nextafter( X, Target ); }
	FORCEINLINE float64 NextAfter( float64 X, float64 Target ) { return std::nextafter( X, Target ); }
	FORCEINLINE float64 NextAfter( float32 X, float64 Target ) { return std::nextafter( static_cast<float64>( X ), Target ); }
	FORCEINLINE float64 NextAfter( float64 X, float32 Target ) { return std::nextafter( X, static_cast<float64>( Target ) ); }
	// 直角三角形の斜辺の長さ（sqrt( X * X + Y * Y ) を、途中でオーバーフロー・アンダーフローさせずに求める）。
	FORCEINLINE float32 Hypot( float32 X, float32 Y ) { return std::hypot( X, Y ); }
	FORCEINLINE float64 Hypot( float64 X, float64 Y ) { return std::hypot( X, Y ); }
	FORCEINLINE float64 Hypot( float32 X, float64 Y ) { return std::hypot( static_cast<float64>( X ), Y ); }
	FORCEINLINE float64 Hypot( float64 X, float32 Y ) { return std::hypot( X, static_cast<float64>( Y ) ); }
	// 立方根（負の数も扱える）。
	FORCEINLINE float32 Cbrt( float32 Value ) { return std::cbrt( Value ); }
	FORCEINLINE float64 Cbrt( float64 Value ) { return std::cbrt( Value ); }
}
