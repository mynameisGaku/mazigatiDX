// SPDX-License-Identifier: NOASSERTION
#ifndef FRAMEWORK_EFFECT_EFFECTCOLORCURVE_H
#define FRAMEWORK_EFFECT_EFFECTCOLORCURVE_H

#include "Library/Core/Collection/Array.h"
#include "Library/Core/Collection/String.h"
#include "Library/Framework/Effect/EffectScalarCurve.h"
#include "Library/Json/JsonObject.h"
#include "Library/Math/Color.h"

// 粒子の寿命に沿って RGBA 倍率を変える、保存可能な色曲線。
struct FEffectColorCurve
{
	// 寿命位置と、その位置で使う RGBA 倍率。
	struct FKey
	{
		// 粒子の寿命に対する位置。0 から 1 の間。
		float32 Time = 0.0f;
		// 赤・緑・青・アルファの倍率。
		FColor Value = FColor::White;
	};

	// 一つの曲線に保存できるキーの上限。
	static constexpr int32 MaxKeys = 32;
	// キー間で使う既存の補間方法。
	FEffectScalarCurve::EInterpolation Interpolation = FEffectScalarCurve::EInterpolation::Linear;
	// 時刻の昇順で並べた色キー。
	TArray<FKey> Keys;

	// 寿命位置の色倍率を評価する。空・非有限入力なら既定色を返す。
	FColor Evaluate( float32 Time, const FColor& DefaultValue ) const;
	// 補間方法と全キーを、精度を落とさず JSON にする。
	NlohmannJson ToJson() const;
	// オブジェクトを読む。不正値は丸めず Validate へ残す。
	static bool FromJson( const NlohmannJson& Json, FEffectColorCurve& Out, TArray<FString>* Warnings = nullptr );
	// 補間方法、キー数、時刻順、RGBA 値を調べる。
	TArray<FString> Validate() const;
	// 補間方法と全 RGBA キーから、変更を判定する識別値を求める。
	uint64 ComputeHash() const;
};

#endif
