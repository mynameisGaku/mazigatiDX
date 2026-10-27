// SPDX-License-Identifier: NOASSERTION
#pragma once
#include "Library/Core/Interfaces/Physics/PhysicsCharacterTypes.h"
#include "Library/Core/Types/Numeric.h"
#include "Library/Math/MathConstants.h"

//=============================================================================
// NCharacterRules（Physics の内部用）
//   キャラクターの設定が物理エンジン（Jolt）の前提を満たすかを確かめる。
//   Jolt は前提を破ると assert で止まる（カプセルの円柱部分が 0 など）ので、作るときも後から変えるときも、必ずここを通す。
//=============================================================================
namespace NCharacterRules
{
	// カプセルの円柱部分の高さの最小（これより短いと Jolt のカプセルが作れない）
	inline constexpr float32 MinCylinderHeight = 0.002f;

	// 有限で 0 以上の値（段差の高さ・吸いつく距離・体重・押す力）
	inline bool IsFiniteNonNegative( float32 Value )
	{
		return NMath::IsFinite( Value ) && Value >= 0.0f;
	}

	// 登れる坂の角度（0 より大きく PI/2 より小さい）
	inline bool IsValidSlopeAngle( float32 Angle )
	{
		return NMath::IsFinite( Angle ) && Angle > 0.0f && Angle < NMath::HALF_PI;
	}

	// 半径が正で、高さが 半径の 2 倍 + 円柱の最小 以上
	inline bool IsValidShape( float32 Radius, float32 Height )
	{
		return NMath::IsFinite( Radius ) && Radius > 0.0f && NMath::IsFinite( Height ) && Height - 2.0f * Radius >= MinCylinderHeight;
	}

	// 設定が使えるか。使えなければ false にして、理由を OutReason へ入れる
	inline bool Validate( const FCharacterSettings& Settings, const char*& OutReason )
	{
		if ( !IsValidShape( Settings.Radius, Settings.Height ) )
		{
			OutReason = "Radius must be > 0 and Height must be larger than 2 * Radius";
			return false;
		}
		if ( !IsValidSlopeAngle( Settings.MaxSlopeAngle ) )
		{
			OutReason = "MaxSlopeAngle must be within (0, PI/2)";
			return false;
		}
		if ( !IsFiniteNonNegative( Settings.StepHeight ) || !IsFiniteNonNegative( Settings.StickToFloorDistance ) )
		{
			OutReason = "StepHeight and StickToFloorDistance must be finite and >= 0";
			return false;
		}
		if ( !IsFiniteNonNegative( Settings.Mass ) || !IsFiniteNonNegative( Settings.MaxPushForce ) )
		{
			OutReason = "Mass and MaxPushForce must be finite and >= 0";
			return false;
		}
		if ( !NMath::IsFinite( Settings.GravityFactor ) )
		{
			OutReason = "GravityFactor must be finite";
			return false;
		}
		if ( !NCollisionLayer::IsValid( Settings.CollisionLayer ) )
		{
			OutReason = "CollisionLayer is out of range";
			return false;
		}
		return true;
	}
}
