// SPDX-License-Identifier: LicenseRef-Proprietary
#ifndef FRAMEWORK_CORE_INTERFACES_INPUT_EMOUSEBUTTON_H
#define FRAMEWORK_CORE_INTERFACES_INPUT_EMOUSEBUTTON_H

#include "Library/Core/Types/TypeDefinition.h"

// マウスの物理ボタン番号。各値は FMouseState の同じ番号のビットに対応する。
enum class EMouseButton : uint8
{
	// 左ボタン。
	Left,
	// 右ボタン。
	Right,
	// ホイールの押し込み。
	Middle,
	// 追加の第4ボタン。一般的なマウスでは戻るボタン。
	Button4,
	// 追加の第5ボタン。一般的なマウスでは進むボタン。
	Button5,
	// 追加の第6ボタン。
	Button6,
	// 追加の第7ボタン。
	Button7,
	// 追加の第8ボタン。
	Button8,
	// ボタン数。入力としては使わない。
	Count
};

#endif
