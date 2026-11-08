// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Collection/String.h"

//=============================================================================
// IClipboard
//   クリップボード（コピー・貼り付けの受け渡し）の窓口。文字列だけ（UTF-8）。
//   実装: CDxLibInputSubsystem（OS のクリップボード）、CNullInputSubsystem（検査用。アプリの中だけで持つ）。
//   ゲームは GetAppSubsystem<IClipboard>() で取る。メインスレッドから使う。
//=============================================================================
class IClipboard
{
public:
	virtual ~IClipboard() = default;

	// クリップボードの文字列を読む。文字列が入っていない（画像だけ、空）なら false
	virtual bool GetText( FString& OutText ) const = 0;
	// クリップボードへ、文字列を入れる（それまでの中身は消える）。入れられなければ false
	virtual bool SetText( const FString& Text ) = 0;
};
