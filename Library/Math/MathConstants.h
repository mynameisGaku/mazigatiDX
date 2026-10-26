#pragma once
#include <limits>

#include "Library/Core/Types/Numeric.h"
#include "Library/Core/Types/TypeDefinition.h"

//=============================================================================
// 数学定数
//=============================================================================
namespace NMath
{
	inline constexpr float32 PI          = 3.14159265358979323846f;
	inline constexpr float32 TWO_PI      = PI * 2.0f;
	inline constexpr float32 HALF_PI     = PI * 0.5f;
	inline constexpr float32 INV_PI      = 1.0f / PI;

	inline constexpr float32 DEG2RAD     = PI / 180.0f;
	inline constexpr float32 RAD2DEG     = 180.0f / PI;

	inline constexpr float32 Epsilon     = 1.0e-6f;    // 比較用の許容誤差
	inline constexpr float32 SmallNumber = 1.0e-8f;    // ゼロ除算回避用
	inline constexpr float32 Infinity    = TNumericLimits<float32>::Infinity();
	inline constexpr float32 MaxFloat    = TNumericLimits<float32>::Max();
}
