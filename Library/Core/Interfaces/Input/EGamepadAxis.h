// SPDX-License-Identifier: MIT
#ifndef FRAMEWORK_INPUT_EGAMEPADAXIS_H
#define FRAMEWORK_INPUT_EGAMEPADAXIS_H

#include "Library/Core/Types/TypeDefinition.h"

// スティックは -1～1、トリガーは 0～1。Y の正方向は下。
enum class EGamepadAxis : uint8
{
	LeftX, // 左スティック左右。
	LeftY, // 左スティック上下。
	RightX, // 右スティック左右。
	RightY, // 右スティック上下。
	LeftTrigger, // 左トリガー。
	RightTrigger, // 右トリガー。
	Count // 状態配列の要素数。
};

#endif
