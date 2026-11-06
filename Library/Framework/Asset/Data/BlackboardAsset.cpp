// SPDX-License-Identifier: MIT
#include "BlackboardAsset.h"

CBlackboardAsset::CBlackboardAsset( const FString& InPath, const FBlackboardDefinition& InDefinition, const TArray<FAIIssue>& InLoadIssues )
	: CAsset( EAssetType::Blackboard, InPath )
	, Definition( MakeShared<FBlackboardDefinition>( InDefinition ) )
	, LoadIssues( InLoadIssues )
{
}

FString CBlackboardAsset::Describe() const
{
	return FString::Printf( "%d key(s)", Definition->Keys.Num() );
}

int64 CBlackboardAsset::GetMemoryBytes() const
{
	int64 Bytes = static_cast<int64>( sizeof( FBlackboardDefinition ) );
	for ( const FBlackboardKeyDefinition& Key : Definition->Keys )
	{
		Bytes += static_cast<int64>( sizeof( FBlackboardKeyDefinition ) ) + Key.Name.Len() + Key.Default.Text.Len();
		for ( const FString& Value : Key.EnumValues )
		{
			Bytes += Value.Len();
		}
	}
	return Bytes;
}
