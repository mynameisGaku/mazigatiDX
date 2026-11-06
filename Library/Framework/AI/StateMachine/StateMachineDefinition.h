// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Collection/Array.h"
#include "Library/Core/Collection/String.h"
#include "Library/Framework/AI/AIIssue.h"
#include "Library/Framework/AI/Blackboard/BlackboardCompare.h"
#include "Library/Json/JsonObject.h"

//=============================================================================
// ステートマシンの定義（.gxstate）
//   有限状態マシン（FSM）を、データとして定義する。不変のデータなので、同じ定義を使う全インスタンスで共有してよい。
//   階層に対応: ステートは「葉」か、子のステートを持つ「Machine」（例: Jump が Rise / Apex / Fall を持つ）。
//
//   {
//     "Version": 1,
//     "Blackboard": "AI/Player.gxblackboard",
//     "Entry": "Locomotion",
//     "States": [
//       { "Name": "Locomotion", "Entry": "Idle",
//         "States": [ { "Name": "Idle", "Clip": "Idle" }, { "Name": "Run", "Clip": "Run" } ],
//         "Transitions": [
//           { "From": "Idle", "To": "Run", "When": [ { "Key": "MoveAxis", "Op": "!=", "Value": 0 } ] },
//           { "From": "Run", "To": "Idle", "When": [ { "Key": "MoveAxis", "Op": "==", "Value": 0 } ] } ] },
//       { "Name": "Hurt", "Clip": "Hurt", "OnEnter": [ { "Do": "StopMoving" } ] }
//     ],
//     "AnyTransitions": [ { "To": "Hurt", "On": "Damaged", "Priority": 50 } ]
//   }
//
//   - 遷移の条件（When の 1 つ）: ブラックボードの比較 { Key, Op, Value }、イベント { Event }、クリップ終了 { ClipFinished: true }、
//     ステートに入ってからの時間 { Time: 0.4 }（以上）、登録した判定 { Do, Params }。どれも "Not": true で反転できる
//   - When は全部が真（AND）。"Mode": "Any" にすると、どれかが真（OR）。"On": "名前" は { Event: "名前" } の短い書き方
//   - 遷移は Priority（大きいほど先）、同じなら書いた順に評価する。From を省くと、そのマシンの子のどれからでも（Any State）
//   - To は同じマシンの子の名前。"A/B" のように書くと、入れ子の子へ直接入る
//   - 状態は平らな配列（Parent が親の番号）で持つ。JSON では入れ子に書く。Editor 欄は実行側は読まない（エディタの座標など）
//=============================================================================

// 部品の呼び出し（登録した名前とパラメータ）
struct FAICall
{
	FString Do;
	// パラメータ（オブジェクト。無ければ null）
	NlohmannJson Params;
};

enum class EStateConditionType : uint8
{
	BlackboardCompare,   // { Key, Op, Value }
	Event,               // { Event }
	ClipFinished,        // { ClipFinished: true }
	ElapsedTime,         // { Time }
	Custom,              // { Do, Params }
};

struct FStateConditionDef
{
	EStateConditionType Type = EStateConditionType::BlackboardCompare;
	// 結果を反転するか
	bool bNot = false;
	// BlackboardCompare
	FString Key;
	EAICompareOp Op = EAICompareOp::Equal;
	NlohmannJson Value;
	// Event
	FString Event;
	// ElapsedTime（秒）
	float32 Seconds = 0.0f;
	// Custom
	FAICall Call;
};

struct FStateTransitionDef
{
	// 元の子の名前。空なら、そのマシンの子のどれからでも
	FString From;
	FString To;
	TArray<FStateConditionDef> When;
	// When を OR で結ぶか（既定は AND）
	bool bAnyOf = false;
	int32 Priority = 0;
	// 元のステートに入ってから、この秒数が過ぎるまでは遷移しない
	float32 MinTime = 0.0f;
};

// アニメーションの出来事に反応する処理
struct FStateClipEventDef
{
	FString Event;
	FAICall Call;
};

struct FStateDef
{
	FString Name;
	// 親の番号（-1 は最上位）
	int32 Parent = -1;
	// 子のステートを持つか
	bool bMachine = false;
	// bMachine のとき、入ったら最初に入る子の名前
	FString Entry;
	// 入ったときに再生するクリップ（空なら再生しない）
	FString Clip;
	// bMachine のとき、子の間の遷移
	TArray<FStateTransitionDef> Transitions;
	// bMachine のとき、子のどれからでも起きる遷移（From は無視される）
	TArray<FStateTransitionDef> AnyTransitions;
	TArray<FAICall> OnEnter;
	TArray<FAICall> OnTick;
	TArray<FAICall> OnExit;
	TArray<FStateClipEventDef> OnClipEvent;
	// エディタ用（座標など）。実行側は読まない
	NlohmannJson Editor;
};

struct FStateMachineDefinition
{
	// 使うブラックボードのアセットのパス
	FString Blackboard;
	// 最上位で最初に入るステートの名前
	FString Entry;
	// 最上位のステートの間の遷移（From で元を指す。From を省くと、最上位のどれからでも）
	TArray<FStateTransitionDef> Transitions;
	// 最上位のステートのどれからでも起きる遷移（入れ子の中のステートからも）
	TArray<FStateTransitionDef> AnyTransitions;
	// すべてのステート（親の前に子が来る順。最上位は Parent が -1）
	TArray<FStateDef> States;
	// エディタ用
	NlohmannJson Editor;

	// 親 Parent（-1 は最上位）の子で、名前がそれのステートの番号。無ければ -1
	int32 FindChild( int32 Parent, const FString& Name ) const;
	// "A/B" のような経路で、親 Parent から辿ったステートの番号。無ければ -1
	int32 FindByPath( int32 Parent, const FString& Path ) const;
	// 親 Parent の子の番号（定義の順）
	TArray<int32> GetChildren( int32 Parent ) const;
	// 最上位から index までの経路（"Locomotion/Jump/Rise"）
	FString GetPath( int32 Index ) const;

	NlohmannJson ToJson() const;
	// JSON から読む。オブジェクトでなければ false。問題（捨てた項目など）は Issues に足す
	static bool FromJson( const NlohmannJson& Json, FStateMachineDefinition& OutDefinition, TArray<FAIIssue>* Issues = nullptr );
};
