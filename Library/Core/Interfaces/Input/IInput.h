// SPDX-License-Identifier: MIT
#ifndef FRAMEWORK_CORE_INTERFACES_INPUT_IINPUT_H
#define FRAMEWORK_CORE_INTERFACES_INPUT_IINPUT_H

#include "Library/Core/Interfaces/Input/IKeyboardInput.h"
#include "Library/Core/Interfaces/Input/IMouseInput.h"

// キーボードとマウスをまとめて取得する既存窓口。各入力は同じフレームの状態を返す。
class IInput : public IKeyboardInput, public IMouseInput
{
public:
	// 派生する入力実装の破棄を許可する。
	~IInput() override = default;
};

#endif
