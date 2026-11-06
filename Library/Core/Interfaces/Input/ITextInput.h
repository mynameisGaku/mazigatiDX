// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Collection/Array.h"
#include "Library/Framework/Input/Text/TextInputTypes.h"

//=============================================================================
// ITextInput
//   文字入力（OS を通った文字・キーの自動反復・かな漢字変換）の窓口。キーの状態（IKeyboardInput）とは別。
//   実装: CDxLibInputSubsystem（DxLib）、CNullInputSubsystem（検査用）。ゲームは GetAppSubsystem<ITextInput>() で取る。すべてメインスレッドから使う。
//
//   - 欄に文字を打てるようにするときだけ SetTextInputActive( true ) にし、終わったら false に戻す。
//     false の間は、文字もかな漢字変換も受けない（WASD などのゲーム操作に、変換や全角が割り込まない）
//   - GetTextInputEvents は、このフレームに届いた文字と編集の操作を、届いた順に返す。参照は次の入力更新まで有効
//   - 文字列は UTF-8。バックエンドごとの文字コード・制御コード・分割された文字は、実装が吸収して、ここには出さない
//   - FTextEditBuffer（Framework/Input/Text）に渡すと、1 行の入力欄になる
//=============================================================================
class ITextInput
{
public:
	virtual ~ITextInput() = default;

	// 文字入力を受けるか切り替える。切り替えたとき、たまっていた古い入力は捨てる
	virtual void SetTextInputActive( bool bActive ) = 0;
	virtual bool IsTextInputActive() const = 0;

	// このフレームに届いた文字と編集の操作（届いた順）。受けていない間は空
	virtual const TArray<FTextInputEvent>& GetTextInputEvents() const = 0;

	// かな漢字変換で入力している途中の状態。変換していない・対応していない実装は、bActive が false
	virtual const FImeComposition& GetImeComposition() const = 0;
};
