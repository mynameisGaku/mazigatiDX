// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Interfaces/AI/AIAnimationControl.h"
#include "Library/Core/Interfaces/Tickable/ITickable.h"
#include "Library/Core/Memory/SharedPtr.h"
#include "Library/Core/Memory/UniquePtr.h"
#include "Library/Framework/AI/Blackboard/Blackboard.h"
#include "Library/Framework/AI/StateMachine/StateMachineRuntime.h"
#include "Library/Framework/Component/Component.h"

class CAssetSubsystem;

//=============================================================================
// CStateMachineComponent
//   オブジェクトに付けて、ステートマシン（.gxstate）を動かす部品。毎フレーム、所有者の更新より前（PreUpdate）に動く。
//   ブラックボード（.gxblackboard）は、ステートマシンの定義の Blackboard のパスから読み込む。
//
//   CStateMachineComponent* Machine = Object->AddComponent<CStateMachineComponent>( "AI/Player.gxstate" );
//   Machine->SetAnimationControl( &MyAnimator );            // ゲームのアニメーターを、窓口（IAIAnimationControl）越しに渡す
//   Machine->GetBlackboard()->SetFloat( Machine->GetBlackboard()->FindKey( "Speed" ), 3.0f );
//   Machine->SendEvent( "Damaged" );
//
//   - 最初の Tick で読み込んで始める（AutoStart）。読み込みの問題は、ログと GetIssues に出る。ステートが 1 つも無いなど致命的なら、動かない
//   - ブラックボードは SetBlackboard で外から渡せる。同じオブジェクトの CBehaviorTreeComponent が、同じパスのブラックボードを
//     すでに持っているときは、それを共有する（ビヘイビアツリーと見る値が同じになる）。渡さず共有もしなければ、自分で作る
//   - 共有したとき、Trigger（1 フレームだけ真になるキー）を戻すのは、あとから読み込んだほうだけ（コンポーネントは付けた順に Tick される前提）。
//     手動で決めるなら SetClearsTriggers
//   - 部品（State の処理・Condition）は、ゲームが NAI::RegisterStateAction などで登録しておく（SetRegistry で、別のレジストリも指せる）
//   - フレームの終わりに、ブラックボードの Trigger（1 フレームだけ真になるキー）を戻す
//   - 外す・所有者が壊れるときは、ステートマシンを止める（今いるステートの OnExit が呼ばれる）
//   - メインスレッドで使う
//=============================================================================
class CStateMachineComponent : public CComponent, public ITickable
{
public:
	CStateMachineComponent() = default;
	explicit CStateMachineComponent( const FString& InPath )
		: Path( InPath )
	{
	}
	~CStateMachineComponent() override;

	//-------------------------------------------------------------------------
	// 設定（動き出す前に）
	//-------------------------------------------------------------------------
	void SetPath( const FString& InPath ) { Path = InPath; }
	const FString& GetPath() const { return Path; }
	// 使うレジストリ（nullptr で、アプリ全体の NAI::GetRegistry()）
	void SetRegistry( const CAIRegistry* InRegistry ) { Registry = InRegistry; }
	// ゲームのアニメーターの窓口（nullptr なら、Clip の再生・クリップ終了・出来事は無効）
	void SetAnimationControl( IAIAnimationControl* InAnimation ) { Animation = InAnimation; }
	// ブラックボードを外から渡す（定義は、そのブラックボードのものを使う）。Load の前に呼ぶ
	void SetBlackboard( const TSharedPtr<CBlackboard>& InBlackboard ) { Blackboard = InBlackboard; }
	// false にすると、同じオブジェクトのビヘイビアツリーとブラックボードを共有しない
	void SetShareBlackboard( bool bInShare ) { bShareBlackboard = bInShare; }
	// フレームの終わりに、ブラックボードの Trigger を戻すか（既定は true）
	void SetClearsTriggers( bool bInClears ) { bClearsTriggers = bInClears; }
	// true（既定）なら、最初の Tick で読み込んで始める
	void SetAutoStart( bool bInAutoStart ) { bAutoStart = bInAutoStart; }
	// true の間は Tick しない（ステートも時間も進まない）
	void SetPaused( bool bInPaused ) { bPaused = bInPaused; }
	bool IsPaused() const { return bPaused; }

	//-------------------------------------------------------------------------
	// 読み込みと開始
	//-------------------------------------------------------------------------
	// Path のステートマシンを読み込み、ブラックボードを作り、コンパイルする（まだ始めない）。失敗したら false
	bool Load();
	bool Load( CAssetSubsystem& Assets );
	// コードで作った定義を使う。Blackboard が nullptr のときは、空のブラックボードになる。失敗したら false
	bool SetDefinition( const TSharedPtr<const FStateMachineDefinition>& Definition, const TSharedPtr<const FBlackboardDefinition>& BlackboardDefinition );
	// 読み込み済みなら始める / 止める（OnExit）
	bool Start();
	void Stop();
	bool IsRunning() const;

	//-------------------------------------------------------------------------
	// 実行中の操作・調べる
	//-------------------------------------------------------------------------
	void SendEvent( const FString& Name );
	const TSharedPtr<CBlackboard>& GetBlackboard() const { return Blackboard; }
	CStateMachine* GetStateMachine() const { return Machine.Get(); }
	// 読み込み・コンパイルで出た問題
	const TArray<FAIIssue>& GetIssues() const { return Issues; }
	// 読み込んだ定義の Blackboard のパス（ビヘイビアツリーとの共有の判定に使う）
	const FString& GetBlackboardPath() const { return BlackboardPath; }
	bool ClearsTriggers() const { return bClearsTriggers; }
	// 今の状態の説明（複数行。コンソールの ai.dump 用）: ステートの経路・経過時間・ブラックボードの値・遷移の履歴
	FString Describe() const;

	//-------------------------------------------------------------------------
	// CComponent / ITickable
	//-------------------------------------------------------------------------
	void OnDetach() override;
	void Tick( float32 DeltaTime ) override;
	ETickGroup GetTickGroup() const override { return ETickGroup::PreUpdate; }

private:
	FAIContext MakeContext( float32 DeltaTime );
	bool BuildMachine( const TSharedPtr<const FStateMachineDefinition>& Definition );
	void AdoptPeerBlackboard();

	FString Path;
	FString BlackboardPath;
	const CAIRegistry* Registry = nullptr;
	IAIAnimationControl* Animation = nullptr;
	TSharedPtr<CBlackboard> Blackboard;
	TUniquePtr<CStateMachine> Machine;
	TArray<FAIIssue> Issues;
	float64 Time = 0.0;
	bool bAutoStart = true;
	bool bPaused = false;
	bool bTriedLoad = false;
	bool bShareBlackboard = true;
	bool bClearsTriggers = true;
};
