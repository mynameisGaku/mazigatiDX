// SPDX-License-Identifier: MIT
#ifndef FRAMEWORK_EFFECT_EFFECTUVDISTORTIONDEFINITION_H
#define FRAMEWORK_EFFECT_EFFECTUVDISTORTIONDEFINITION_H

#include "Library/Core/Collection/Array.h"
#include "Library/Core/Collection/String.h"
#include "Library/Framework/Effect/EffectScalarCurve.h"
#include "Library/Json/JsonObject.h"
#include "Library/Math/Vector2.h"

// 粒子の画像参照位置をずらすための保存可能な設定。
struct FEffectUVDistortionDefinition
{
	// 効果を描画へ適用するか。
	bool bEnabled = false;
	// ゆがみ量を読む画像のアセット相対パス。
	FString Texture;
	// ゆがみ画像の横・縦の繰り返し倍率。
	FVector2 UVScale = FVector2( 1.0f, 1.0f );
	// 粒子の経過秒あたりの画像移動量。
	FVector2 UVScroll = FVector2( 0.0f, 0.0f );
	// 粒子の寿命位置に沿ったUV単位のゆがみ幅。
	FEffectScalarCurve Strength;

	// 全項目が未設定の既定値か調べる。
	bool IsDefault() const;
	// 読み込み型と保存可能な値域を検証する。
	bool IsValidSettings() const;
	// 保存値を浮動小数点の精度を保って JSON にする。
	NlohmannJson ToJson() const;
	// JSON を読み、不正な型も保存前の検証へ残す。
	static bool FromJson( const NlohmannJson& Json, FEffectUVDistortionDefinition& Out, TArray<FString>* Warnings = nullptr );
	// すべての値と曲線の問題を返す。
	TArray<FString> Validate() const;
	// 全項目から変更判定用の識別値を求める。
	uint64 ComputeHash() const;

private:
	// 不正なJSON型を既定値へ直さず再保存するための印。
	uint8 InvalidFieldTypes = 0;
};

#endif
