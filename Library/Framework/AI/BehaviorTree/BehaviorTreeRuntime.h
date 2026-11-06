// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Collection/Array.h"
#include "Library/Core/Memory/SharedPtr.h"
#include "Library/Core/Memory/UniquePtr.h"
#include "Library/Framework/AI/AIContext.h"
#include "Library/Framework/AI/BehaviorTree/BehaviorTreeDefinition.h"
#include "Library/Framework/AI/Blackboard/BlackboardDefinition.h"
#include "Library/Framework/AI/Registry/AIRegistry.h"

//=============================================================================
// CBehaviorTree
//   ビヘイビアツリー（BT）の実行用のインスタンス。FBehaviorTreeDefinition を、部品の名前の解決・ブラックボードのキーの解決を済ませた形に
//   「コンパイル」して持つ。定義は共有するが、部品（Task・Service・判定）と、ノードごとの状態は、インスタンスごとに持つ。
//
//   TUniquePtr<CBehaviorTree> Tree = CBehaviorTree::Create( Definition, NAI::GetRegistry(), Blackboard.GetDefinition(), &Issues );
//   Tree->Tick( Context, DeltaTime );            // 毎フレーム
//
//   実行のしかた:
//   - 毎フレーム根から評価するが、Running のノードの経路を覚えていて、続きから実行する（Selector / Sequence は、実行中の子から再開する）
//   - 各ノードは Success / Failure / Running を返す。根が終わる（Success / Failure）と、次の Tick で最初からやり直す
//   - デコレーター: 入るときの条件（BlackboardCondition / Condition / Cooldown）と、結果を変えるもの（Inverter / Succeeder / Failer / Repeat / TimeLimit）
//   - 中断（条件のデコレーターの Abort）:
//       Self          実行中に条件が偽になったら、自分の配下の実行を中断して失敗（Task の OnAbort が呼ばれる）
//       LowerPriority Selector の子が持つ。条件が偽から真に変わり、その子に入れる状態なら、後ろ（優先度の低い）の兄弟の実行を中断して、その子から実行する
//     ブラックボードのキーは、リビジョンで変化を見る。変わっていないキーの条件は、評価し直さない（登録した判定 Condition は、毎 Tick 評価し直す）
//   - Service: 付いているノードが動いている間、Interval 秒ごとに Tick される（最初の 1 回は、動き始めてすぐ）
//   - 1 回の Tick で動かすノードの数に上限がある（MaxNodeVisitsPerTick）。超えたら警告して、その Tick の残りを打ち切る
//   - 壊れた定義は、問題を Issues に出して、可能なら続行する（未登録の Task は常に失敗、無いキーの条件は常に偽）。根が無ければ nullptr を返す
//   - ゲームのオブジェクトに触るので、メインスレッドで使う
//=============================================================================
class CBehaviorTree
{
public:
	// 1 回の Tick で動かしてよいノードの数
	static constexpr int32 MaxNodeVisitsPerTick = 1024;

	// コンパイルして作る。根が無ければ nullptr（Issues にエラーが出る）。Blackboard は、実行時に渡すブラックボードの定義
	static TUniquePtr<CBehaviorTree> Create( const TSharedPtr<const FBehaviorTreeDefinition>& Definition, const CAIRegistry& Registry, const FBlackboardDefinition& Blackboard, TArray<FAIIssue>* Issues );

	~CBehaviorTree();
	CBehaviorTree( const CBehaviorTree& ) = delete;
	CBehaviorTree& operator=( const CBehaviorTree& ) = delete;

	const FBehaviorTreeDefinition& GetDefinition() const { return *Definition; }

	//-------------------------------------------------------------------------
	// 実行
	//-------------------------------------------------------------------------
	// 1 回進める。根の結果を返す（Running なら、まだ続いている）
	EBTStatus Tick( FAIContext& Context, float32 DeltaTime );
	// 動いているノードをすべて中断する（Task の OnAbort・Service の OnDeactivate）。次の Tick は最初から
	void Abort( FAIContext& Context );

