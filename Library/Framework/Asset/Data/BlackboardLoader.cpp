// SPDX-License-Identifier: MIT
#include "BlackboardLoader.h"

#include "Library/Core/Log/Log.h"
#include "Library/Core/Thread/GameThread.h"
#include "Library/Core/Time/PlatformTime.h"
#include "Library/Framework/Asset/Loading/AssetLoadRecorder.h"
#include "Library/Framework/Asset/Loading/AssetLog.h"
#include "Library/Framework/Asset/Loading/AssetPathResolver.h"
#include "Library/Json/JsonObject.h"

CBlackboardLoader::CBlackboardLoader( CAssetLoadContext& InContext )
	: Context( InContext )
{
}

TSharedPtr<CBlackboardAsset> CBlackboardLoader::Load( const FString& Path )
{
	GAME_THREAD_ONLY( nullptr );
	const TOptional<FString> Full = Context.PrepareLoad( "blackboard", Path );
	if ( !Full.IsSet() )
	{
		return nullptr;
	}

	const FString FullPath = Full.Get( FString() );
	const FString Key = CAssetPathResolver::MakeKey( FullPath );
	if ( TSharedPtr<CBlackboardAsset> Existing = Cache.Find( Key ) )
	{
		Context.GetRecorder().RecordCacheHit();
		return Existing;
	}

	const int64 StartTick = NPlatformTime::GetTicks();
	NJson::CJsonObject Json;
	FBlackboardDefinition Definition;
	TArray<FAIIssue> Issues;
	if ( !Json.LoadFromFile( FullPath ) || !FBlackboardDefinition::FromJson( Json.GetRoot(), Definition, &Issues ) )
	{
		LOG( LogAsset, Error, "Failed to read the blackboard '%s' (not valid JSON?).", *FullPath );
		Context.GetRecorder().RecordFailure();
		return nullptr;
	}
	NAI::LogIssues( FullPath, Issues );

	TSharedPtr<CBlackboardAsset> Asset = MakeShared<CBlackboardAsset>( FullPath, Definition, Issues );
	Context.GetRecorder().RecordLoad( *Asset, FullPath, StartTick );
	Cache.Add( Key, Asset );
	LOG( LogAsset, Verbose, "Loaded the blackboard '%s' (%d key(s))", *FullPath, Definition.Keys.Num() );
	return Asset;
}

bool CBlackboardLoader::Save( const FString& Path, const FBlackboardDefinition& Definition )
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
		LOG( LogAsset, Error, "Failed to write the blackboard '%s'.", *Full.Get( FString() ) );
		return false;
	}
	Cache.Remove( CAssetPathResolver::MakeKey( Full.Get( FString() ) ) );
	LOG( LogAsset, Log, "Saved the blackboard '%s' (%d key(s))", *Full.Get( FString() ), Definition.Keys.Num() );
	return true;
}

void CBlackboardLoader::CollectInfos( TArray<FAssetInfo>& Out ) const
{
	Cache.CollectInfos( Out );
}

void CBlackboardLoader::Prune()
{
	Cache.Prune();
}

void CBlackboardLoader::Clear()
{
	Cache.Clear();
}
