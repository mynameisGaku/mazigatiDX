// SPDX-License-Identifier: NOASSERTION
#pragma once

#include <cmath>   // 中身は std::floor / std::ceil。標準ライブラリに触れるのは、ここと MathUtils.h だけ

#include "Library/Core/Types/CompilerMacros.h"
#include "Library/Core/Types/TypeDefinition.h"

//=============================================================================
// 数値の基本（標準ライブラリに依存しない）
//   <algorithm> の std::min / std::max / std::clamp、<cmath> の std::fabs / std::abs / std::isfinite、
//   <limits> の std::numeric_limits の代わり。Core でも使えるよう、DxLib や Math の型に依存しない。
//   namespace は Math の NMath と同じ（NMath::Min など。Math.h からも同じ名前で使える）。
//
//   const float32 Clamped = NMath::Clamp( Value, 0.0f, 1.0f );
//   if ( !NMath::IsFinite( Position.x ) ) { ... }                  // NaN・無限大を弾く
//   const int32 Largest = TNumericLimits<int32>::Max();
//=============================================================================
namespace NMath
{
	//-------------------------------------------------------------------------
	// 基本（同じ型どうしを比べる。<algorithm> と同じく、型が違うと呼べない）
	//-------------------------------------------------------------------------
	template<typename T>
	FORCEINLINE constexpr T Abs( T Value ) { return Value < T( 0 ) ? -Value : Value; }

	// 浮動小数点の Abs は符号ビットを落とす（std::fabs と同じ。-0.0 は +0.0 になり、NaN は NaN のまま）
	FORCEINLINE constexpr float32 Abs( float32 Value ) { return __builtin_bit_cast( float32, __builtin_bit_cast( uint32, Value ) & 0x7FFFFFFFu ); }
	FORCEINLINE constexpr float64 Abs( float64 Value ) { return __builtin_bit_cast( float64, __builtin_bit_cast( uint64, Value ) & 0x7FFFFFFFFFFFFFFFull ); }

	template<typename T>
	FORCEINLINE constexpr T Min( T A, T B ) { return B < A ? B : A; }

	template<typename T>
	FORCEINLINE constexpr T Max( T A, T B ) { return A < B ? B : A; }

	// 3 つのうちの最小・最大（std::min / std::max の初期化子リスト版の代わり）
	template<typename T>
	FORCEINLINE constexpr T Min( T A, T B, T C ) { return Min( Min( A, B ), C ); }

	template<typename T>
	FORCEINLINE constexpr T Max( T A, T B, T C ) { return Max( Max( A, B ), C ); }

	template<typename T>
	FORCEINLINE constexpr T Clamp( T Value, T Low, T High ) { return Value < Low ? Low : ( High < Value ? High : Value ); }

	template<typename T>
	FORCEINLINE constexpr T Sign( T Value ) { return Value > T( 0 ) ? T( 1 ) : ( Value < T( 0 ) ? T( -1 ) : T( 0 ) ); }

	template<typename T>
	FORCEINLINE constexpr T Square( T Value ) { return Value * Value; }

	//-------------------------------------------------------------------------
	// NaN・無限大の判定（ビットの並びで調べる。指数がすべて 1 なら、NaN か無限大）
	//-------------------------------------------------------------------------
	// 32bit 浮動小数点の指数部分のビット。
	inline constexpr uint32 FloatExponentMask32 = 0x7F800000u;
	// 32bit 浮動小数点の仮数部分のビット。
	inline constexpr uint32 FloatMantissaMask32 = 0x007FFFFFu;
	// 64bit 浮動小数点の指数部分のビット。
	inline constexpr uint64 FloatExponentMask64 = 0x7FF0000000000000ull;
	// 64bit 浮動小数点の仮数部分のビット。
	inline constexpr uint64 FloatMantissaMask64 = 0x000FFFFFFFFFFFFFull;

	// 有限の数（NaN でも無限大でもない）か。
	FORCEINLINE constexpr bool IsFinite( float32 Value ) { return ( __builtin_bit_cast( uint32, Value ) & FloatExponentMask32 ) != FloatExponentMask32; }
	// 有限の数（NaN でも無限大でもない）か。
	FORCEINLINE constexpr bool IsFinite( float64 Value ) { return ( __builtin_bit_cast( uint64, Value ) & FloatExponentMask64 ) != FloatExponentMask64; }

	// NaN（数ではない値）か。
	FORCEINLINE constexpr bool IsNaN( float32 Value )
	{
		const uint32 Bits = __builtin_bit_cast( uint32, Value );
		return ( Bits & FloatExponentMask32 ) == FloatExponentMask32 && ( Bits & FloatMantissaMask32 ) != 0u;
	}
	// NaN（数ではない値）か。
	FORCEINLINE constexpr bool IsNaN( float64 Value )
	{
		const uint64 Bits = __builtin_bit_cast( uint64, Value );
		return ( Bits & FloatExponentMask64 ) == FloatExponentMask64 && ( Bits & FloatMantissaMask64 ) != 0ull;
	}

