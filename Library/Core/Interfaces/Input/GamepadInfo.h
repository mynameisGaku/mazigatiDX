// SPDX-License-Identifier: MIT
#ifndef FRAMEWORK_INPUT_GAMEPADINFO_H
#define FRAMEWORK_INPUT_GAMEPADINFO_H

#include "Library/Core/Collection/String.h"
#include "Library/Core/Interfaces/Input/EGamepadType.h"

// 接続中の機器の識別情報。ID は抜き差しで変わり、配列内の位置には依存しない。
struct FGamepadInfo
{
	// 接続期間中だけ有効な識別子。0 は無効。
	uint32 Id = 0;
	// UTF-8 から変換した機器名。
	FString Name;
	// 配置追加に用いる SDL の識別文字列。
	FString Guid;
	// 機器が申告した系統。
	EGamepadType Type = EGamepadType::Generic;
	// USB の製造元番号。
	uint16 Vendor = 0;
	// USB の製品番号。
	uint16 Product = 0;
	// 共通ボタン・軸の割り当てが存在するか。
	bool bMapped = false;
	// 本体振動を利用できるか。
	bool bRumble = false;
	// トリガーの振動を利用できるか。
	bool bTriggerRumble = false;
	// 色指定のランプを利用できるか。
	bool bLed = false;
	// 生ボタンの個数。
	int32 NumButtons = 0;
	// 生軸の個数。
	int32 NumAxes = 0;
	// 十字キー方向値の個数。
	int32 NumHats = 0;
};

#endif
