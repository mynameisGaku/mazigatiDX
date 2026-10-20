// SPDX-License-Identifier: NOASSERTION
#ifndef FRAMEWORK_EFFECT_EFFECTAPPEARANCEDEFINITION_H
#define FRAMEWORK_EFFECT_EFFECTAPPEARANCEDEFINITION_H

#include "Library/Core/Collection/Array.h"
#include "Library/Core/Collection/String.h"
#include "Library/Framework/Effect/EffectColorCurve.h"
#include "Library/Framework/Effect/EffectScalarCurve.h"
#include "Library/Json/JsonObject.h"
#include "Library/Math/Color.h"

// 粒子の寿命に沿って大きさと RGBA を変える表示倍率。
struct FEffectAppearanceDefinition
{
	// 粒子の寿命に沿って大きさを変える倍率曲線。空なら 1 倍。
	FEffectScalarCurve Size;
	// 粒子の寿命に沿って RGBA を変える倍率曲線。空なら白。
	FEffectColorCurve Color;

	// 寿命位置の大きさ倍率を安全な 0..32 に収めて返す。
	float32 EvaluateSizeScale( float32 Time ) const;
	// 寿命位置の RGBA 倍率を安全な 0..1 に収めて返す。
	FColor EvaluateColorScale( float32 Time ) const;
	// 全曲線を、精度を落とさず JSON にする。
	NlohmannJson ToJson() const;
	// オブジェクトを読む。不正値は丸めず Validate へ残す。
	static bool FromJson( const NlohmannJson& Json, FEffectAppearanceDefinition& Out, TArray<FString>* Warnings = nullptr );
	// 両曲線と各曲線の表示倍率の範囲を調べる。
	TArray<FString> Validate() const;
	// 両曲線から、変更を判定する識別値を求める。
	uint64 ComputeHash() const;
};

#endif
