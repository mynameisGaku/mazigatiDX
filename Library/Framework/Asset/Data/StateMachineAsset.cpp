// SPDX-License-Identifier: MIT
#include "StateMachineAsset.h"

CStateMachineAsset::CStateMachineAsset( const FString& InPath, const FStateMachineDefinition& InDefinition, const TArray<FAIIssue>& InLoadIssues )
	: CAsset( EAssetType::StateMachine, InPath )
	, Definition( MakeShared<FStateMachineDefinition>( InDefinition ) )
	, LoadIssues( InLoadIssues )
{
}

FString CStateMachineAsset::Describe() const
{
	int32 NumTransitions = Definition->Transitions.Num() + Definition->AnyTransitions.Num();
	for ( const FStateDef& State : Definition->States )
	{
		NumTransitions += State.Transitions.Num() + State.AnyTransitions.Num();
	}
	return FString::Printf( "%d state(s), %d transition(s)", Definition->States.Num(), NumTransitions );
}

int64 CStateMachineAsset::GetMemoryBytes() const
{
	// 定義そのものの大きさ（部品は、使うインスタンスが持つ）
	int64 Bytes = static_cast<int64>( sizeof( FStateMachineDefinition ) );
	for ( const FStateDef& State : Definition->States )
	{
		Bytes += static_cast<int64>( sizeof( FStateDef ) ) + State.Name.Len() + State.Clip.Len();
		Bytes += static_cast<int64>( State.Transitions.Num() + State.AnyTransitions.Num() ) * static_cast<int64>( sizeof( FStateTransitionDef ) );
	}
	Bytes += static_cast<int64>( Definition->Transitions.Num() + Definition->AnyTransitions.Num() ) * static_cast<int64>( sizeof( FStateTransitionDef ) );
	return Bytes;
}
