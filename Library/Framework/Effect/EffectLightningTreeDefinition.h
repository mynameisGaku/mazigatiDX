// SPDX-License-Identifier: MIT
#ifndef FRAMEWORK_EFFECT_EFFECTLIGHTNINGTREEDEFINITION_H
#define FRAMEWORK_EFFECT_EFFECTLIGHTNINGTREEDEFINITION_H

#include "Library/Core/Collection/Array.h"
#include "Library/Core/Collection/String.h"
#include "Library/Json/JsonObject.h"

// 枝分かれする雷の段数、角度、成長と明滅の設定を保存する値型。
struct FEffectLightningTreeDefinition
{
	// 主幹から何段目の枝まで作るか。
	int32 Depth = 2;
	// 主幹の枝からさらに分ける本数。
	int32 ChildBranches = 2;
	// 親の進行方向から枝を開く角度。
	float32 BranchAngleDegrees = 35.0f;
	// 主幹全体が伸びる秒数。0なら一度に完成する。
	float32 GrowthSeconds = 0.08f;
	// 枝の接続から伸び始めるまでの秒数。
	float32 BranchDelaySeconds = 0.005f;
	// 完成後に光る回数。
	int32 StrokeCount = 3;
	// 光る開始時刻の間隔。
	float32 StrokeInterval = 0.06f;
	// 一回の光が続く時間。
	float32 StrokeSeconds = 0.018f;

	// 既定値かを調べ、Pathの旧hashとJSONを保つ判定に使う。
	bool IsDefault() const;
	// 全項目をfloat精度のままJSONへ書き出す。
	NlohmannJson ToJson() const;
	// JSONから読み、不正な型や値はValidateで検出できる値として残す。
	static bool FromJson( const NlohmannJson& Json, FEffectLightningTreeDefinition& Out, TArray<FString>* Warnings = nullptr );
	// 全ての数値範囲を確認し、保存できない項目を返す。
	TArray<FString> Validate() const;
	// 全フィールドを順番に混ぜて変更識別値を返す。
	uint64 ComputeHash() const;
};

#endif
