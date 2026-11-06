// SPDX-License-Identifier: MIT
#pragma once
#include "Library/Core/Collection/Array.h"
#include "Library/Core/Collection/Optional.h"
#include "Library/Core/Collection/String.h"

//=============================================================================
// NCommandLine
//   起動したときのコマンドライン（Windows API）と、その読み取り。WinMain の引数や windows.h を、ゲームのコードに出さないための窓口。
//
//   // game.exe --sample=Hello --console "--title=My Game"
//   if ( NCommandLine::HasFlag( "console" ) ) ...                                   // --console があるか
//   if ( TOptional<FString> Sample = NCommandLine::GetValue( "sample" ); Sample.IsSet() ) ...   // --sample=Hello の値（"Hello"）
//
//   - 引数は空白で区切る。"..." で囲むと、空白を含められる（引用符は取り除く）。"--title=My Game" も、--title="My Game" も、値は My Game。
//   - 先頭（実行ファイルのパス）は、引数として数えない。
//   - 名前は、"--" を付けずに渡す。大文字小文字は区別する。
//   - HasFlag は "--name"（値なし）だけに合う。"--name=値" や "--name-more" には合わない。
//   - GetValue は "--name=値" の値を返す（"--name=" なら空の文字列）。同じ名前が複数あれば、最初のもの。
//   引数を渡す版（第 1 引数がコマンドライン全体）は、確認や、別の文字列を読むときに使う。
//=============================================================================
namespace NCommandLine
{
	// コマンドライン全体を返す。先頭は実行ファイルのパス（空白を含むときは引用符つき）、続けて引数。
	FString Get();

	// コマンドライン全体を、引数に分ける。先頭の実行ファイルのパスも含む（引用符は取り除く）。
	TArray<FString> Split( const FString& CommandLine );

	// 引数 "--Name"（値なし）があるか。
	bool HasFlag( const FString& CommandLine, const char* Name );
	bool HasFlag( const char* Name );

	// 引数 "--Name=値" の値。無ければ、未設定を返す。
	TOptional<FString> GetValue( const FString& CommandLine, const char* Name );
	TOptional<FString> GetValue( const char* Name );
}
