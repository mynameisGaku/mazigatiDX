// SPDX-License-Identifier: LicenseRef-Proprietary
#ifndef FRAMEWORK_CORE_INTERFACES_INPUT_IKEYBOARDINPUT_H
#define FRAMEWORK_CORE_INTERFACES_INPUT_IKEYBOARDINPUT_H

#include "Library/Core/Interfaces/Input/EKey.h"

// 毎フレームに一度取り込んだ物理キーの状態。文字入力やキーの自動反復は扱わない。
class IKeyboardInput
{
public:
	// 派生する入力実装の破棄を許可する。
	virtual ~IKeyboardInput() = default;
	// Key が押されていれば true。不正なキーは false。
	virtual bool IsKeyDown( EKey Key ) const = 0;
	// Key がこのフレームで押された場合だけ true。不正なキーは false。
	virtual bool IsKeyPressed( EKey Key ) const = 0;
	// Key がこのフレームで離された場合だけ true。フォーカス喪失も解放とする。
	virtual bool IsKeyReleased( EKey Key ) const = 0;
};

#endif
