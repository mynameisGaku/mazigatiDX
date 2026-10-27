// SPDX-License-Identifier: MIT
#pragma once
#include "Library/Core/Interfaces/Physics/PhysicsConstraintTypes.h"
#include "Library/Core/Types/Numeric.h"
#include "Library/Math/MathConstants.h"

//=============================================================================
// NConstraintRules（Physics の内部用）
//   関節の設定が物理エンジン（Jolt）の前提を満たすかを確かめ、Jolt に渡せる値へそろえる。
//   Jolt は前提を破ると assert で止まるので、作るときも後から変えるときも、必ずここを通す。
//=============================================================================
namespace NConstraintRules
{
	// 1 ステップで関節がかけた力積（インパルス）を、力（トルク）へ直す。ステップの時間が 0 以下・NaN なら 0
	inline float32 ImpulseToLoad( float32 Impulse, float32 StepDelta )
	{
		if ( !( StepDelta > 0.0f ) ) return 0.0f;
		return Impulse / StepDelta;
	}

	// 壊れるか。力かトルクのどちらかが限界を超えたとき（等しいだけでは壊れない。NaN は超えたことにならない）
	inline bool ShouldBreak( float32 Force, float32 Torque, float32 BreakForce, float32 BreakTorque )
	{
		return Force > BreakForce || Torque > BreakTorque;
	}

	// ばねの設定が使えるか（有限で、どちらも 0 以上）
	inline bool IsValidSpring( const FConstraintSpring& Spring )
	{
		return NMath::IsFinite( Spring.Frequency ) && Spring.Frequency >= 0.0f && NMath::IsFinite( Spring.Damping ) && Spring.Damping >= 0.0f;
	}

	// 0 以上の力・トルク・摩擦。NaN と負は不可。無限大は「上限なし」として MaxFloat にする
	inline bool NormalizeNonNegative( float32 Value, float32& OutValue )
	{
		if ( NMath::IsNaN( Value ) || Value < 0.0f ) return false;
		OutValue = NMath::IsInfinite( Value ) ? NMath::MaxFloat : Value;
		return true;
	}

	// 端の値を、有限の範囲に収める（無限大は MaxFloat）。NaN は不可
	inline bool NormalizeEdge( float32 Value, float32& OutValue )
	{
		if ( NMath::IsNaN( Value ) ) return false;
		OutValue = NMath::Clamp( Value, -NMath::MaxFloat, NMath::MaxFloat );
		return true;
	}

	// 動ける範囲を、Jolt に渡せる値へそろえる。使えなければ false。
	//   Hinge: Min は -PI..0、Max は 0..PI（端を超えたら端にする）
	//   Slider: Min <= 0 <= Max
	//   Distance: 0 以上で Min <= Max。bAllowAuto なら、負の値は「作った瞬間の距離」として -1 のまま通す
	//   Hinge / Slider で Min == Max（0, 0）のとき、ばねが硬いと動けないので不可（Fixed を使う）
	inline bool NormalizeLimits( EConstraintType Type, float32 Min, float32 Max, const FConstraintSpring& Spring, bool bAllowAuto, float32& OutMin, float32& OutMax )
	{
		float32 LimitMin = 0.0f;
		float32 LimitMax = 0.0f;
		if ( !NormalizeEdge( Min, LimitMin ) || !NormalizeEdge( Max, LimitMax ) ) return false;

		switch ( Type )
		{
		case EConstraintType::Hinge:
			if ( LimitMin > 0.0f || LimitMax < 0.0f ) return false;
			LimitMin = NMath::Max( LimitMin, -NMath::PI );
			LimitMax = NMath::Min( LimitMax, NMath::PI );
			break;
		case EConstraintType::Slider:
			if ( LimitMin > 0.0f || LimitMax < 0.0f ) return false;
			break;
		case EConstraintType::Distance:
			if ( LimitMin < 0.0f || LimitMax < 0.0f )
			{
				if ( !bAllowAuto ) return false;
				if ( LimitMin < 0.0f ) LimitMin = -1.0f;
				if ( LimitMax < 0.0f ) LimitMax = -1.0f;
			}
			else if ( LimitMin > LimitMax )
			{
				return false;
			}
			OutMin = LimitMin;
			OutMax = LimitMax;
			return true;
		default:
			return false;
		}

		// 範囲が 1 点に潰れるのに、硬く止めると動けない
		if ( LimitMin == LimitMax && Spring.Frequency <= 0.0f ) return false;

		OutMin = LimitMin;
		OutMax = LimitMax;
		return true;
	}
}
