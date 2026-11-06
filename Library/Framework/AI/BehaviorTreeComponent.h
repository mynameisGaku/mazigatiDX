// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Interfaces/AI/AIAnimationControl.h"
#include "Library/Core/Interfaces/Tickable/ITickable.h"
#include "Library/Core/Memory/SharedPtr.h"
#include "Library/Core/Memory/UniquePtr.h"
#include "Library/Framework/AI/BehaviorTree/BehaviorTreeRuntime.h"
#include "Library/Framework/AI/Blackboard/Blackboard.h"
#include "Library/Framework/Component/Component.h"

class CAssetSubsystem;
struct FAIReloadContext;

//=============================================================================
// CBehaviorTreeComponent
//   オブジェクトに付けて、ビヘイビアツリー（.gxbt）を動かす部品。毎フレーム、所有者の更新より前（PreUpdate）に動く。
//   ブラックボード（.gxblackboard）は、ビヘイビアツリーの定義の Blackboard のパスから読み込む。
//
//   CBehaviorTreeComponent* Brain = Object->AddComponent<CBehaviorTreeComponent>( "AI/Enemy.gxbt" );
//   Brain->GetBlackboard()->SetBool( Brain->GetBlackboard()->FindKey( "HasTarget" ), true );
//
//   - 最初の Tick で読み込んで始める（AutoStart）。読み込みの問題は、ログと GetIssues に出る。ルートが無いなど致命的なら、動かない
//   - ブラックボードは SetBlackboard で外から渡せる。同じオブジェクトの CStateMachineComponent が、同じパスのブラックボードを
//     すでに持っているときは、それを共有する（ステートマシンと見る値が同じになる）
//   - 共有したとき、Trigger（1 フレームだけ真になるキー）を戻すのは、あとから読み込んだほうだけ（コンポーネントは付けた順に Tick される前提）。
//     手動で決めるなら SetClearsTriggers
//   - 部品（Task・Service・判定）は、ゲームが NAI::RegisterTask などで登録しておく（標準の Task は最初から）。SetRegistry で別のレジストリも指せる
//   - 外す・所有者が壊れるときは、動いているノードを中断する（Task の OnAbort）
//   - メインスレッドで使う
//=============================================================================
class CBehaviorTreeComponent : public CComponent, public ITickable
{
public:
	CBehaviorTreeComponent() = default;
	explicit CBehaviorTreeComponent( const FString& InPath )
		: Path( InPath )
	{
	}
	~CBehaviorTreeComponent() override;

	//-------------------------------------------------------------------------
	// 設定（動き出す前に）
	//-------------------------------------------------------------------------
	void SetPath( const FString& InPath ) { Path = InPath; }
	const FString& GetPath() const { return Path; }
	// 使うレジストリ（nullptr で、アプリ全体の NAI::GetRegistry()）
	void SetRegistry( const CAIRegistry* InRegistry ) { Registry = InRegistry; }
	// ゲームのアニメーターの窓口（Task が、文脈の Animation 越しに使う）
	void SetAnimationControl( IAIAnimationControl* InAnimation ) { Animation = InAnimation; }
	// ブラックボードを外から渡す（定義は、そのブラックボードのものを使う）。Load の前に呼ぶ
	void SetBlackboard( const TSharedPtr<CBlackboard>& InBlackboard ) { Blackboard = InBlackboard; }
	// false にすると、同じオブジェクトのステートマシンとブラックボードを共有しない
	void SetShareBlackboard( bool bInShare ) { bShareBlackboard = bInShare; }
	// フレームの終わりに、ブラックボードの Trigger を戻すか（既定は true）
	void SetClearsTriggers( bool bInClears ) { bClearsTriggers = bInClears; }
	// true（既定）なら、最初の Tick で読み込んで始める
	void SetAutoStart( bool bInAutoStart ) { bAutoStart = bInAutoStart; }
	// true の間は Tick しない（ノードも時間も進まない）
	void SetPaused( bool bInPaused ) { bPaused = bInPaused; }
	bool IsPaused() const { return bPaused; }

	//-------------------------------------------------------------------------
	// 読み込みと開始
	//-------------------------------------------------------------------------
	// Path のビヘイビアツリーを読み込み、ブラックボードを用意し、コンパイルする（まだ始めない）。失敗したら false
	bool Load();
	bool Load( CAssetSubsystem& Assets );
	// 保存し直したアセットを読み直して、動いたまま差し替える（ホットリロード。AIHotReload.h）。読めなければ、元のまま動き続けて false。
	// ブラックボードは作り直して値を引き継ぎ、動いていたツリーは、最初から動かし直す。Context は、同じオブジェクトの部品どうしでブラックボードを共有するための入れ物
	bool Reload( FAIReloadContext& Context );
	bool Reload( CAssetSubsystem& Assets, FAIReloadContext& Context );
	// コードで作った定義を使う。Blackboard が nullptr のときは、空のブラックボードになる。失敗したら false
	bool SetDefinition( const TSharedPtr<const FBehaviorTreeDefinition>& Definition, const TSharedPtr<const FBlackboardDefinition>& BlackboardDefinition );
	// 読み込み済みなら始める / 止める（動いているノードを中断する）
	bool Start();
	void Stop();
	bool IsRunning() const { return bRunning; }

	//-------------------------------------------------------------------------
	// 調べる
	//-------------------------------------------------------------------------
	const TSharedPtr<CBlackboard>& GetBlackboard() const { return Blackboard; }
	CBehaviorTree* GetBehaviorTree() const { return Tree.Get(); }
	// 読み込み・コンパイルで出た問題
	const TArray<FAIIssue>& GetIssues() const { return Issues; }
	// 読み込んだ定義の Blackboard のパス（ステートマシンとの共有の判定に使う）
	const FString& GetBlackboardPath() const { return BlackboardPath; }
	bool ClearsTriggers() const { return bClearsTriggers; }
	// コンポーネントが動いた時間の合計（秒。一時停止の間は進まない。ノードの結果の時刻と同じ）
	float64 GetTime() const { return Time; }
	// 今の状態の説明（複数行。コンソールの ai.dump 用）: 動いているノード・ルートの結果・ブラックボードの値
	FString Describe() const;

	//-------------------------------------------------------------------------
	// CComponent / ITickable
	//-------------------------------------------------------------------------
	void OnDetach() override;
	// 一時停止中でも、1 回だけ進める（デバッガのステップ実行用。読み込んで始める前なら、何もしない）
	void Step( float32 DeltaTime );
	void Tick( float32 DeltaTime ) override;
	ETickGroup GetTickGroup() const override { return ETickGroup::PreUpdate; }

private:
	FAIContext MakeContext( float32 DeltaTime );
	void Update( float32 DeltaTime );
	bool BuildTree( const TSharedPtr<const FBehaviorTreeDefinition>& Definition );
	void AdoptPeerBlackboard();

	FString Path;
	FString BlackboardPath;
	const CAIRegistry* Registry = nullptr;
	IAIAnimationControl* Animation = nullptr;
	TSharedPtr<CBlackboard> Blackboard;
	TUniquePtr<CBehaviorTree> Tree;
	TArray<FAIIssue> Issues;
	float64 Time = 0.0;
	bool bRunning = false;
	bool bAutoStart = true;
	bool bPaused = false;
	bool bTriedLoad = false;
	bool bShareBlackboard = true;
	bool bClearsTriggers = true;
};
