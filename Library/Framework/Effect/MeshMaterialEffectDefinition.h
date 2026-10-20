// SPDX-License-Identifier: NOASSERTION
#ifndef FRAMEWORK_EFFECT_MESHMATERIALEFFECTDEFINITION_H
#define FRAMEWORK_EFFECT_MESHMATERIALEFFECTDEFINITION_H

#include "Library/Core/Collection/Array.h"
#include "Library/Core/Collection/String.h"
#include "Library/Core/Interfaces/Render/DrawCommand.h"
#include "Library/Framework/Effect/EffectMaterialDefinition.h"
#include "Library/Json/JsonObject.h"

// 編集メッシュの一つの材質に付ける、寿命・合成付きエフェクト設定。
struct FMeshMaterialEffectDefinition
{
	// 粒子・帯・ノイズ材質の既存設定。有効化はこのbEnabledだけで決める。
	FEffectMaterialDefinition Material;
	// 材質が一巡する時間。0より大きく3600秒以下。
	float32 Duration = 2.0f;
	// 終端後に同じ時間変化を繰り返すか。
	bool bLoop = true;
	// 材質へ掛ける不透明度。0から1。
	float32 Opacity = 1.0f;
	// 材質の合成方法。AlphaまたはAddだけを保存できる。
	EDrawBlend Blend = EDrawBlend::Alpha;

	// 全項目が構造上の既定値か調べる。
	bool IsDefault() const;
	// 内包材質、Duration、Opacity、Blendを検証し、問題があればfalseを返す。
	bool IsValidSettings() const;
	// 全fieldを精度を落とさずJSONへ書き、不正rootならnullを返す。
	NlohmannJson ToJson() const;
	// Jsonを読み、型違い・未知enum・不正rootを修復せずValidateへ残す。
	static bool FromJson( const NlohmannJson& Json, FMeshMaterialEffectDefinition& Out, TArray<FString>* Warnings = nullptr );
	// 内包材質と全保存fieldの問題一覧を返す。
	TArray<FString> Validate() const;
	// 全設定と不正状態から識別hashを求める。既定meshへの混合有無は呼び出し側が決める。
	uint64 ComputeHash() const;
	// Otherと全保存値を構造比較し、hash衝突に依存しない。
	bool IsSameAs( const FMeshMaterialEffectDefinition& Other ) const;

private:
	// JSON rootがobject以外だったことを再保存後も残す印。
	bool bInvalidRoot = false;
	// 型違いを既定値へ黙って直さず保存検査へ残すbit。
	uint8 InvalidFieldTypes = 0;
};

#endif
