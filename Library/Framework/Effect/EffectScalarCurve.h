// SPDX-License-Identifier: NOASSERTION
#ifndef FRAMEWORK_EFFECT_EFFECTSCALARCURVE_H
#define FRAMEWORK_EFFECT_EFFECTSCALARCURVE_H

#include "Library/Core/Collection/Array.h"
#include "Library/Core/Collection/String.h"
#include "Library/Json/JsonObject.h"

// 粒子の寿命に沿って一つの値を変える、保存可能な曲線。
struct FEffectScalarCurve
{
	// 曲線上で値を指定する位置と、その位置の値。
	struct FKey
	{
		// 粒子の寿命に対する位置。0 から 1 の間。
		float32 Time = 0.0f;
		// この位置で使う値。
		float32 Value = 0.0f;
	};

	// 隣り合うキーの間で値をつなぐ方法。
	enum class EInterpolation : uint8
	{
		Linear, // 両端を直線でつなぐ。
		Step, // 左のキーの値を保つ。
		Smooth, // smoothstep で両端をなめらかにつなぐ。
	};

	// 一つの曲線に保存できるキーの上限。
	static constexpr int32 MaxKeys = 32;
	// キー間で使う補間方法。
	EInterpolation Interpolation = EInterpolation::Linear;
	// 時刻の昇順で並べた曲線のキー。
	TArray<FKey> Keys;

	// 寿命の位置を評価する。空・非有限入力なら既定値を返す。
	float32 Evaluate( float32 Time, float32 DefaultValue ) const;
	// 補間方法と全キーを、精度を落とさず JSON にする。
	NlohmannJson ToJson() const;
	// オブジェクトを読む。不正な数値は丸めず Validate へ残す。
	static bool FromJson( const NlohmannJson& Json, FEffectScalarCurve& Out, TArray<FString>* Warnings = nullptr );
	// 補間方法、キー数、時刻順、有限値を調べる。
	TArray<FString> Validate() const;
	// 補間方法と全キーから、変更を判定する識別値を求める。
	uint64 ComputeHash() const;
};

#endif
