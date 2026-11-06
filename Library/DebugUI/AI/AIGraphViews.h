// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Collection/String.h"
#include "Library/DebugUI/AI/NodeGraph.h"
#include "Library/Framework/AI/BehaviorTree/BehaviorTreeDefinition.h"

class CBehaviorTreeComponent;
class CStateMachineComponent;

//=============================================================================
// AI のグラフ表示（AI デバッガが使う）
//   ステートマシン: 各ステートを箱、遷移を矢印にして描く。入れ子のステートは、親（Machine）の箱の中に入る。配置は自動（入口から遷移をたどった順に、左から右へ並べる）。
//     - 今いるステートは緑の枠。Any State 遷移は、左の「ANY」の箱から出る
//     - 矢印の色: 遷移した直後は明るい緑（数秒かけて元に戻る）、条件が今満たされている（次の Tick で遷移する）ものは黄色、
//       今のステートから起こりうるが、条件は満たされていないものは明るい灰色、今のステートからは起こらないものは暗い灰色
//   ビヘイビアツリー: ノードをツリーに並べる（子の左右の順が優先順位）。動いているノードは緑（葉は黄）の枠、直近の結果は上辺の帯の色
//     （成功 = 緑・失敗 = 赤・中断 = オレンジ。終わった直後ほど明るい）、デコレーターの条件の真偽と、サービスは箱の中に出す。
//   どちらも、ドラッグでパン、ホイールでズーム、矢印や箱にマウスを載せると説明が出る。
//=============================================================================
struct FAIGraphState
{
	FNodeGraphView StateMachineView;
	FNodeGraphView BehaviorTreeView;
	// 表示している定義（変わったら、全体が収まるように合わせ直す）
	const void* ShownStateMachine = nullptr;
	const void* ShownBehaviorTree = nullptr;
	// グラフの高さ（ピクセル）
	float Height = 330.0f;
};

namespace NDebugUI
{
	// グラフを描く（ImGui のウィンドウの中で。高さは State.Height）
	void DrawStateMachineGraph( FAIGraphState& State, CStateMachineComponent& Component );
	void DrawBehaviorTreeGraph( FAIGraphState& State, CBehaviorTreeComponent& Component );

	// デコレーターの 1 行の説明（"BlackboardCondition HasTarget == true  [abort: Both]"）
	FString DescribeBehaviorDecorator( const FBTDecoratorDef& Decorator );
}
