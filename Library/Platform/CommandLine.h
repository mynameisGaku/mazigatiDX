// SPDX-License-Identifier: MIT
#pragma once
#include "Library/Core/Collection/String.h"

//=============================================================================
// NCommandLine
//   起動したときのコマンドライン（Windows API）。WinMain の引数や windows.h を、ゲームのコードに出さないための窓口。
//
//   const FString CommandLine = NCommandLine::Get();
//=============================================================================
namespace NCommandLine
{
	// コマンドライン全体を返す。先頭は実行ファイルのパス（空白を含むときは引用符つき）、続けて引数。
	FString Get();
}
