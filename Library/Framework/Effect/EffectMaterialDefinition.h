// SPDX-License-Identifier: MIT
#ifndef FRAMEWORK_EFFECT_EFFECTMATERIALDEFINITION_H
#define FRAMEWORK_EFFECT_EFFECTMATERIALDEFINITION_H

#include "Library/Core/Collection/Array.h"
#include "Library/Core/Collection/String.h"
#include "Library/Framework/Effect/EffectScalarCurve.h"
#include "Library/Framework/Effect/EffectUVDistortionDefinition.h"
#include "Library/Json/JsonObject.h"
#include "Library/Math/Color.h"
#include "Library/Math/Vector2.h"

// 粒子の四角形・帯・雷で共有する材質と時間変化を保存する定義。
struct FEffectMaterialDefinition
{
	// 材質の効果を有効にするか。
	bool bEnabled = false;
	// マスクに沿った消失境界を有効にするか。
	bool bDissolve = false;
	// 消失マスク画像。空なら描画側の組み込みノイズを使う。
	FString MaskTexture;
	// 画像の UV を横・縦に何倍するか。
	FVector2 UVScale = FVector2( 1.0f, 1.0f );
	// 粒子の経過秒数あたりに進める UV。
	FVector2 UVScroll = FVector2( 0.0f, 0.0f );
	// マスク模様の大きさ。
	float32 MaskScale = 1.0f;
	// 粒子の寿命に沿って消失位置を変える曲線。
	FEffectScalarCurve Dissolve;
	// 粒子の寿命に沿って自己発光を変える曲線。
	FEffectScalarCurve Emission;
	// 消失境界の幅をマスク値に対する割合で指定する。
	float32 EdgeWidth = 0.08f;
	// 消失境界へ重ねる色。
	FColor32 EdgeColor = FColor32( 255, 128, 32, 255 );
	// 消失境界の発光倍率。
	float32 EdgeEmission = 2.0f;
	// 画像のUVを別画像で動かす歪み設定。
	FEffectUVDistortionDefinition Distortion;

	// 材質の全項目を、精度を落とさず JSON にする。
	NlohmannJson ToJson() const;
	// オブジェクトを読む。不正な値は丸めず Validate へ残す。
	static bool FromJson( const NlohmannJson& Json, FEffectMaterialDefinition& Out, TArray<FString>* Warnings = nullptr );
	// 有効状態に関係なく、材質の全保存値と曲線を検証する。
	TArray<FString> Validate() const;
	// 全項目から、変更を判定する識別値を求める。
	uint64 ComputeHash() const;
};

#endif
