// SPDX-License-Identifier: MIT
#ifndef FRAMEWORK_CORE_INTERFACES_INPUT_EKEY_H
#define FRAMEWORK_CORE_INTERFACES_INPUT_EKEY_H

#include "Library/Core/Types/TypeDefinition.h"

// 物理キーを表す共通番号。配列による表示文字の違いや IME の文字確定は扱わない。
enum class EKey : uint8
{
	// A キー。
	A,
	// B キー。
	B,
	// C キー。
	C,
	// D キー。
	D,
	// E キー。
	E,
	// F キー。
	F,
	// G キー。
	G,
	// H キー。
	H,
	// I キー。
	I,
	// J キー。
	J,
	// K キー。
	K,
	// L キー。
	L,
	// M キー。
	M,
	// N キー。
	N,
	// O キー。
	O,
	// P キー。
	P,
	// Q キー。
	Q,
	// R キー。
	R,
	// S キー。
	S,
	// T キー。
	T,
	// U キー。
	U,
	// V キー。
	V,
	// W キー。
	W,
	// X キー。
	X,
	// Y キー。
	Y,
	// Z キー。
	Z,
	// 主キー領域の 0 キー。
	Num0,
	// 主キー領域の 1 キー。
	Num1,
	// 主キー領域の 2 キー。
	Num2,
	// 主キー領域の 3 キー。
	Num3,
	// 主キー領域の 4 キー。
	Num4,
	// 主キー領域の 5 キー。
	Num5,
	// 主キー領域の 6 キー。
	Num6,
	// 主キー領域の 7 キー。
	Num7,
	// 主キー領域の 8 キー。
	Num8,
	// 主キー領域の 9 キー。
	Num9,
	// ファンクション 1 キー。
	F1,
	// ファンクション 2 キー。
	F2,
	// ファンクション 3 キー。
	F3,
	// ファンクション 4 キー。
	F4,
	// ファンクション 5 キー。
	F5,
	// ファンクション 6 キー。
	F6,
	// ファンクション 7 キー。
	F7,
	// ファンクション 8 キー。
	F8,
	// ファンクション 9 キー。
	F9,
	// ファンクション 10 キー。
	F10,
	// ファンクション 11 キー。
	F11,
	// ファンクション 12 キー。
	F12,
	// 上矢印。
	Up,
	// 下矢印。
	Down,
	// 左矢印。
	Left,
	// 右矢印。
	Right,
	// 空白。
	Space,
	// 主キー領域の確定。
	Enter,
	// 取り消し。
	Escape,
	// タブ。
	Tab,
	// 手前の文字を削除。
	BackSpace,
	// 左の Shift。
	LeftShift,
	// 右の Shift。
	RightShift,
	// 左の Control。
	LeftControl,
	// 右の Control。
	RightControl,
	// 左の Alt。
	LeftAlt,
	// 右の Alt。
	RightAlt,
	// 前のページ。
	PageUp,
	// 次のページ。
	PageDown,
	// 行または項目の末尾。
	End,
	// 行または項目の先頭。
	Home,
	// 挿入。
	Insert,
	// 次の文字を削除。
	Delete,
	// 主キー領域のマイナス。
	Minus,
	// 日本語配列の円記号。
	Yen,
	// 前の曲。配列によっては別の記号位置に対応する。
	PreviousTrack,
	// ピリオド。
	Period,
	// スラッシュ。
	Slash,
	// スクロール固定。
	ScrollLock,
	// セミコロン。
	Semicolon,
	// 日本語配列のコロン。
	Colon,
	// 左角括弧。
	LeftBracket,
	// 右角括弧。
	RightBracket,
	// 日本語配列のアットマーク。
	At,
	// バックスラッシュ位置。表示文字は配列に依存する。
	BackSlash,
	// カンマ。
	Comma,
	// 日本語入力の漢字切り替え。
	Kanji,
	// 日本語入力の変換。
	Convert,
	// 日本語入力の無変換。
	NonConvert,
	// 日本語入力のかな切り替え。
	Kana,
	// アプリケーションメニュー。
	Application,
	// 英大文字固定。
	CapsLock,
	// 画面の印刷。
	PrintScreen,
	// 一時停止。
	Pause,
	// 左の Windows。
	LeftWindows,
	// 右の Windows。
	RightWindows,
	// テンキーの数字固定。
	NumLock,
	// テンキーの 0。
	Numpad0,
	// テンキーの 1。
	Numpad1,
	// テンキーの 2。
	Numpad2,
	// テンキーの 3。
	Numpad3,
	// テンキーの 4。
	Numpad4,
	// テンキーの 5。
	Numpad5,
	// テンキーの 6。
	Numpad6,
	// テンキーの 7。
	Numpad7,
	// テンキーの 8。
	Numpad8,
	// テンキーの 9。
	Numpad9,
	// テンキーの掛け算。
	NumpadMultiply,
	// テンキーの足し算。
	NumpadAdd,
	// テンキーの引き算。
	NumpadSubtract,
	// テンキーの小数点。
	NumpadDecimal,
	// テンキーの割り算。
	NumpadDivide,
	// テンキーの確定。
	NumpadEnter,
	// キー数。入力としては使わない。
	Count
};

#endif

