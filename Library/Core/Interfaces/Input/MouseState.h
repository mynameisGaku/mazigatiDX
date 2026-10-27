// SPDX-License-Identifier: MIT
#ifndef FRAMEWORK_CORE_INTERFACES_INPUT_MOUSESTATE_H
#define FRAMEWORK_CORE_INTERFACES_INPUT_MOUSESTATE_H

#include "Library/Core/Types/TypeDefinition.h"

// 一つのフレームで取り込んだマウス状態。座標はゲーム描画領域の左上を原点にする。
struct FMouseState
{
	// 描画座標での横位置。右方向が正。
	int32 X = 0;
	// 描画座標での縦位置。下方向が正。
	int32 Y = 0;
	// 前フレームからの横移動。初回取得とフォーカス復帰時は 0。
	int32 DeltaX = 0;
	// 前フレームからの縦移動。初回取得とフォーカス復帰時は 0。
	int32 DeltaY = 0;
	// このフレームの横ホイール回転。右方向を正、通常の1段分を 1 とする。
	float32 WheelX = 0.0f;
	// このフレームの縦ホイール回転。上方向を正、通常の1段分を 1 とする。
	float32 WheelY = 0.0f;
	// 押されているボタン。EMouseButton の番号をビット位置に使う。
	uint32 Buttons = 0;
	// このフレームで押されたボタンのビット。
	uint32 PressedButtons = 0;
	// このフレームで離されたボタンのビット。
	uint32 ReleasedButtons = 0;
	// このフレームの位置が取得できた場合だけ true。
	bool bPositionValid = false;
	// 実装が横ホイール取得に対応していれば true。
	bool bHorizontalWheelSupported = false;
	// ゲームのメインウィンドウに入力の対象がある場合だけ true。
	bool bFocused = false;
};

#endif
