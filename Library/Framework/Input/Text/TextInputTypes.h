// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Collection/Array.h"
#include "Library/Core/Collection/String.h"
#include "Library/Core/Types/TypeDefinition.h"

//=============================================================================
// 文字入力の値の型
//   ITextInput が毎フレーム返す。キーの状態（IKeyboardInput）とは別で、OS の文字入力（キーの自動反復・かな漢字変換・全角）を通った結果。
//   文字列は UTF-8。
//=============================================================================

// 文字入力の欄を編集する操作（キーの自動反復を含む。押しっぱなしで繰り返し届く）
enum class ETextEditCommand : uint8
{
	Backspace,
	Delete,
	Enter,
	Tab,
	Escape,
	Left,
	Right,
	Up,
	Down,
	Home,
	End,
	PageUp,
	PageDown,
	Copy,
	Cut,
	Paste,
	SelectAll,
};

// 文字入力 1 つぶん。文字列か、編集の操作のどちらか
struct FTextInputEvent
{
	// true なら Command、false なら Text が有効
	bool bCommand = false;
	ETextEditCommand Command = ETextEditCommand::Backspace;
	// 確定した文字（UTF-8。1 文字とは限らない。制御文字は含まない）
	FString Text;

	static FTextInputEvent MakeText( const FString& InText )
	{
		FTextInputEvent Event;
		Event.Text = InText;
		return Event;
	}

	static FTextInputEvent MakeCommand( ETextEditCommand InCommand )
	{
		FTextInputEvent Event;
		Event.bCommand = true;
		Event.Command = InCommand;
		return Event;
	}
};

// かな漢字変換（IME）で入力している途中の状態。確定した文字は FTextInputEvent の Text として届く
struct FImeComposition
{
	// 変換中の文字があるときだけ true
	bool bActive = false;
	// 入力している途中の文字列（UTF-8）
	FString Text;
	// 途中の文字列の中の、カーソルの位置（先頭からのバイト数）
	int32 CursorByteOffset = 0;
	// 変換の候補（出ていないときは空）
	TArray<FString> Candidates;
	// 選んでいる候補の番号（候補が無いときは -1）
	int32 SelectedCandidate = -1;
};
