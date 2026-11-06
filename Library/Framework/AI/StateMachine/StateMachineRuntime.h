// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Collection/Array.h"
#include "Library/Core/Memory/SharedPtr.h"
#include "Library/Core/Memory/UniquePtr.h"
#include "Library/Framework/AI/AIContext.h"
#include "Library/Framework/AI/Blackboard/BlackboardDefinition.h"
#include "Library/Framework/AI/Registry/AIRegistry.h"
#include "Library/Framework/AI/StateMachine/StateMachineDefinition.h"

//=============================================================================
// CStateMachine
//   ステートマシン（FSM）の実行用のインスタンス。FStateMachineDefinition を、部品の名前の解決・ブラックボードのキーの解決を済ませた形に
//   「コンパイル」して持つ。定義は共有するが、部品（State の処理・Condition）はインスタンスごとに作る。
//
//   TUniquePtr<CStateMachine> Machine = CStateMachine::Create( Definition, NAI::GetRegistry(), Blackboard.GetDefinition(), &Issues );
//   Machine->Start( Context );
//   Machine->SendEvent( "Damaged" );
//   Machine->Tick( Context, DeltaTime );            // 毎フレーム
//
//   毎フレームの動き（Tick）:
//     1. 各ステートの経過時間を進める
//     2. 遷移を探す: 最上位の Any State 遷移 -> 現在のステートの遷移（葉から親へ）。同じ一覧の中では Priority の大きい順、同じなら定義の順
//     3. あれば、離れるステートの OnExit（葉から）-> 入るステートの OnEnter（親から）を呼ぶ。入るステートに Clip があれば再生する
//     4. 同じ Tick の中で、続けて遷移してよい回数に上限がある（MaxTransitionsPerTick）。同じ遷移は 1 回の Tick で 2 度起きない。
//        イベントの条件で起きた遷移は、そのイベントを消費する（同じイベントでもう 1 度遷移しない）
//     5. いるステートの OnTick と、アニメーションの出来事への反応（OnClipEvent）を呼ぶ。イベントはこの Tick の終わりに消える
//
//   - 壊れた定義は、問題を Issues に出して、可能なら続行する（未登録の部品・無いキーの条件は「無効」。条件は偽として扱う）。
//     致命的（ステートが 1 つも無い・入口が無い・マシンの子が無い）なら nullptr を返す
//   - ゲームのオブジェクトに触るので、メインスレッドで使う
//=============================================================================

// 遷移の起き方（履歴用）
enum class EStateTransitionCause : uint8
{
	Initial,       // Start で入口へ入った
	Transition,    // マシンの子の間の遷移
	AnyTransition, // Any State 遷移
};

struct FStateHistoryEntry
{
	float64 Time = 0.0;
	// 離れたステート・入ったステートの番号（葉。最初は From が -1）
	int32 From = -1;
	int32 To = -1;
	EStateTransitionCause Cause = EStateTransitionCause::Initial;
	// 起こした遷移の説明（"Idle -> Run when MoveAxis != 0"）
	FString Description;
};

class CStateMachine
{
public:
	// 1 回の Tick で、続けて遷移してよい回数
	static constexpr int32 MaxTransitionsPerTick = 8;
	// 履歴に残す数
	static constexpr int32 MaxHistory = 16;
	// 溜めておけるイベントの数
	static constexpr int32 MaxPendingEvents = 16;

	// コンパイルして作る。致命的な問題があれば nullptr（Issues にエラーが出る）。Blackboard は、実行時に渡すブラックボードの定義（キーの番号を解決する）
	static TUniquePtr<CStateMachine> Create( const TSharedPtr<const FStateMachineDefinition>& Definition, const CAIRegistry& Registry, const FBlackboardDefinition& Blackboard, TArray<FAIIssue>* Issues );

	~CStateMachine();
	CStateMachine( const CStateMachine& ) = delete;
	CStateMachine& operator=( const CStateMachine& ) = delete;

	const FStateMachineDefinition& GetDefinition() const { return *Definition; }

	//-------------------------------------------------------------------------
	// 実行
	//-------------------------------------------------------------------------
	// 入口のステートへ入る（OnEnter・Clip）。すでに動いていれば何もしない
	void Start( FAIContext& Context );
	// 動いているすべてのステートから出る（OnExit）。止める
	void Stop( FAIContext& Context );
	void Tick( FAIContext& Context, float32 DeltaTime );
	// イベントを送る。次の Tick の遷移の条件（Event）で使われ、その Tick の終わりに消える。溜めすぎると捨てる
	void SendEvent( const FString& Name );

	bool IsRunning() const { return bRunning; }

