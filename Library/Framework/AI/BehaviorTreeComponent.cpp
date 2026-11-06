// SPDX-License-Identifier: MIT
#include "BehaviorTreeComponent.h"

#include "Library/Core/Log/Log.h"
#include "Library/Core/Profiler/ProfilerScope.h"
#include "Library/Core/Thread/GameThread.h"
#include "Library/Framework/AI/AIBlackboardSetup.h"
#include "Library/Framework/AI/AIHotReload.h"
#include "Library/Framework/AI/AILog.h"
#include "Library/Framework/AI/StateMachineComponent.h"
#include "Library/Framework/Asset/AssetSubsystem.h"
#include "Library/Framework/Engine/Engine.h"
#include "Library/Framework/Object/GameObject.h"

CBehaviorTreeComponent::~CBehaviorTreeComponent() = default;

bool CBehaviorTreeComponent::Load()
{
	CAssetSubsystem* Assets = GetAppSubsystem<CAssetSubsystem>();
	if ( Assets == nullptr )
	{
		LOG( LogAI, Error, "BehaviorTree '%s': no asset subsystem is available.", *Path );
		bTriedLoad = true;
		return false;
	}
	return Load( *Assets );
}

bool CBehaviorTreeComponent::Load( CAssetSubsystem& Assets )
{
	bTriedLoad = true;
	Stop();
	Tree.Reset();
	Issues.Empty();
	if ( Path.IsEmpty() )
	{
		LOG( LogAI, Error, "BehaviorTree: no path was set." );
		return false;
	}
	const TSharedPtr<CBehaviorTreeAsset> Asset = Assets.LoadBehaviorTree( Path );
	if ( !Asset.IsValid() )
	{
		return false;
	}

	// ブラックボード: 外から渡されていればそれ、同じパスを持つ相手（ステートマシン）がいればそれ、無ければ定義のパスから作る
	BlackboardPath = Asset->GetDefinition().Blackboard;
	if ( !Blackboard.IsValid() )
	{
		AdoptPeerBlackboard();
	}
	if ( !Blackboard.IsValid() )
	{
		Blackboard = NAI::CreateBlackboard( Assets, BlackboardPath, Path );
	}
	Issues = Asset->GetLoadIssues();
	return BuildTree( Asset->GetDefinitionShared() );
}

void CBehaviorTreeComponent::AdoptPeerBlackboard()
{
	if ( !bShareBlackboard || BlackboardPath.IsEmpty() || GetOwner() == nullptr )
	{
		return;
	}
	CStateMachineComponent* Peer = GetOwner()->GetComponent<CStateMachineComponent>();
	if ( Peer != nullptr && Peer->GetBlackboard().IsValid() && Peer->GetBlackboardPath() == BlackboardPath )
	{
		Blackboard = Peer->GetBlackboard();
		// あとから読み込んだほうが、Trigger を戻す（先に読み込んだほうは、戻さない）
		Peer->SetClearsTriggers( false );
		bClearsTriggers = true;
	}
}

bool CBehaviorTreeComponent::SetDefinition( const TSharedPtr<const FBehaviorTreeDefinition>& Definition, const TSharedPtr<const FBlackboardDefinition>& BlackboardDefinition )
{
	bTriedLoad = true;
	Stop();
	Tree.Reset();
	Issues.Empty();
	if ( !Blackboard.IsValid() )
	{
		Blackboard = MakeShared<CBlackboard>( BlackboardDefinition );
	}
	return BuildTree( Definition );
}

bool CBehaviorTreeComponent::Reload( FAIReloadContext& Context )
{
	CAssetSubsystem* Assets = GetAppSubsystem<CAssetSubsystem>();
	return Assets != nullptr && Reload( *Assets, Context );
}

bool CBehaviorTreeComponent::Reload( CAssetSubsystem& AssetSubsystem, FAIReloadContext& Context )
{
	GAME_THREAD_ONLY( false );
	CAssetSubsystem* Assets = &AssetSubsystem;
	if ( Path.IsEmpty() || !bTriedLoad )
	{
		return false;
	}
	// 読めなければ、元のまま動き続ける
	const TSharedPtr<CBehaviorTreeAsset> Asset = Assets->LoadBehaviorTree( Path );
	if ( !Asset.IsValid() )
	{
		return false;
	}
	const bool bWasRunning = IsRunning();
	Stop();

	// ブラックボード: 同じオブジェクトの相手が作り直したものがあれば共有し、無ければ作り直して、キー名と型が同じ値を引き継ぐ
	const FString NewBlackboardPath = Asset->GetDefinition().Blackboard;
	if ( bShareBlackboard && Context.Blackboard.IsValid() && Context.BlackboardPath == NewBlackboardPath )
	{
		Blackboard = Context.Blackboard;
	}
	else
	{
		TSharedPtr<CBlackboard> Fresh = NAI::CreateBlackboard( *Assets, NewBlackboardPath, Path );
		if ( Fresh.IsValid() )
		{
			if ( Blackboard.IsValid() )
			{
				Fresh->CopyMatchingValuesFrom( *Blackboard );
			}
			Blackboard = Fresh;
			if ( bShareBlackboard )
			{
				Context.BlackboardPath = NewBlackboardPath;
				Context.Blackboard = Fresh;
			}
		}
	}
	BlackboardPath = NewBlackboardPath;
	Issues = Asset->GetLoadIssues();
	Tree.Reset();
	const bool bBuilt = BuildTree( Asset->GetDefinitionShared() );
	if ( bBuilt && bWasRunning )
	{
		Start();
	}
	return bBuilt;
}

