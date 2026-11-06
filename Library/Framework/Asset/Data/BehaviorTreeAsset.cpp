// SPDX-License-Identifier: MIT
#include "BehaviorTreeAsset.h"

CBehaviorTreeAsset::CBehaviorTreeAsset( const FString& InPath, const FBehaviorTreeDefinition& InDefinition, const TArray<FAIIssue>& InLoadIssues )
	: CAsset( EAssetType::BehaviorTree, InPath )
	, Definition( MakeShared<FBehaviorTreeDefinition>( InDefinition ) )
	, LoadIssues( InLoadIssues )
{
}

FString CBehaviorTreeAsset::Describe() const
{
	int32 NumDecorators = 0;
	int32 NumServices = 0;
	for ( const FBTNodeDef& Node : Definition->Nodes )
	{
		NumDecorators += Node.Decorators.Num();
		NumServices += Node.Services.Num();
	}
	return FString::Printf( "%d node(s), %d decorator(s), %d service(s)", Definition->Nodes.Num(), NumDecorators, NumServices );
}

int64 CBehaviorTreeAsset::GetMemoryBytes() const
{
	// 定義そのものの大きさ（部品は、使うインスタンスが持つ）
	int64 Bytes = static_cast<int64>( sizeof( FBehaviorTreeDefinition ) );
	for ( const FBTNodeDef& Node : Definition->Nodes )
	{
		Bytes += static_cast<int64>( sizeof( FBTNodeDef ) ) + Node.Name.Len() + Node.Task.Do.Len();
		Bytes += static_cast<int64>( Node.Decorators.Num() ) * static_cast<int64>( sizeof( FBTDecoratorDef ) );
		Bytes += static_cast<int64>( Node.Services.Num() ) * static_cast<int64>( sizeof( FBTServiceDef ) );
	}
	return Bytes;
}
