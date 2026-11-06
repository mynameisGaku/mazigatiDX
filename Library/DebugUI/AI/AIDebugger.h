// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Collection/Array.h"
#include "Library/Core/Collection/String.h"
#include "Library/Core/Types/TypeDefinition.h"
#include "Library/DebugUI/AI/AIGraphViews.h"

//=============================================================================
// AI デバッガ
//   シーンの中の、ステートマシン（CStateMachineComponent）・ビヘイビアツリー（CBehaviorTreeComponent）を持つオブジェクトを選んで、
//   「いま何をしているか・なぜそうなったか」を追う ImGui のウィンドウ。ゲームの型もエディタも知らない（コンポーネントの公開関数だけを使う）。
//
//   - ステートマシン: 今のステートの経路と経過時間、ステートの階層（動いているものを強調）、今起こりうる遷移と条件が満たされているか、
//     遷移の履歴（いつ・どの遷移か）、イベントの送信
//   - ビヘイビアツリー: ノードの木（動いているもの・直近の結果・中断されたか・デコレーターの条件の真偽・サービス）
//   - ブラックボード: すべてのキーの現在値（クリックで書き換え）、変わった直後の強調、デバッガが見た変更の履歴
//   - グラフ表示（AIGraphViews.h）: ステートマシンは各ステートを箱・遷移を矢印にして（遷移した直後は緑、条件が満たされているものは黄色）、
//     ビヘイビアツリーはノードのツリーにして、動いているものと直近の結果を色で出す（パンとズームができる）。同じ内容のリストも、下に畳んである
//   - 問題の一覧（読み込み・コンパイルで出たもの）
//   - 一時停止と 1 Tick のステップ実行（選んだオブジェクトの AI だけ）
//
//   ゲーム側は FAIDebuggerState を 1 つ持ち、開いている間、毎フレーム DrawAIDebugger を呼ぶ（FAssetToolsState が持っているので、普通は意識しない）。
//=============================================================================
struct FAIDebuggerState
{
	// 選んでいるオブジェクトの Id（0 は、最初の候補を自動で選ぶ）
	uint64 SelectedObjectId = 0;

	// デバッガが見たブラックボードの変更
	struct FChange
	{
		float64 Time = 0.0;
		FString Text;
	};
	TArray<FChange> Changes;
	// 変更を見ているブラックボード（比べるだけで、中身は読まない）と、キーごとの前回のリビジョン・最後に変わった時刻
	const void* ObservedBlackboard = nullptr;
	TArray<uint32> LastRevisions;
	TArray<float64> LastChangeTimes;

	// 最初に選ぶオブジェクトの名前の一部（--ai-object=Guard など。最初の 1 回だけ使う）
	FString InitialSelection;

	// グラフ表示のパン・ズーム・高さ
	FAIGraphState Graph;

	// 入力欄
	char EventName[64] = {};
	float32 StepSeconds = 1.0f / 60.0f;
};

namespace NDebugUI
{
	// ウィンドウを描く（pOpen の x ボタンで閉じられる）
	void DrawAIDebugger( FAIDebuggerState& State, bool* pOpen );
}