bool CBehaviorTreeComponent::BuildTree( const TSharedPtr<const FBehaviorTreeDefinition>& Definition )
{
	TArray<FAIIssue> CompileIssues;
	const CAIRegistry& UsedRegistry = Registry != nullptr ? *Registry : NAI::GetRegistry();
	Tree = CBehaviorTree::Create( Definition, UsedRegistry, Blackboard->GetDefinition(), &CompileIssues );
	NAI::LogIssues( Path, CompileIssues );
	Issues.Append( CompileIssues );
	return Tree.IsValid();
}

FAIContext CBehaviorTreeComponent::MakeContext( float32 DeltaTime )
{
	FAIContext Context;
	Context.Owner = GetOwner();
	Context.Scene = GetOwner() != nullptr ? GetOwner()->GetScene() : nullptr;
	Context.Blackboard = Blackboard.Get();
	Context.Animation = Animation;
	Context.DeltaTime = DeltaTime;
	Context.Time = Time;
	return Context;
}

bool CBehaviorTreeComponent::Start()
{
	GAME_THREAD_ONLY( false );
	if ( !Tree.IsValid() )
	{
		return false;
	}
	bRunning = true;
	return true;
}

void CBehaviorTreeComponent::Stop()
{
	GAME_THREAD_ONLY();
	if ( Tree.IsValid() && bRunning )
	{
		FAIContext Context = MakeContext( 0.0f );
		Tree->Abort( Context );
	}
	bRunning = false;
}

void CBehaviorTreeComponent::OnDetach()
{
	Stop();
}

void CBehaviorTreeComponent::Tick( float32 DeltaTime )
{
	if ( bPaused )
	{
		return;
	}
	if ( !Tree.IsValid() )
	{
		// 最初の Tick で 1 回だけ読み込んで始める（失敗しても、毎フレーム読み直さない）
		if ( !bAutoStart || bTriedLoad || Path.IsEmpty() || !Load() )
		{
			return;
		}
	}
	if ( !bRunning && bAutoStart )
	{
		Start();
	}
	if ( !bRunning )
	{
		return;
	}
	Update( DeltaTime );
}

void CBehaviorTreeComponent::Step( float32 DeltaTime )
{
	if ( Tree.IsValid() && bRunning )
	{
		Update( DeltaTime );
	}
}

void CBehaviorTreeComponent::Update( float32 DeltaTime )
{
	PROFILE_SCOPE_CAT( "AI", "BehaviorTree" );
	Time += static_cast<float64>( DeltaTime );
	FAIContext Context = MakeContext( DeltaTime );
	Tree->Tick( Context, DeltaTime );
	if ( bClearsTriggers )
	{
		Blackboard->ClearTriggers();
	}
}

FString CBehaviorTreeComponent::Describe() const
{
	FString Text = FString::Printf( "BehaviorTree '%s'", *Path );
	if ( !Tree.IsValid() )
	{
		return Text + " (not loaded)";
	}
	const char* RootResult = Tree->GetRootResult() == EBTStatus::Success ? "success" : ( Tree->GetRootResult() == EBTStatus::Running ? "running" : "failure" );
	Text += FString::Printf( " %s, root %s, %d node(s) visited", bRunning ? "running" : "stopped", RootResult, Tree->GetLastNumVisits() );
	if ( bPaused )
	{
		Text += " (paused)";
	}

	TArray<int32> Active;
	Tree->GetActiveNodes( Active );
	Text += "\n  active:";
	if ( Active.IsEmpty() )
	{
		Text += " (none)";
	}
	for ( const int32 Node : Active )
	{
		Text += " " + Tree->GetDefinition().GetNodeLabel( Node );
	}
	if ( Blackboard.IsValid() )
	{
		Text += "\n  blackboard:" + NAI::DescribeBlackboard( *Blackboard, "\n    " );
	}
	return Text;
}
