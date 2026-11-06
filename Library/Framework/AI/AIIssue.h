// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Collection/Array.h"
#include "Library/Core/Collection/String.h"
#include "Library/Core/Types/TypeDefinition.h"

//=============================================================================
// 検証で見つかった問題（読み込み・エディタの Issues の一覧に出す）
//=============================================================================
enum class EAIIssueSeverity : uint8
{
	Info,       // 補正した（範囲に丸めた、など）。動作には影響しない
	Warning,    // 問題があるが、無効にして続行できる（未登録の部品、足りない項目など）
	Error,      // 致命的。そのアセットは使えない（根が無い、入口が無い、など）
};

struct FAIIssue
{
	EAIIssueSeverity Severity = EAIIssueSeverity::Warning;
	// 問題の場所（例: "States/Run/OnEnter[0]"）
	FString Where;
	FString Message;
};

namespace NAI
{
	// Error が 1 つでもあるか
	bool HasErrors( const TArray<FAIIssue>& Issues );

	// 問題の一覧をログに出す（AssetPath は、どのアセットかを示す）
	void LogIssues( const FString& AssetPath, const TArray<FAIIssue>& Issues );

	// 問題を足す
	void AddIssue( TArray<FAIIssue>* Issues, EAIIssueSeverity Severity, const FString& Where, const FString& Message );
}