	// 正または負の無限大か。
	FORCEINLINE constexpr bool IsInfinite( float32 Value ) { return !IsFinite( Value ) && !IsNaN( Value ); }
	// 正または負の無限大か。
	FORCEINLINE constexpr bool IsInfinite( float64 Value ) { return !IsFinite( Value ) && !IsNaN( Value ); }

	//-------------------------------------------------------------------------
	// 切り捨て・切り上げ（結果は、引数と同じ型の浮動小数点。<cmath> の std::floor / std::ceil と同じ）
	//-------------------------------------------------------------------------
	FORCEINLINE float32 Floor( float32 Value ) { return std::floor( Value ); }
	FORCEINLINE float64 Floor( float64 Value ) { return std::floor( Value ); }
	FORCEINLINE float32 Ceil( float32 Value ) { return std::ceil( Value ); }
	FORCEINLINE float64 Ceil( float64 Value ) { return std::ceil( Value ); }
}

//=============================================================================
// TNumericLimits<T>
//   型ごとの最小・最大など。std::numeric_limits<T> の代わり。
//   Min は整数では最小の値、浮動小数点では 0 より大きい最小の正規化数（std::numeric_limits と同じ）。
//   最小の値（浮動小数点では最も小さい負の値）は Lowest。名前が大文字なので、windows.h の min / max マクロとぶつからない。
//=============================================================================
template<typename T>
struct TNumericLimits;

// 8bit 符号あり整数の限界。
template<>
struct TNumericLimits<int8>
{
	static constexpr int8 Min() { return static_cast<int8>( -128 ); }
	static constexpr int8 Max() { return static_cast<int8>( 127 ); }
	static constexpr int8 Lowest() { return Min(); }
};

// 16bit 符号あり整数の限界。
template<>
struct TNumericLimits<int16>
{
	static constexpr int16 Min() { return static_cast<int16>( -32768 ); }
	static constexpr int16 Max() { return static_cast<int16>( 32767 ); }
	static constexpr int16 Lowest() { return Min(); }
};

// 32bit 符号あり整数の限界。
template<>
struct TNumericLimits<int32>
{
	static constexpr int32 Min() { return static_cast<int32>( -2147483647 - 1 ); }
	static constexpr int32 Max() { return static_cast<int32>( 2147483647 ); }
	static constexpr int32 Lowest() { return Min(); }
};

// 64bit 符号あり整数の限界。
template<>
struct TNumericLimits<int64>
{
	static constexpr int64 Min() { return static_cast<int64>( -9223372036854775807ll - 1 ); }
	static constexpr int64 Max() { return static_cast<int64>( 9223372036854775807ll ); }
	static constexpr int64 Lowest() { return Min(); }
};

// 8bit 符号なし整数の限界。
template<>
struct TNumericLimits<uint8>
{
	static constexpr uint8 Min() { return 0; }
	static constexpr uint8 Max() { return static_cast<uint8>( 255u ); }
	static constexpr uint8 Lowest() { return Min(); }
};

// 16bit 符号なし整数の限界。
template<>
struct TNumericLimits<uint16>
{
	static constexpr uint16 Min() { return 0; }
	static constexpr uint16 Max() { return static_cast<uint16>( 65535u ); }
	static constexpr uint16 Lowest() { return Min(); }
};

// 32bit 符号なし整数の限界。
template<>
struct TNumericLimits<uint32>
{
	static constexpr uint32 Min() { return 0u; }
	static constexpr uint32 Max() { return 4294967295u; }
	static constexpr uint32 Lowest() { return Min(); }
};

// 64bit 符号なし整数の限界。
template<>
struct TNumericLimits<uint64>
{
	static constexpr uint64 Min() { return 0ull; }
	static constexpr uint64 Max() { return 18446744073709551615ull; }
	static constexpr uint64 Lowest() { return Min(); }
};

// 32bit 浮動小数点の限界。Epsilon は 1.0 と、その次に大きい値との差。
template<>
struct TNumericLimits<float32>
{
	static constexpr float32 Min() { return 1.17549435e-38f; }
	static constexpr float32 Max() { return 3.40282347e+38f; }
	static constexpr float32 Lowest() { return -Max(); }
	static constexpr float32 Epsilon() { return 1.19209290e-07f; }
	static constexpr float32 Infinity() { return __builtin_bit_cast( float32, 0x7F800000u ); }
	static constexpr float32 QuietNaN() { return __builtin_bit_cast( float32, 0x7FC00000u ); }
};

// 64bit 浮動小数点の限界。Epsilon は 1.0 と、その次に大きい値との差。
template<>
struct TNumericLimits<float64>
{
	static constexpr float64 Min() { return 2.2250738585072014e-308; }
	static constexpr float64 Max() { return 1.7976931348623158e+308; }
	static constexpr float64 Lowest() { return -Max(); }
	static constexpr float64 Epsilon() { return 2.2204460492503131e-16; }
	static constexpr float64 Infinity() { return __builtin_bit_cast( float64, 0x7FF0000000000000ull ); }
	static constexpr float64 QuietNaN() { return __builtin_bit_cast( float64, 0x7FF8000000000000ull ); }
};
