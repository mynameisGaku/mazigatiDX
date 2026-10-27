// SPDX-License-Identifier: MIT
#ifndef FRAMEWORK_INPUT_GAMEPADSENSORSTATE_H
#define FRAMEWORK_INPUT_GAMEPADSENSORSTATE_H

#include "Library/Core/Types/TypeDefinition.h"

// 一度の入力更新で取得した測定値。取得失敗時は bValid=false とゼロ値になる。
struct FGamepadSensorState
{
	// SDL が定める機器座標での X、Y、Z。
	float32 Values[3] = {};
	// この機器が測定に対応しているか。
	bool bAvailable = false;
	// 利用者が測定を有効にしたか。
	bool bEnabled = false;
	// 今フレームの値を取得できたか。
	bool bValid = false;
};

#endif
