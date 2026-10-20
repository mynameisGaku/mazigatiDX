// SPDX-License-Identifier: LicenseRef-Proprietary
#ifndef FRAMEWORK_INPUT_EGAMEPADTYPE_H
#define FRAMEWORK_INPUT_EGAMEPADTYPE_H

#include "Library/Core/Types/TypeDefinition.h"

// 機器が申告した互換系統。利用可能な機能は個別の能力値で確認する。
enum class EGamepadType : uint8
{
	Generic, // 未登録機器・USB 変換アダプター。
	Standard, // 共通配置を持つ機器。
	Xbox360, // Xbox 360 互換。
	XboxOne, // Xbox One・Series 互換。
	PS3, // PlayStation 3 互換。
	PS4, // PlayStation 4 互換。
	PS5, // PlayStation 5 互換。
	SwitchPro, // Nintendo Switch Pro 互換。
	JoyConLeft, // 左 Joy-Con。
	JoyConRight, // 右 Joy-Con。
	JoyConPair, // 左右を結合した Joy-Con。
	Switch2Pro, // 純正 Nintendo Switch 2 Pro。
	JoyCon2Left, // 純正左 Joy-Con 2。
	JoyCon2Right, // 純正右 Joy-Con 2。
	JoyCon2Pair, // 純正 Joy-Con 2 の組。
	GameCube, // GameCube 互換。
	Steam // Steam Controller 互換。
};

#endif
