// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Collection/Array.h"
#include "Library/Core/Collection/String.h"
#include "Library/Framework/AI/AIIssue.h"
#include "Library/Framework/AI/Blackboard/BlackboardValue.h"
#include "Library/Json/JsonObject.h"

//=============================================================================
// FBlackboardKeyDefinition / FBlackboardDefinition
//   ブラックボード（AI が参照する値の置き場）の定義。不変のデータなので、同じ定義を使う全インスタンスで共有してよい。
//   JSON（.gxblackboard）との行き来を持つ。
//
//   {
//     "Version": 1,
//     "Keys": [
//       { "Name": "Target", "Type": "Object" },
//       { "Name": "DistanceToTarget", "Type": "Float", "Default": 999.0 },
//       { "Name": "AttackReady", "Type": "Bool", "Default": true },
//       { "Name": "JumpPressed", "Type": "Bool", "Trigger": true },
//       { "Name": "Mood", "Type": "Enum", "Values": [ "Calm", "Alert" ], "Default": "Calm" }
//     ]
//   }
//
//   - Trigger の Bool は、「押した瞬間」のように 1 フレームだけ真になり、CBlackboard::ClearTriggers() で自動的に偽へ戻る
//   - 重複した名前・名前の無いキー・不明な型のキーは、読み込みのときに警告して捨てる
//=============================================================================
struct FBlackboardKeyDefinition
{
	FString Name;
	EBlackboardKeyType Type = EBlackboardKeyType::Bool;
	FBlackboardValue Default;
	// Enum の選択肢（Type が Enum のときだけ）
	TArray<FString> EnumValues;
	// Bool の、1 フレームだけ真になるキーか
	bool bTrigger = false;
};

struct FBlackboardDefinition
{
	TArray<FBlackboardKeyDefinition> Keys;

	// 名前のキーの番号。無ければ -1
	int32 FindKeyIndex( const FString& Name ) const;

	NlohmannJson ToJson() const;
	// JSON から読む。形が違えば false。問題（捨てたキーなど）は Issues に足す（nullptr なら足さない）
	static bool FromJson( const NlohmannJson& Json, FBlackboardDefinition& OutDefinition, TArray<FAIIssue>* Issues = nullptr );

	// 値を JSON にする / JSON から値を読む（型と、Enum の選択肢で決まる）。読めなければ false
	static NlohmannJson ValueToJson( const FBlackboardKeyDefinition& Key, const FBlackboardValue& Value );
	static bool ValueFromJson( const FBlackboardKeyDefinition& Key, const NlohmannJson& Json, FBlackboardValue& OutValue );
};
