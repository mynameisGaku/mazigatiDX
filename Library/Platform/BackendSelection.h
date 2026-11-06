// SPDX-License-Identifier: MIT
#pragma once
#include "Library/Core/Collection/String.h"

//=============================================================================
// NBackendSelection
//   どのプラットフォームの実装（ウィンドウ・描画・読み込み・音・入力）を使うかを、起動の引数から決める窓口。
//   DxLib を読まない（Library/Platform/DxLib の外にある）ので、DxLib の実装も、DxLib を使わない実装も、同じ窓口で判断する。
//
//   game.exe --backend=null            ウィンドウを出さず、描かず、音を出さない実装（Library/Platform/Null）で動かす
//   game.exe                           （指定なし）DxLib の実装で動かす
//
//   各実装のサブシステムは、ShouldCreateSubsystem でこの窓口を見て、自分を作るかを決める。
//   「null」以外の値は、DxLib として扱う。
//=============================================================================
namespace NBackendSelection
{
	// 起動の引数に --backend=null（大文字小文字は区別しない）があるか。true なら、DxLib の実装は作られず、Null の実装が作られる
	bool IsNullRequested();
	// 渡したコマンドライン文字列で調べる版（確認用）
	bool IsNullRequested( const FString& CommandLine );
}
