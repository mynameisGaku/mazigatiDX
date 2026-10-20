// SPDX-License-Identifier: LicenseRef-Proprietary
#ifndef FRAMEWORK_INPUT_GAMEPADSTATE_H
#define FRAMEWORK_INPUT_GAMEPADSTATE_H

#include "Library/Core/Collection/Array.h"
#include "Library/Core/Interfaces/Input/EGamepadAxis.h"
#include "Library/Core/Interfaces/Input/EGamepadButton.h"
#include "Library/Core/Interfaces/Input/EGamepadSensor.h"
#include "Library/Core/Interfaces/Input/GamepadSensorState.h"

// フレーム開始時の入力値。切断時は1フレームだけ解放状態を取得できる。
struct FGamepadState
{
	// 押されている共通ボタン。
	bool Buttons[static_cast<int32>( EGamepadButton::Count )] = {};
	// この更新で押された共通ボタン。
	bool PressedButtons[static_cast<int32>( EGamepadButton::Count )] = {};
	// この更新で離された共通ボタン。
	bool ReleasedButtons[static_cast<int32>( EGamepadButton::Count )] = {};
	// 共通配置の軸。利用不能な軸はゼロ。
	float32 Axes[static_cast<int32>( EGamepadAxis::Count )] = {};
	// 有効にした測定器の値。
	FGamepadSensorState Sensors[static_cast<int32>( EGamepadSensor::Count )];
	// 配置未登録機器でも読める番号順ボタン。
	TArray<uint8> RawButtons;
	// 番号順ボタンの押下開始。
	TArray<uint8> RawPressedButtons;
	// 番号順ボタンの解放開始。
	TArray<uint8> RawReleasedButtons;
	// 番号順の符号付き軸。範囲は -1～1。
	TArray<float32> RawAxes;
	// 上=1、右=2、下=4、左=8 の組合せ。
	TArray<uint8> RawHats;
	// 機器が接続されているか。
	bool bConnected = false;
	// ゲームが入力を受け付ける状態か。
	bool bFocused = false;

	// 無効なボタン番号なら false を返す。
	bool IsDown( EGamepadButton Button ) const;
	// 無効なボタン番号なら false を返す。
	bool IsPressed( EGamepadButton Button ) const;
	// 無効なボタン番号なら false を返す。
	bool IsReleased( EGamepadButton Button ) const;
	// 無効な軸番号なら 0 を返す。
	float32 GetAxis( EGamepadAxis Axis ) const;
};

#endif
