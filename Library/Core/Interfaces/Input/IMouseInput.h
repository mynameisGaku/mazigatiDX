// SPDX-License-Identifier: MIT
#ifndef FRAMEWORK_CORE_INTERFACES_INPUT_IMOUSEINPUT_H
#define FRAMEWORK_CORE_INTERFACES_INPUT_IMOUSEINPUT_H

#include "Library/Core/Interfaces/Input/EMouseButton.h"
#include "Library/Core/Interfaces/Input/MouseState.h"

// 毎フレームに一度取り込んだマウス入力。カーソルの表示状態は変更しない。
class IMouseInput
{
public:
	// 派生する入力実装の破棄を許可する。
	virtual ~IMouseInput() = default;
	// 次の入力更新まで同じ状態を参照する。座標の有効性は bPositionValid で判定する。
	virtual const FMouseState& GetMouseState() const = 0;
	// Button が押されていれば true。不正なボタンは false。
	virtual bool IsMouseButtonDown( EMouseButton Button ) const = 0;
	// Button がこのフレームで押された場合だけ true。不正なボタンは false。
	virtual bool IsMouseButtonPressed( EMouseButton Button ) const = 0;
	// Button がこのフレームで離された場合だけ true。フォーカス喪失も解放とする。
	virtual bool IsMouseButtonReleased( EMouseButton Button ) const = 0;
};

#endif
