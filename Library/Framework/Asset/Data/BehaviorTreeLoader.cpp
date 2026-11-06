// SPDX-License-Identifier: MIT
#include "BehaviorTreeLoader.h"

#include "Library/Core/Log/Log.h"
#include "Library/Core/Thread/GameThread.h"
#include "Library/Core/Time/PlatformTime.h"
#include "Library/Framework/Asset/Loading/AssetLoadRecorder.h"
#include "Library/Framework/Asset/Loading/AssetLog.h"
#include "Library/Framework/Asset/Loading/AssetPathResolver.h"
#include "Library/Json/JsonObject.h"

CBehaviorTreeLoader::CBehaviorTreeLoader( CAssetLoadContext& InContext )
	: Context( InContext )
{
}

TSharedPtr<CBehaviorTreeAsset> CBehaviorTreeLoader::Load( const FString& Path )
{
	GAME_THREAD_ONLY( nullptr );
	const TOptional<FString> Full = Context.PrepareLoad( "behavior tree", Path );
	if ( !Full.IsSet() )
	{
		return nullptr;
	}

	const FString FullPath = Full.Get( FString() );
	const FString Key = CAssetPathResolver::MakeKey( FullPath );
	if ( TSharedPtr<CBehaviorTreeAsset> Existing = Cache.Find( Key ) )
	{
		Context.GetRecorder().RecordCacheHit();
		return Existing;
	}

	const int64 StartTick = NPlatformTime::GetTicks();
	NJson::CJsonObject Json;
	FBehaviorTreeDefinition Definition;
	TArray<FAIIssue> Issues;
	if ( !Json.LoadFromFile( FullPath ) || !FBehaviorTreeDefinition::FromJson( Json.GetRoot(), Definition, &Issues ) )
	{
		LOG( LogAsset, Error, "Failed to read the behavior tree '%s' (not valid JSON?).", *FullPath );
		Context.GetRecorder().RecordFailure();
		return nullptr;
	}
	NAI::LogIssues( FullPath, Issues );
	if ( !Definition.HasRoot() )
	{
		LOG( LogAsset, Error, "The behavior tree '%s' has no root.", *FullPath );
		Context.GetRecorder().RecordFailure();
		return nullptr;
	}

	TSharedPtr<CBehaviorTreeAsset> Asset = MakeShared<CBehaviorTreeAsset>( FullPath, Definition, Issues );
	Context.GetRecorder().RecordLoad( *Asset, FullPath, StartTick );
	Cache.Add( Key, Asset );
	LOG( LogAsset, Verbose, "Loaded the behavior tree '%s' (%d node(s))", *FullPath, Definition.Nodes.Num() );
	return Asset;
}

bool CBehaviorTreeLoader::Save( const FString& Path, const FBehaviorTreeDefinition& Definition )
{
	const TOptional<FString> Full = Context.GetPaths().PrepareSavePath( Path );
	if ( !Full.IsSet() )
	{
		return false;
	}

	NJson::CJsonObject Json;
	Json.GetRoot() = Definition.ToJson();
	if ( !Json.SaveToFile( Full.Get( FString() ) ) )
	{
		LOG( LogAsset, Error, "Failed to write the behavior tree '%s'.", *Full.Get( FString() ) );
		return false;
	}
	Cache.Remove( CAssetPathResolver::MakeKey( Full.Get( FString() ) ) );
	LOG( LogAsset, Log, "Saved the behavior tree '%s' (%d node(s))", *Full.Get( FString() ), Definition.Nodes.Num() );
	return true;
}

void CBehaviorTreeLoader::CollectInfos( TArray<FAssetInfo>& Out ) const
{
	Cache.CollectInfos( Out );
}

void CBehaviorTreeLoader::Prune()
{
	Cache.Prune();
}

void CBehaviorTreeLoader::Clear()
{
	Cache.Clear();
}
