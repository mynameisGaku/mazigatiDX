// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Collection/String.h"
#include "Library/Framework/AI/Blackboard/BlackboardDefinition.h"
#include "Library/Json/JsonObject.h"

//=============================================================================
// ブラックボードの値の比較（ステートマシンの遷移の条件と、ビヘイビアツリーのデコレーターが共通で使う）
//=============================================================================
enum class EAICompareOp : uint8
{
	Equal,
	NotEqual,
	Less,
	LessEqual,
	Greater,
	GreaterEqual,
};

namespace NAI
{
	// "==" "!=" "<" "<=" ">" ">=" との行き来
	const char* ToString( EAICompareOp Op );
	bool TryParseCompareOp( const FString& Text, EAICompareOp& OutOp );

	// 大小の比較ができる型か（Int / Float / Enum の番号）。それ以外は == と != だけ
	bool SupportsOrdering( EBlackboardKeyType Type );

	// 現在の値 Current と期待する値 Expected を、型 Type として Op で比べる
	bool CompareValues( EBlackboardKeyType Type, const FBlackboardValue& Current, EAICompareOp Op, const FBlackboardValue& Expected );

	// JSON の値 Value を、キー Key の型に合わせた比較の値にする。型に合わない値・その型で使えない演算子は false（理由は OutReason）
	bool PrepareCompare( const FBlackboardKeyDefinition& Key, EAICompareOp Op, const NlohmannJson& Value, FBlackboardValue& OutCompare, FString& OutReason );
}
