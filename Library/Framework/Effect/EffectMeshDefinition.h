// SPDX-License-Identifier: MIT
#ifndef FRAMEWORK_EFFECT_EFFECTMESHDEFINITION_H
#define FRAMEWORK_EFFECT_EFFECTMESHDEFINITION_H

#include "Library/Core/Collection/Array.h"
#include "Library/Json/JsonObject.h"
#include "Library/Math/Vector3.h"

// 粒子を固定向きの平面または円環として描く形状設定。
struct FEffectMeshDefinition
{
	// 形状の種類。None は既存の四角形描画を使う。
	enum class EKind : uint8
	{
		// 従来の四角形粒子を維持する。
		None,
		// XY面に置く両面の四角形を使う。
		Plane,
		// XY面に穴のある両面円環を使う。
		Ring,
	};
	// このemitterが使用する形状。
	EKind Kind = EKind::None;
	// 円環の周方向分割数。
	int32 Segments = 48;
	// 外半径に対する内半径の割合。
	float32 InnerRadius = 0.65f;
	// 粒子回転の後に掛ける固定形状のオイラー角。JSONは度。
	FVector3 RotationDegrees = FVector3::Zero;

private:
	// 読み込めなかったJSON型を保存往復後もエラーに残す印。
	uint8 InvalidFieldTypes = 0;

public:
	// 完全な既定値か調べ、旧JSONとhashの互換性を保つ。
	bool IsDefault() const;
	// geometryを作らずに描画前の値域を検査する。
	bool IsValidSettings() const;
	// 不正値の検査状態を保ったままJSONへ書き出す。
	NlohmannJson ToJson() const;
	// JSONを読み、型違いを検証可能な状態で保持する。
	static bool FromJson( const NlohmannJson& Json, FEffectMeshDefinition& Out, TArray<FString>* Warnings = nullptr );
	// 保存を止める形状値の問題を返す。
	TArray<FString> Validate() const;
	// 全値を識別する安定hashを返す。
	uint64 ComputeHash() const;
};

// 保存するmesh種別の表示名を返す。
const char* ToString( FEffectMeshDefinition::EKind Kind );

#endif
