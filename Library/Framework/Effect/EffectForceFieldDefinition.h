// SPDX-License-Identifier: NOASSERTION
#pragma once
#include "Library/Core/Collection/Array.h"
#include "Library/Json/JsonObject.h"
#include "Library/Math/Vector3.h"

// 粒子へ加えるワールド空間の加速度場を保存し、値を検証・評価する。
struct FEffectForceFieldDefinition
{
	// 加速度場の形。Radialは中心から外向き、Strengthの符号で引力にする。
	enum class EKind : uint8
	{
		Uniform,   // 位置によらないWorld加速度。
		Radial,    // 中心から距離に応じて弱まる加速度。
	};

	static constexpr int32 MaxFields = 16;               // 1エミッターで計算する力場の上限。
	static constexpr float32 MaxMagnitude = 1.0e6f;      // 距離・加速度の各保存値の上限。

	// 無効時は粒子へ加速度を加えない。
	bool bEnabled = true;
	// 粒子へ適用する加速度場の種類。
	EKind Kind = EKind::Uniform;
	// Uniformで使うワールド空間の加速度。
	FVector3 Acceleration = FVector3::Zero;
	// Radialで使うワールド空間の中心。
	FVector3 Position = FVector3::Zero;
	// Radialの作用半径。境界上では加速度を加えない。
	float32 Radius = 1.0f;
	// Radialの中心からの加速度。正は外向き、負は引力。
	float32 Strength = 0.0f;

	// 全保存値の有限性と上限を調べる。
	bool IsValidSettings() const;
	// 有効な場をWorld位置で評価し、不正値や無効場ではゼロを返す。
	FVector3 EvaluateAcceleration( const FVector3& WorldPosition ) const;
	// 全設定をJSONへ保存する。
	NlohmannJson ToJson() const;
	// JSONを読み、不正値は検証で見つかる状態に保持する。
	static bool FromJson( const NlohmannJson& Json, FEffectForceFieldDefinition& Out, TArray<FString>* Warnings = nullptr );
	// 保存値の問題を列挙する。
	TArray<FString> Validate() const;
	// 全保存値と入力型の印を識別する。
	uint64 ComputeHash() const;

private:
	// 読み込めなかった項目を保存往復後も検出する印。
	uint8 InvalidFieldTypes = 0;
};

// 加速度場の種類をJSONへ保存する名前に変換する。
const char* ToString( FEffectForceFieldDefinition::EKind Kind );
