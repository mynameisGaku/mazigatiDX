// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Types/Callable.h"
#include "Library/Core/Types/TypeDefinition.h"

//=============================================================================
// AsyncTask
//   仕事を、決めた種類のスレッドで、あとで実行する（Unreal Engine の AsyncTask と同じ使い方）。どのスレッドからでも呼べる。
//
//   AsyncTask( EThreadType::Worker, [ Path ]()                     // 重い仕事はワーカースレッドで
//   {
//       FParsedData Data = Parse( Path );
//       AsyncTask( EThreadType::Game, [ Data ]()                   // 結果は、ゲームスレッドへ戻して使う（描画・アセット・オブジェクト）
//       {
//           Apply( Data );
//       } );
//   } );
//
//   - Game: ゲームスレッドの次のフレームのはじめ（更新の前）に、頼んだ順に実行する。ゲームスレッドから頼んでも、その場では実行しない
//   - Worker: ジョブシステム（IJobSystem）のワーカーで実行する。ワーカーが 0 なら、呼んだスレッドでその場で実行する
//   - 仕事は、呼んだ側の変数を参照で使わない（値で持たせる）。実行の時点では、呼んだ関数は終わっている
//=============================================================================
enum class EThreadType : uint8
{
	// ゲームスレッド（メインスレッド。DxLib を触る仕事）
	Game,
	// ワーカースレッド（重い計算・ファイルの読み込みなど。DxLib は触らない）
	Worker,
};

// Name は文字列リテラル（プロファイラーとログに出る）
void AsyncTask( EThreadType Thread, TCallable<void()> Task, const char* Name = "AsyncTask" );
