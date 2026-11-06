// SPDX-License-Identifier: MIT
#include "AIBlackboardSetup.h"

#include "Library/Core/Log/Log.h"
#include "Library/Framework/AI/AILog.h"
#include "Library/Framework/Asset/AssetSubsystem.h"

TSharedPtr<CBlackboard> NAI::CreateBlackboard( CAssetSubsystem& Assets, const FString& BlackboardPath, const FString& Owner )
{
	TSharedPtr<const FBlackboardDefinition> Definition;
	if ( !BlackboardPath.IsEmpty() )
	{
		if ( const TSharedPtr<CBlackboardAsset> Asset = Assets.LoadBlackboard( BlackboardPath ) )
		{
			Definition = Asset->GetDefinitionShared();
		}
		else
		{
			LOG( LogAI, Warning, "'%s': the blackboard '%s' could not be loaded. An empty blackboard is used.", *Owner, *BlackboardPath );
		}
	}
	return MakeShared<CBlackboard>( Definition );
}

FString NAI::DescribeBlackboard( const CBlackboard& Blackboard, const char* Prefix )
{
	FString Text;
	for ( int32 Index = 0; Index < Blackboard.GetNumKeys(); ++Index )
	{
		const FBlackboardKey Key( Index );
		FString Value;
		switch ( Blackboard.GetKeyType( Key ) )
		{
		case EBlackboardKeyType::Bool:
			Value = Blackboard.GetBool( Key ) ? "true" : "false";
			break;
		case EBlackboardKeyType::Int:
			Value = FString::Printf( "%d", Blackboard.GetInt( Key ) );
			break;
		case EBlackboardKeyType::Float:
			Value = FString::Printf( "%g", static_cast<float64>( Blackboard.GetFloat( Key ) ) );
			break;
		case EBlackboardKeyType::Vector3:
		{
			const FVector3 Vector = Blackboard.GetVector( Key );
			Value = FString::Printf( "(%g, %g, %g)", static_cast<float64>( Vector.x ), static_cast<float64>( Vector.y ), static_cast<float64>( Vector.z ) );
			break;
		}
		case EBlackboardKeyType::Name:
			Value = "\"" + Blackboard.GetName( Key ) + "\"";
			break;
		case EBlackboardKeyType::Object:
			Value = FString::Printf( "#%llu", static_cast<unsigned long long>( Blackboard.GetObjectId( Key ) ) );
			break;
		case EBlackboardKeyType::Enum:
			Value = Blackboard.GetEnumName( Key );
			break;
		}
		Text += FString::Printf( "%s%s = %s", Prefix, *Blackboard.GetKeyName( Key ), *Value );
	}
	return Text;
}
