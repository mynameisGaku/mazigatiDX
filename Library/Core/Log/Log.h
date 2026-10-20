#pragma once
#include "Library/Core/Log/LogVerbosity.h"
#include "Library/Core/Log/LogCategory.h"
#include "Library/Core/Log/LogSink.h"
#include "Library/Core/Log/LogManager.h"

//=============================================================================
// LOG( Category, Verbosity, Format, ... )
//   printf 形式でログを出力する。ファイル名・行番号・関数名は自動で付く。
//
//   LOG( LogMySystem, Warning, "value = %d", n );
//   -> [LogMySystem]: Warning: CMySystem: Tick: value = 3 | MySystem.cpp | line: 42
//
//   重要度の使い分け:
//     Fatal       続行できない致命的な状態（出力後に停止する）
//     Error       機能が動かなくなる失敗（読み込み・初期化・生成に失敗した、など）。必ず原因（ファイル名・エラー文）を含める
//     Warning     続行はできるが想定外のこと（既定値へのフォールバック、無効な引数を無視した、など）
//     Display     常に見せたい情報
//     Log         起動・終了・シーン遷移・読み込み結果・デバイスの接続切断など、節目の出来事（1 回の動作で 1 行まで）
//     Verbose     オブジェクト単位の生成・破棄、各 API 呼び出しの成功、設定の変更など、バグを追うときに欲しい動作
//     VeryVerbose 毎フレームや大量に出る値（Release ではコンパイル時に除去される）
//   機能を持つクラスの関数は、失敗したら Error / Warning、成功したら（節目なら Log、細かい動作なら Verbose）を残す。
//   実行時の切り替えは、デバッグ画面の "Log categories" か、起動引数 --log-verbose / --log=LogScene:Verbose,...
//
//   - LOG_COMPILE_VERBOSITY より詳細なログはコンパイル時に除去される
//   - カテゴリの実行時 Verbosity より詳細なログは出力されない
//   - Fatal は出力後にプログラムを停止する
//=============================================================================
#define LOG( CategoryName, Verbosity, Format, ... )                                                       \
	do                                                                                                    \
	{                                                                                                     \
		if constexpr ( ELogVerbosity::Verbosity <= ELogVerbosity::LOG_COMPILE_VERBOSITY                  \
					   || ELogVerbosity::Verbosity == ELogVerbosity::Fatal )                              \
		{                                                                                                 \
			if ( !( CategoryName ).IsSuppressed( ELogVerbosity::Verbosity ) )                             \
			{                                                                                             \
				CLogManager::Get().Log( CategoryName, ELogVerbosity::Verbosity,                           \
										__FILE__, __LINE__, __FUNCTION__, Format, ##__VA_ARGS__ );        \
			}                                                                                             \
		}                                                                                                 \
	} while ( 0 )

//=============================================================================
// 汎用カテゴリ（とりあえず出したいとき用）
//=============================================================================
DECLARE_LOG_CATEGORY_EXTERN( LogTemp, All )
