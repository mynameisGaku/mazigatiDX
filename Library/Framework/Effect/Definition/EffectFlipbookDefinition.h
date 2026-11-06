// SPDX-License-Identifier: MIT
#ifndef FRAMEWORK_EFFECT_EFFECTFLIPBOOKDEFINITION_H
#define FRAMEWORK_EFFECT_EFFECTFLIPBOOKDEFINITION_H

#include <cstdint>

#include "Library/Core/Collection/String.h"
#include "Library/Framework/Animation/AnimationTimeline.h"
#include "Library/Json/JsonObject.h"

// 粒子の四角形へ適用するスプライトシートと再生時計を保存する値。
struct FEffectFlipbookDefinition
{
	// 粒子ごとのコマ切替を有効にするか。
	bool bEnabled = false;
	// 再生するスプライトシートのアセット相対パス。
	FString Sheet;
	// 空ならシート内で最初に定義されたクリップを使う。
	FString Clip;
	// 寿命割合か、粒子の経過秒数を再生時計に使うか。
	enum class ETimeMode : uint8
	{
		// 寿命割合でclip全体を一度再生する。
		Lifetime,
		// 経過秒数とSpeedでclipのloop方式に従う。
		Seconds,
	};
	// 各粒子のコマ位置を決める時計。
	ETimeMode TimeMode = ETimeMode::Lifetime;
	// 秒時計での再生速度。寿命時計では保存値のみ保持する。
	float32 Speed = 1.0f;

private:
	// 読めなかった型を JSON 往復後も検証エラーとして残すビット集合。
	uint8 InvalidFieldTypes = 0;

public:
	// 既定値なら true を返し、旧 effect JSON/hash の形を保つ。
	bool IsDefault() const;
	// 粒子ごとの評価前に使う、配列生成なしの軽い設定検証。
	bool IsValidSettings() const;
	// 不正状態も失わず、保存可能な JSON 値へ変換する。
	NlohmannJson ToJson() const;
	// JSON を読む。不正な値は既定へ直さず Validate で検出できる状態にする。
	static bool FromJson( const NlohmannJson& Json, FEffectFlipbookDefinition& Out, TArray<FString>* Warnings = nullptr );
	// 保存値と選択状態を検証する。
	TArray<FString> Validate() const;
	// 全項目を識別する安定した値を求める。
	uint64 ComputeHash() const;
};

namespace NEffectFlipbook
{
	// 読込済みタイムラインの配列、累積時間、コマ参照を検証する。
	bool IsValidTimeline( const FAnimTimeline& Timeline );
	// 検証済みtimelineのstep番号を返す。描画コマはStepSource[step]で得る。不正時はINDEX_NONE。
	int32 EvaluateStep( const FEffectFlipbookDefinition& Definition, const FAnimTimeline& Timeline, float64 AgeSeconds, float32 NormalizedAge );
}

#endif
