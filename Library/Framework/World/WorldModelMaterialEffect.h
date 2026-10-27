// SPDX-License-Identifier: MIT
#ifndef FRAMEWORK_WORLD_WORLDMODELMATERIALEFFECT_H
#define FRAMEWORK_WORLD_WORLDMODELMATERIALEFFECT_H

#include "Library/Core/Collection/Array.h"
#include "Library/Core/Collection/String.h"
#include "Library/Framework/Effect/MeshMaterialEffectDefinition.h"
#include "Library/Json/JsonObject.h"

// 一つのモデル材質に保存する効果と両面描画設定。
struct FWorldModelMaterialEffect
{
	// 適用先の材質番号。負数は検証で拒否する。
	int32 MaterialIndex = 0;
	// 寿命・合成・模様を含む材質効果。
	FMeshMaterialEffectDefinition Effect;
	// 裏面を描くかどうか。
	bool bTwoSided = false;

	// 保存値をJSONへ変換し、不正rootはnullで保持する。
	NlohmannJson ToJson() const;
	// JSONを読み、型違いをmarkerとして保持する。
	static bool FromJson( const NlohmannJson& Json, FWorldModelMaterialEffect& Out, TArray<FString>* Warnings = nullptr );
	// 材質番号と内包効果の問題を返す。
	TArray<FString> Validate() const;
	// 設定がすべて有効ならtrueを返す。
	bool IsValidSettings() const;
	// 保存値と不正markerから識別hashを求める。
	uint64 ComputeHash() const;
	// hash衝突に依存せず保存値を構造比較する。
	bool IsSameAs( const FWorldModelMaterialEffect& Other ) const;

private:
	// object以外のrootだったことを再保存後も残す印。
	bool bInvalidRoot = false;
	// 材質番号、効果、両面設定の型違いを保持するbit。
	uint8 InvalidFieldTypes = 0;
};

#endif