	//-------------------------------------------------------------------------
	// 状態を調べる（デバッグ・テスト・ゲームの判断用）
	//-------------------------------------------------------------------------
	// 今のステートの経路（最上位から葉へ。定義のステートの番号）
	const TArray<int32>& GetActivePath() const { return ActivePath; }
	// "Locomotion/Jump/Rise" のような、今のステートの経路の文字列。止まっていれば空
	FString DescribeActiveState() const;
	// 経路 Path（"Locomotion" や "Locomotion/Jump"）のステートの中にいるか（その子孫にいてもよい）
	bool IsInState( const FString& Path ) const;
	// 今の葉のステートに入ってからの秒数。止まっていれば 0
	float32 GetLeafStateTime() const;
	// 今のステートの経路の、depth 番目（0 が最上位）のステートに入ってからの秒数
	float32 GetStateTime( int32 Depth ) const;
	// 遷移の履歴（古い順）
	void GetHistory( TArray<FStateHistoryEntry>& OutHistory ) const;
	// 同じ Tick の連鎖の上限に当たったことがあるか
	bool HasHitTransitionLimit() const { return bHitTransitionLimit; }

private:
	struct FCompiledCondition
	{
		EStateConditionType Type = EStateConditionType::BlackboardCompare;
		bool bNot = false;
		// 評価できないもの（未登録の部品・無いキー・型に合わない比較）は、常に偽として扱う
		bool bValid = true;
		FBlackboardKey Key;
		EBlackboardKeyType KeyType = EBlackboardKeyType::Bool;
		EAICompareOp Op = EAICompareOp::Equal;
		FBlackboardValue Compare;
		FString Event;
		float32 Seconds = 0.0f;
		TSharedPtr<FAICondition> Custom;
	};

	struct FCompiledTransition
	{
		// 持ち主のマシン（-1 は最上位）と、元の子（-1 はどの子からでも）
		int32 Machine = -1;
		int32 FromChild = -1;
		int32 Target = -1;
		TArray<FCompiledCondition> Conditions;
		bool bAnyOf = false;
		bool bAnyTransition = false;
		int32 Priority = 0;
		float32 MinTime = 0.0f;
		FString Description;
	};

	struct FCompiledEventAction
	{
		FString Event;
		TSharedPtr<FAIStateAction> Action;
	};

	struct FCompiledState
	{
		int32 Parent = -1;
		int32 Entry = -1;
		FString Clip;
		TArray<TSharedPtr<FAIStateAction>> OnEnter;
		TArray<TSharedPtr<FAIStateAction>> OnTick;
		TArray<TSharedPtr<FAIStateAction>> OnExit;
		TArray<FCompiledEventAction> OnClipEvent;
		// このマシンの遷移（優先度の順）。Any State 遷移と、From の指定のある遷移に分けて持つ
		TArray<int32> AnyTransitions;
		TArray<int32> Transitions;
	};

	CStateMachine() = default;

	// コンパイル
	bool Compile( const CAIRegistry& Registry, const FBlackboardDefinition& Blackboard, TArray<FAIIssue>* Issues );
	TSharedPtr<FAIStateAction> CompileAction( const FAICall& Call, const CAIRegistry& Registry, const FString& Where, TArray<FAIIssue>* Issues ) const;
	void CompileTransitions( int32 Machine, const TArray<FStateTransitionDef>& Defs, bool bAnyTransitions, const CAIRegistry& Registry, const FBlackboardDefinition& Blackboard, const FString& Where, TArray<FAIIssue>* Issues );
	void CompileCondition( const FStateConditionDef& Def, FCompiledCondition& Out, const CAIRegistry& Registry, const FBlackboardDefinition& Blackboard, const FString& Where, TArray<FAIIssue>* Issues ) const;
	int32 ResolveEntry( int32 Machine, const FString& EntryName, const FString& Where, TArray<FAIIssue>* Issues, bool& bFatal ) const;
	void MarkReachable( TArray<bool>& Reachable, int32 State ) const;

	// 実行
	bool FindTransition( FAIContext& Context, int32& OutTransition );
	bool EvaluateList( const TArray<int32>& List, int32 ActiveChild, int32 Level, FAIContext& Context, int32& OutTransition );
	bool EvaluateTransition( const FCompiledTransition& Transition, int32 Level, FAIContext& Context ) const;
	bool EvaluateCondition( const FCompiledCondition& Condition, int32 Level, FAIContext& Context ) const;
	void PerformTransition( int32 TransitionId, FAIContext& Context );
	// 状態へ入る（OnEnter・Clip）。bDescend なら、マシンの入口の子の葉まで続けて入る
	void EnterState( int32 State, FAIContext& Context, bool bDescend );
	void ExitDownTo( int32 Level, FAIContext& Context );
	void AddHistory( EStateTransitionCause Cause, int32 From, int32 To, const FString& Description, const FAIContext& Context );
	bool HasPendingEvent( const FString& Name ) const;
	void ConsumeEvent( const FString& Name );

	TSharedPtr<const FStateMachineDefinition> Definition;
	// 定義のステートと同じ番号
	TArray<FCompiledState> States;
	TArray<FCompiledTransition> CompiledTransitions;
	// 最上位（-1）のマシンの情報
	int32 RootEntry = -1;
	TArray<int32> RootAnyTransitions;
	TArray<int32> RootTransitions;

	// 実行時の状態
	bool bRunning = false;
	TArray<int32> ActivePath;
	TArray<float32> ActiveTimes;
	TArray<FString> PendingEvents;
	TArray<int32> FiredThisTick;
	FStateHistoryEntry History[MaxHistory];
	int32 HistoryCount = 0;
	int32 HistoryHead = 0;
	bool bHitTransitionLimit = false;
	bool bWarnedEventOverflow = false;
};
