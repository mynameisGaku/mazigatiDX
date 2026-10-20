#pragma once
#include "Library/Core/Types/TypeDefinition.h"

//=============================================================================
// ELogVerbosity
//   ログの重要度。値が小さいほど重要。
//   カテゴリに設定された Verbosity より大きい（詳細な）ログは出力されない。
//=============================================================================
enum class ELogVerbosity : uint8
{
	NoLogging = 0,  // 何も出力しない（カテゴリの Verbosity 指定用）

	Fatal,          // 致命的エラー。出力後にプログラムを停止する
	Error,          // エラー
	Warning,        // 警告
	Display,        // 常に表示したい情報
	Log,            // 通常のログ
	Verbose,        // 詳細ログ
	VeryVerbose,    // 非常に詳細なログ（毎フレームの値など）

	All = VeryVerbose,
};

const char* LogVerbosityToString( ELogVerbosity Verbosity );
// "Verbose" / "verbose" / "VeryVerbose" などから変換する（大文字小文字は区別しない）。認識できなければ false
bool LogVerbosityFromString( const char* Text, ELogVerbosity& OutVerbosity );

//-----------------------------------------------------------------------------
// コンパイル時に残す最大 Verbosity
//   これより詳細な LOG() 呼び出しはコンパイル時に除去される。
//   プロジェクト設定などで LOG_COMPILE_VERBOSITY を定義すれば上書きできる。
//
//   Release でも Verbose までは残す（実行時にカテゴリごとに有効にして、バグの原因を追えるように。
//   無効なときのコストは「カテゴリの Verbosity を 1 回読んで比べる」だけ）。
//   VeryVerbose（毎フレームの値など量が多いもの）は Release では除去する。
//-----------------------------------------------------------------------------
#ifndef LOG_COMPILE_VERBOSITY
	#ifdef _DEBUG
		#define LOG_COMPILE_VERBOSITY VeryVerbose
	#else
		#define LOG_COMPILE_VERBOSITY Verbose
	#endif
#endif
