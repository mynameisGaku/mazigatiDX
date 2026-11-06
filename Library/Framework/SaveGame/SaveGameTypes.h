// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Collection/String.h"
#include "Library/Core/Time/DateTime.h"
#include "Library/Core/Types/TypeDefinition.h"

//=============================================================================
// セーブデータの値の型
//=============================================================================

// 読み込みの結果
enum class ESaveLoadResult : uint8
{
	// 読めた
	Ok,
	// 読めた。ただし本体が壊れていたので、1 つ前のセーブ（バックアップ）から読んだ
	RecoveredFromBackup,
	// そのスロットは無い
	NotFound,
	// 本体もバックアップも壊れていて、読めない
	Corrupt,
	// スロットの名前が使えない形
	InvalidSlotName,
};

// スロット 1 つの情報
struct FSaveSlotInfo
{
	// スロットの名前
	FString Name;
	// 中身（ペイロード）の大きさ（バイト）。壊れていれば 0
	int64 PayloadBytes = 0;
	// 最後に書き込んだ日時（UTC）。分からなければ 1970-01-01
	FDateTime Modified;
	// 本体が壊れていない（そのまま読める）か
	bool bValid = false;
	// 1 つ前のセーブ（バックアップ）があるか
	bool bHasBackup = false;
};