	//-------------------------------------------------------------------------
	// 状態を調べる（デバッグ・テスト・ゲームの判断用）
	//-------------------------------------------------------------------------
	bool IsNodeActive( int32 Node ) const;
	// ノードが最後に終わったときの結果（まだ終わっていなければ Failure）。HasNodeResult で、結果が出たことがあるか
	EBTStatus GetNodeResult( int32 Node ) const;
	bool HasNodeResult( int32 Node ) const;
	// 最後の終わり方が、中断だったか
	bool WasNodeAborted( int32 Node ) const;
	// ノードが最後に終わった時刻（FAIContext::Time）
	float64 GetNodeResultTime( int32 Node ) const;
	// 動いているノードの番号（根から。深さ優先）
	void GetActiveNodes( TArray<int32>& OutNodes ) const;
	// 条件のデコレーターの、最後の評価結果（Decorator は、ノードの Decorators の番号）。評価したことがなければ false
	bool GetDecoratorResult( int32 Node, int32 Decorator ) const;
	// 直前の Tick で動かしたノードの数
	int32 GetLastNumVisits() const { return NumVisits; }
	// 動かすノードの数の上限に当たったことがあるか
	bool HasHitVisitLimit() const { return bHitVisitLimit; }
	// 根の、最後の結果
	EBTStatus GetRootResult() const { return RootResult; }

private:
	struct FCompiledDecorator
	{
		EBTDecoratorType Type = EBTDecoratorType::BlackboardCondition;
		EBTAbortMode Abort = EBTAbortMode::None;
		bool bNot = false;
		// 評価できないもの（無いキー・型に合わない比較・未登録の判定）は、常に偽として扱う
		bool bValid = true;
		FBlackboardKey Key;
		EBlackboardKeyType KeyType = EBlackboardKeyType::Bool;
		EAICompareOp Op = EAICompareOp::Equal;
		FBlackboardValue Compare;
		TSharedPtr<FAICondition> Custom;
		float32 Seconds = 0.0f;
		int32 Count = 0;
		bool bUntilSuccess = false;
		// 実行時の状態
		float64 CooldownEnd = 0.0;
		int32 RepeatCounter = 0;
		uint32 ObservedRevision = 0;
		bool bObserved = false;
		bool bLastCondition = false;
	};

	struct FCompiledService
	{
		TSharedPtr<FAIService> Part;
		float32 Interval = 0.5f;
		float32 Elapsed = 0.0f;
	};

	struct FCompiledNode
	{
		EBTNodeType Type = EBTNodeType::Task;
		EBTParallelMode Mode = EBTParallelMode::AllSuccess;
		int32 Parent = -1;
		TArray<int32> Children;
		TArray<FCompiledDecorator> Decorators;
		TArray<FCompiledService> Services;
		// 葉。未登録なら nullptr（常に失敗）
		TSharedPtr<FAITask> Task;
		// 実行時の状態
		bool bActive = false;
		bool bTaskEntered = false;
		int32 Cursor = 0;
		float32 ActiveTime = 0.0f;
		TArray<uint8> ParallelResults;
		EBTStatus LastResult = EBTStatus::Failure;
		bool bHasResult = false;
		bool bLastAborted = false;
		float64 LastResultTime = 0.0;
	};

	CBehaviorTree() = default;

	bool Compile( const CAIRegistry& Registry, const FBlackboardDefinition& Blackboard, TArray<FAIIssue>* Issues );
	void CompileDecorator( const FBTDecoratorDef& Def, FCompiledDecorator& Out, const CAIRegistry& Registry, const FBlackboardDefinition& Blackboard, const FString& Where, TArray<FAIIssue>* Issues ) const;

	// 実行
	EBTStatus Run( int32 NodeIndex, FAIContext& Context, float32 DeltaTime );
	EBTStatus RunBody( int32 NodeIndex, FAIContext& Context, float32 DeltaTime );
	EBTStatus RunSelector( int32 NodeIndex, FAIContext& Context, float32 DeltaTime );
	EBTStatus RunSequence( int32 NodeIndex, FAIContext& Context, float32 DeltaTime );
	EBTStatus RunParallel( int32 NodeIndex, FAIContext& Context, float32 DeltaTime );
	EBTStatus ApplyResultDecorators( int32 NodeIndex, EBTStatus Status, bool& bOutRestart );

	bool CanEnter( int32 NodeIndex, FAIContext& Context );
	bool EvaluateCondition( FCompiledDecorator& Decorator, FAIContext& Context ) const;   // 評価して、観測したリビジョンと結果を覚える
	bool NeedsReevaluation( const FCompiledDecorator& Decorator, FAIContext& Context ) const;
	bool CheckSelfAbort( int32 NodeIndex, FAIContext& Context );
	// Selector の実行中の子 Cursor より前の子で、LowerPriority の条件が偽から真になり、入れる状態になったものの位置。無ければ -1
	int32 FindLowerPriorityTarget( int32 NodeIndex, FAIContext& Context );
	float32 GetTimeLimit( int32 NodeIndex ) const;

	void Activate( int32 NodeIndex, FAIContext& Context );
	void Deactivate( int32 NodeIndex, FAIContext& Context, bool bAborted, EBTStatus Result );
	void AbortNode( int32 NodeIndex, FAIContext& Context );
	void ResetBody( int32 NodeIndex );
	void TickServices( int32 NodeIndex, FAIContext& Context, float32 DeltaTime );

	TSharedPtr<const FBehaviorTreeDefinition> Definition;
	TArray<FCompiledNode> Nodes;
	int32 NumVisits = 0;
	bool bHitVisitLimit = false;
	EBTStatus RootResult = EBTStatus::Failure;
};
