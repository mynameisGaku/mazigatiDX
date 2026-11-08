// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Collection/Array.h"
#include "Library/Core/Interfaces/Input/IGamepadInput.h"
#include "Library/Core/Interfaces/Input/IKeyboardInput.h"
#include "Library/Core/Interfaces/Input/IMouseInput.h"
#include "Library/Framework/UI/Core/UIContext.h"

//=============================================================================
// FUIInputMapping
//   キーボード・パッドの、どの入力を、UI のどの操作にするか（ホストが決める。既定は下のとおり）。
//     矢印キー           : フォーカスを動かす / スライダーなどの値を変える
//     Enter・Space       : 決定
//     Escape             : 戻る
//     Tab / Shift + Tab  : フォーカスを次 / 前へ
//     パッド             : 十字キー = 矢印 / 下側の決定ボタン（South）= 決定 / 右側のボタン（East）= 戻る / 右上・左上のボタン = 次 / 前
//   （パッドのスティックは、まだ扱わない）
//=============================================================================
struct FUIInputMapping
{
	TArray<EKey> NavigateUpKeys { EKey::Up };
	TArray<EKey> NavigateDownKeys { EKey::Down };
	TArray<EKey> NavigateLeftKeys { EKey::Left };
	TArray<EKey> NavigateRightKeys { EKey::Right };
	TArray<EKey> ConfirmKeys { EKey::Enter, EKey::Space };
	TArray<EKey> CancelKeys { EKey::Escape };
	TArray<EKey> FocusKeys { EKey::Tab };                          // 押すと次へ。下の修飾キーを押しながらなら、前へ
	TArray<EKey> FocusPreviousModifierKeys { EKey::LeftShift, EKey::RightShift };
	bool bUseGamepad = true;
};

namespace NUIInput
{
	// ゲームの入力の窓口から、このフレームの FUIInputFrame を作る（毎フレーム、CUIContext::Update の前に呼ぶ）。
	//   どの窓口も、null でよい（その入力は、何も押されていない扱い）。
	//   マウス: 位置が取れ、ゲームのウィンドウに入力の対象があるときだけ、ポインターが有効。主ボタン = 左、副ボタン = 右。
	//   パッド: つながっているすべてのパッドの、このフレームで押されたボタンをまとめる。
	FUIInputFrame MakeFrame( const IKeyboardInput* Keyboard, const IMouseInput* Mouse, const IGamepadInput* Gamepad, const FUIInputMapping& Mapping = FUIInputMapping() );
}
