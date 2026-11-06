// SPDX-License-Identifier: MIT
#include "StateMachineComponent.h"

#include "Library/Core/Log/Log.h"
#include "Library/Core/Thread/GameThread.h"
#include "Library/Framework/AI/AILog.h"
#include "Library/Framework/Asset/AssetSubsystem.h"
#include "Library/Framework/Engine/Engine.h"
#include "Library/Framework/Object/GameObject.h"
#include "Library/Core/Profiler/ProfilerScope.h"

CStateMachineComponent::~CStateMachineComponent() = default;

bool CStateMachineComponent::Load()
{
	CAssetSubsystem* Assets = GetAppSubsystem<CAssetSubsystem>();
	if ( Assets == nullptr )
	{
		LOG( LogAI, Error, "StateMachine '%s': no asset subsystem is available.", *Path );
		bTriedLoad = true;
		return false;
	}
	return Load( *Assets );
}

bool CStateMachineComponent::Load( CAssetSubsystem& Assets )
{
	bTriedLoad = true;
	Machine.Reset();
	Issues.Empty();
	if ( Path.IsEmpty() )
	{
		LOG( LogAI, Error, "StateMachine: no path was set." );
		return false;
	}
	const TSharedPtr<CStateMachineAsset> Asset = Assets.LoadStateMachine( Path );
	if ( !Asset.IsValid() )
	{
		return false;
	}

	// ブラックボード: 外から渡されていればそれ、無ければ定義の Blackboard のパスから読み込む（無ければ空）
	if ( !Blackboard.IsValid() )
	{
		TSharedPtr<const FBlackboardDefinition> BlackboardDefinition;
		if ( !Asset->GetDefinition().Blackboard.IsEmpty() )
		{
			if ( const TSharedPtr<CBlackboardAsset> BlackboardAsset = Assets.LoadBlackboard( Asset->GetDefinition().Blackboard ) )
			{
				BlackboardDefinition = BlackboardAsset->GetDefinitionShared();
			}
			else
			{
				LOG( LogAI, Warning, "StateMachine '%s': the blackboard '%s' could not be loaded. An empty blackboard is used.", *Path, *Asset->GetDefinition().Blackboard );
			}
		}
		Blackboard = MakeShared<CBlackboard>( BlackboardDefinition );
	}
	Issues = Asset->GetLoadIssues();
	return BuildMachine( Asset->GetDefinitionShared() );
}

bool CStateMachineComponent::SetDefinition( const TSharedPtr<const FStateMachineDefinition>& Definition, const TSharedPtr<const FBlackboardDefinition>& BlackboardDefinition )
{
	bTriedLoad = true;
	Machine.Reset();
	Issues.Empty();
	if ( !Blackboard.IsValid() )
	{
		Blackboard = MakeShared<CBlackboard>( BlackboardDefinition );
	}
	return BuildMachine( Definition );
}

bool CStateMachineComponent::BuildMachine( const TSharedPtr<const FStateMachineDefinition>& Definition )
{
	TArray<FAIIssue> CompileIssues;
	const CAIRegistry& UsedRegistry = Registry != nullptr ? *Registry : NAI::GetRegistry();
	Machine = CStateMachine::Create( Definition, UsedRegistry, Blackboard->GetDefinition(), &CompileIssues );
	NAI::LogIssues( Path, CompileIssues );
	Issues.Append( CompileIssues );
	return Machine.IsValid();
}

FAIContext CStateMachineComponent::MakeContext( float32 DeltaTime )
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

bool CStateMachineComponent::Start()
{
	GAME_THREAD_ONLY( false );
	if ( !Machine.IsValid() )
	{
		return false;
	}
	FAIContext Context = MakeContext( 0.0f );
	Machine->Start( Context );
	return true;
}

void CStateMachineComponent::Stop()
{
	GAME_THREAD_ONLY();
	if ( Machine.IsValid() && Machine->IsRunning() )
	{
		FAIContext Context = MakeContext( 0.0f );
		Machine->Stop( Context );
	}
}

bool CStateMachineComponent::IsRunning() const
{
	return Machine.IsValid() && Machine->IsRunning();
}

void CStateMachineComponent::SendEvent( const FString& Name )
{
	if ( Machine.IsValid() )
	{
		Machine->SendEvent( Name );
	}
}

void CStateMachineComponent::OnDetach()
{
	Stop();
}

void CStateMachineComponent::Tick( float32 DeltaTime )
{
	if ( bPaused )
	{
		return;
	}
	if ( !Machine.IsValid() )
	{
		// 最初の Tick で 1 回だけ読み込んで始める（失敗しても、毎フレーム読み直さない）
		if ( !bAutoStart || bTriedLoad || Path.IsEmpty() )
		{
			return;
		}
		if ( !Load() )
		{
			return;
		}
	}
	if ( !Machine->IsRunning() && bAutoStart )
	{
		Start();
	}

	PROFILE_SCOPE_CAT( "AI", "StateMachine" );
	Time += static_cast<float64>( DeltaTime );
	FAIContext Context = MakeContext( DeltaTime );
	Machine->Tick( Context, DeltaTime );
	Blackboard->ClearTriggers();
}

FString CStateMachineComponent::Describe() const
{
	FString Text = FString::Printf( "StateMachine '%s'", *Path );
	if ( !Machine.IsValid() )
	{
		return Text + " (not loaded)";
	}
	Text += FString::Printf( " %s, state '%s' for %.2f s", Machine->IsRunning() ? "running" : "stopped", *Machine->DescribeActiveState(), Machine->GetLeafStateTime() );
	if ( bPaused )
	{
		Text += " (paused)";
	}
	if ( Blackboard.IsValid() )
	{
		Text += "\n  blackboard:";
		for ( int32 Index = 0; Index < Blackboard->GetNumKeys(); ++Index )
		{
			const FBlackboardKey Key( Index );
			FString Value;
			switch ( Blackboard->GetKeyType( Key ) )
			{
			case EBlackboardKeyType::Bool:
				Value = Blackboard->GetBool( Key ) ? "true" : "false";
				break;
			case EBlackboardKeyType::Int:
				Value = FString::Printf( "%d", Blackboard->GetInt( Key ) );
				break;
			case EBlackboardKeyType::Float:
				Value = FString::Printf( "%g", static_cast<float64>( Blackboard->GetFloat( Key ) ) );
				break;
			case EBlackboardKeyType::Vector3:
			{
				const FVector3 Vector = Blackboard->GetVector( Key );
				Value = FString::Printf( "(%g, %g, %g)", static_cast<float64>( Vector.x ), static_cast<float64>( Vector.y ), static_cast<float64>( Vector.z ) );
				break;
			}
			case EBlackboardKeyType::Name:
				Value = "\"" + Blackboard->GetName( Key ) + "\"";
				break;
			case EBlackboardKeyType::Object:
				Value = FString::Printf( "#%llu", static_cast<unsigned long long>( Blackboard->GetObjectId( Key ) ) );
				break;
			case EBlackboardKeyType::Enum:
				Value = Blackboard->GetEnumName( Key );
				break;
			}
			Text += FString::Printf( "\n    %s = %s", *Blackboard->GetKeyName( Key ), *Value );
		}
	}
	TArray<FStateHistoryEntry> History;
	Machine->GetHistory( History );
	if ( !History.IsEmpty() )
	{
		Text += "\n  history (oldest first):";
		for ( const FStateHistoryEntry& Entry : History )
		{
			Text += FString::Printf( "\n    %.2f s  %s", Entry.Time, *Entry.Description );
		}
	}
	return Text;
}
