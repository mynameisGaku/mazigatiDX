#pragma once

//=============================================================================
// デバッグ限定のコンソール（実行中にコマンドを入力する窓）の有効 / 無効
//
//   COG_DEBUG_CONSOLE が 1 のとき: コンソールの窓と、CONSOLE_COMMAND / CONSOLE_VARIABLE による登録が有効になる。
//   0 のとき（Release の既定）: CONSOLE_COMMAND / CONSOLE_VARIABLE は何もしない（ラムダの本体もコンパイルされず、コードもデータも残らない）。
//
//   既定は Debug ビルド（_DEBUG）だけ有効。Release で使いたいときは、プロジェクトのプリプロセッサ定義に COG_FORCE_DEBUG_CONSOLE を足す。
//   登録・実行の仕組み（FConsoleRegistry など）自体は、どちらの構成でもコンパイルされる（確認用・自分で呼ぶ用）。
//=============================================================================
#ifndef COG_DEBUG_CONSOLE
	#if defined( _DEBUG ) || defined( COG_FORCE_DEBUG_CONSOLE )
		#define COG_DEBUG_CONSOLE 1
	#else
		#define COG_DEBUG_CONSOLE 0
	#endif
#endif
