// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Types/TypeDefinition.h"

// 角速度は rad/s、加速度は m/s²。座標軸は機器の SDL 定義を保つ。
enum class EGamepadSensor : uint8
{
	Accelerometer, // 本体の加速度。
	Gyroscope, // 本体の角速度。
	AccelerometerLeft, // 左 Joy-Con の加速度。
	GyroscopeLeft, // 左 Joy-Con の角速度。
	AccelerometerRight, // 右 Joy-Con の加速度。
	GyroscopeRight, // 右 Joy-Con の角速度。
	Count // 状態配列の要素数。
};
