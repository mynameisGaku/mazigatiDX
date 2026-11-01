// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Collection/ArrayView.h"
#include "Library/Core/Collection/String.h"

//=============================================================================
// NScreenshotNames
//   撮った画像の保存先を決める。
//   - 名前を指定しないとき: Directory/Screenshot_00001.png のように、Screenshot_ と 5 桁の番号（すでにある番号のいちばん大きいもの + 1。拡張子は問わずに数える）
//   - 名前を指定したとき: 相対なら Directory の下、絶対パスならそのまま。拡張子が無ければ Extension を付ける
//=============================================================================
namespace NScreenshotNames
{
	// 既定の保存先（プロジェクトのルートの Saved/Screenshots）
	FString GetDefaultDirectory();

	// ExistingFiles（Directory の中のファイル名）から、次の番号（1 以上）を決める。Screenshot_ と数字 5 桁以上で始まる名前だけを数える
	int32 FindNextNumber( TArrayView<const FString> ExistingFiles );

	// 番号の名前（例: Number = 15、Extension = ".png" -> "Screenshot_00015.png"）
	FString MakeNumberedName( int32 Number, const char* Extension );

	// 保存するパス。RequestedName が空なら番号の名前（Directory の中を見て決めた番号と MinimumNumber の大きい方。使った番号を OutNumber へ）。
	// Extension はドットつき（".png" など）。同じフレームに 2 つ撮るときも番号が重ならないよう、呼ぶ側は前に使った番号 + 1 を MinimumNumber に渡す
	FString ResolvePath( const FString& Directory, const FString& RequestedName, const char* Extension, int32 MinimumNumber = 1, int32* OutNumber = nullptr );
}
