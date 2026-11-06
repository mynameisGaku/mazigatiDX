// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Collection/Array.h"
#include "Library/Core/Collection/String.h"
#include "Library/Framework/AI/AIIssue.h"
#include "Library/Framework/AI/Blackboard/BlackboardCompare.h"
#include "Library/Framework/AI/StateMachine/StateMachineDefinition.h"
#include "Library/Json/JsonObject.h"

//=============================================================================
// ビヘイビアツリーの定義（.gxbt）
//   判断をツリーで定義する。不変のデータなので、同じ定義を使う全インスタンスで共有してよい。
//   ノードは平らな配列（先頭がルート。Parent が親の番号。子の順 = 優先順位）で持つ。JSON では入れ子に書く。
//
//   {
//     "Version": 1,
//     "Blackboard": "AI/Enemy.gxblackboard",
//     "Root": {
//       "Type": "Selector",
//       "Services": [ { "Do": "SensePlayer", "Interval": 0.1, "Params": { "SightRange": 7.0 } } ],
//       "Children": [
//         { "Type": "Sequence", "Name": "Attack",
//           "Decorators": [ { "Type": "BlackboardCondition", "Key": "InAttackRange", "Op": "==", "Value": true, "Abort": "LowerPriority" },
//                           { "Type": "Cooldown", "Seconds": 1.0 } ],
//           "Children": [ { "Do": "FaceTarget" }, { "Do": "PlayClip", "Params": { "Clip": "Attack" } } ] },
//         { "Type": "Sequence", "Name": "Patrol", "Children": [ { "Do": "MoveToPatrolPoint" }, { "Do": "Wait", "Params": { "Seconds": 1.0 } } ] }
//       ]
//     }
//   }
//
//   - ノード: Selector（子を順に。成功したら成功）/ Sequence（子を順に。失敗したら失敗）/ Parallel（子を同時に。Mode で終わり方を選ぶ）
//     / 葉（"Do": 登録した Task の名前 + "Params"）
//   - Decorator（ノードの "Decorators"）: BlackboardCondition / Condition（登録した判定）/ Cooldown / Inverter / Succeeder / Failer / Repeat / TimeLimit。
//     条件のデコレーターは "Abort"（None / Self / LowerPriority / Both）で、条件が変わったときの中断を指定する
//   - Service（ノードの "Services"）: 登録した Service を Interval 秒ごとに動かす
//   - 子の順が優先順位（上のほうが先に評価される）
//   - 壊れたノード（名前の無い葉・不明な種類など）は、警告して捨てる。ルートが無ければ読み込みは失敗する
//=============================================================================

enum class EBTNodeType : uint8
{
	Selector,
	Sequence,
	Parallel,
	Task,     // 葉
};

// Parallel の終わり方
enum class EBTParallelMode : uint8
{
	AllSuccess,   // 全部が成功したら成功。1 つでも失敗したら失敗（残りは中断）
	AnySuccess,   // どれかが成功したら成功（残りは中断）。全部が失敗したら失敗
	Primary,      // 最初の子の結果が、そのまま結果（ほかの子は、最初の子が動いている間だけ動く）
};

enum class EBTDecoratorType : uint8
{
	BlackboardCondition,   // { Key, Op, Value }
	Condition,             // { Do, Params }（登録した判定）
	Cooldown,              // { Seconds }: 終わってから Seconds 秒は、入れない（失敗）
	Inverter,              // 結果の反転
	Succeeder,             // 常に成功
	Failer,                // 常に失敗
	Repeat,                // { Count, UntilSuccess }
	TimeLimit,             // { Seconds }: Seconds 秒で打ち切って失敗
};

// 条件が変わったときの中断
enum class EBTAbortMode : uint8
{
	None,           // 入るときだけ調べる
	Self,           // 条件が偽になったら、自分の配下の実行を中断して失敗
	LowerPriority,  // 条件が真になったら、自分より優先度の低い（右側の）兄弟の実行を中断して、自分の枝に移る（Selector の子のとき）
	Both,           // Self + LowerPriority
};

namespace NAI
{
	const char* ToString( EBTNodeType Type );
	const char* ToString( EBTParallelMode Mode );
	const char* ToString( EBTDecoratorType Type );
	const char* ToString( EBTAbortMode Mode );
}

struct FBTDecoratorDef
{
	EBTDecoratorType Type = EBTDecoratorType::BlackboardCondition;
	EBTAbortMode Abort = EBTAbortMode::None;
	// BlackboardCondition
	FString Key;
	EAICompareOp Op = EAICompareOp::Equal;
	NlohmannJson Value;
	// Condition
	FAICall Call;
	// Cooldown / TimeLimit（秒）
	float32 Seconds = 0.0f;
	// Repeat: 繰り返す回数（0 は、無限）。UntilSuccess なら、成功するまで（失敗し続けるあいだ）繰り返す
	int32 Count = 0;
	bool bUntilSuccess = false;
	// 条件の結果を反転するか（BlackboardCondition / Condition）
	bool bNot = false;
};

struct FBTServiceDef
{
	FAICall Call;
	// 動かす間隔（秒）。0 以下なら毎フレーム
	float32 Interval = 0.5f;
};

struct FBTNodeDef
{
	FString Name;
	// 親の番号（ルートは -1）
	int32 Parent = -1;
	EBTNodeType Type = EBTNodeType::Task;
	EBTParallelMode ParallelMode = EBTParallelMode::AllSuccess;
	// 葉（Task）: 登録した Task の呼び出し
	FAICall Task;
	TArray<FBTDecoratorDef> Decorators;
	TArray<FBTServiceDef> Services;
	// エディタ用（座標など）。実行側は読まない
	NlohmannJson Editor;
};

struct FBehaviorTreeDefinition
{
	// 使うブラックボードのアセットのパス
	FString Blackboard;
	// すべてのノード。先頭（0）がルート。親の前に子が来る
	TArray<FBTNodeDef> Nodes;
	// エディタ用
	NlohmannJson Editor;

	bool HasRoot() const { return !Nodes.IsEmpty(); }
	// 親 Parent の子の番号（定義の順 = 優先順位）
	TArray<int32> GetChildren( int32 Parent ) const;
	// ノードの表示名（Name があればそれ、無ければ種類・Task の名前と番号）
	FString GetNodeLabel( int32 Index ) const;

	NlohmannJson ToJson() const;
	// JSON から読む。オブジェクトでなければ false。ルートが無くても true（Nodes が空）。問題（捨てた項目など）は Issues に足す
	static bool FromJson( const NlohmannJson& Json, FBehaviorTreeDefinition& OutDefinition, TArray<FAIIssue>* Issues = nullptr );
};
