// SPDX-License-Identifier: NOASSERTION
#pragma once
#include "Library/Core/Collection/Array.h"
#include "Library/Json/JsonObject.h"
#include "Library/Math/Transform.h"
#include "Library/Math/Vector3.h"

// 粒子へ加えるワールド空間の加速度場を保存し、値を検証・評価する。
struct FEffectForceFieldDefinition
{
	// 加速度場の形。Radialは中心から外向き、Strengthの符号で引力にする。
	enum class EKind : uint8
	{
		// 位置によらない一様加速度。
		Uniform,
		// 中心から距離に応じて弱まる放射加速度。
		Radial,
		// 中心と回転軸の周りで距離に応じて弱まる接線方向の渦加速度。
		Vortex,
		// 中心と半径内で空間周波数と時間スクロールにより変化する決定論的な乱流加速度。
		Turbulence,
	};

	// 1エミッターで計算する力場の上限。
	static constexpr int32 MaxFields = 16;
	// 距離・加速度・周波数の各保存値の上限。
	static constexpr float32 MaxMagnitude = 1.0e6f;

	// 無効時は粒子へ加速度を加えない。
	bool bEnabled = true;
	// 粒子へ適用する加速度場の種類。
	EKind Kind = EKind::Uniform;
	// エミッターの移動・回転に追従するか（falseはワールド固定）。
	bool bFollowEmitter = false;
	// Uniformで使う加速度ベクトル。
	FVector3 Acceleration = FVector3::Zero;
	// Radial/Vortex/Turbulenceで使う中心座標。
	FVector3 Position = FVector3::Zero;
	// Vortexで使う回転軸の向き。
	FVector3 Axis = FVector3::Up;
	// Radial/Vortex/Turbulenceの作用半径。境界上では加速度を加えない。
	float32 Radius = 1.0f;
	// Radial/Vortex/Turbulenceの基準加速度。
	float32 Strength = 0.0f;
	// Turbulenceで使う空間周波数（rad/m）。
	float32 Frequency = 1.0f;
	// Turbulenceで使う時間スクロール速度（rad/s）。
	float32 ScrollSpeed = 1.0f;

	// 全保存値の有限性と上限を調べる。
	bool IsValidSettings() const;
	// 有効な場をワールド固定条件でWorld位置にて評価し、不正値や無効場ではゼロを返す。
	FVector3 EvaluateAcceleration( const FVector3& WorldPosition ) const;
	// 有効な場をエミッター姿勢と経過時間込みで評価し、不正値や無効場ではゼロを返す。
	FVector3 EvaluateAcceleration( const FVector3& WorldPosition, const FTransform& EffectTransform, float32 Time = 0.0f ) const;
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
	uint16 InvalidFieldTypes = 0;
};

// 加速度場の種類をJSONへ保存する名前に変換する。
const char* ToString( FEffectForceFieldDefinition::EKind Kind );
