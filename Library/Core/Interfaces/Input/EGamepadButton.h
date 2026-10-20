// SPDX-License-Identifier: LicenseRef-Proprietary
#ifndef FRAMEWORK_INPUT_EGAMEPADBUTTON_H
#define FRAMEWORK_INPUT_EGAMEPADBUTTON_H

#include "Library/Core/Types/TypeDefinition.h"

// 印字ではなく物理位置で区別するボタン。無効値への問い合わせは false を返す。
enum class EGamepadButton : uint8
{
	South, // 下側の決定ボタン。
	East, // 右側のボタン。
	West, // 左側のボタン。
	North, // 上側のボタン。
	Back, // 戻る・選択ボタン。
	Guide, // 本体メニューボタン。
	Start, // 開始・メニューボタン。
	LeftStick, // 左スティック押し込み。
	RightStick, // 右スティック押し込み。
	LeftShoulder, // 左上部ボタン。
	RightShoulder, // 右上部ボタン。
	DPadUp, // 十字キー上。
	DPadDown, // 十字キー下。
	DPadLeft, // 十字キー左。
	DPadRight, // 十字キー右。
	Misc1, // 共有・撮影などの追加ボタン。
	RightPaddle1, // 右背面上。
	LeftPaddle1, // 左背面上。
	RightPaddle2, // 右背面下。
	LeftPaddle2, // 左背面下。
	Touchpad, // タッチ面押し込み。
	Misc2, // 機種固有の追加ボタン2。
	Misc3, // 機種固有の追加ボタン3。
	Misc4, // 機種固有の追加ボタン4。
	Misc5, // 機種固有の追加ボタン5。
	Misc6, // 機種固有の追加ボタン6。
	Count // 状態配列の要素数。
};

#endif
