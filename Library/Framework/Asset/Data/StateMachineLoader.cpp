// SPDX-License-Identifier: MIT
#include "StateMachineLoader.h"

#include "Library/Core/Log/Log.h"
#include "Library/Core/Thread/GameThread.h"
#include "Library/Core/Time/PlatformTime.h"
#include "Library/Framework/Asset/Loading/AssetLoadRecorder.h"
#include "Library/Framework/Asset/Loading/AssetLog.h"
#include "Library/Framework/Asset/Loading/AssetPathResolver.h"
#include "Library/Json/JsonObject.h"

CStateMachineLoader::CStateMachineLoader( CAssetLoadContext& InContext )
	: Context( InContext )
{
}

TSharedPtr<CStateMachineAsset> CStateMachineLoader::Load( const FString& Path )
{
	GAME_THREAD_ONLY( nullptr );
	const TOptional<FString> Full = Context.PrepareLoad( "state machine", Path );
	if ( !Full.IsSet() )
	{
		return nullptr;
	}

	const FString FullPath = Full.Get( FString() );
	const FString Key = CAssetPathResolver::MakeKey( FullPath );
	if ( TSharedPtr<CStateMachineAsset> Existing = Cache.Find( Key ) )
	{
		Context.GetRecorder().RecordCacheHit();
		return Existing;
	}

	const int64 StartTick = NPlatformTime::GetTicks();
	NJson::CJsonObject Json;
	FStateMachineDefinition Definition;
	TArray<FAIIssue> Issues;
	if ( !Json.LoadFromFile( FullPath ) || !FStateMachineDefinition::FromJson( Json.GetRoot(), Definition, &Issues ) )
	{
		LOG( LogAsset, Error, "Failed to read the state machine '%s' (not valid JSON?).", *FullPath );
		Context.GetRecorder().RecordFailure();
		return nullptr;
	}
	NAI::LogIssues( FullPath, Issues );
	if ( Definition.States.IsEmpty() )
	{
		LOG( LogAsset, Error, "The state machine '%s' has no states.", *FullPath );
		Context.GetRecorder().RecordFailure();
		return nullptr;
	}

	TSharedPtr<CStateMachineAsset> Asset = MakeShared<CStateMachineAsset>( FullPath, Definition, Issues );
	Context.GetRecorder().RecordLoad( *Asset, FullPath, StartTick );
	Cache.Add( Key, Asset );
	LOG( LogAsset, Verbose, "Loaded the state machine '%s' (%d state(s))", *FullPath, Definition.States.Num() );
	return Asset;
}

bool CStateMachineLoader::Save( const FString& Path, const FStateMachineDefinition& Definition )
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
		LOG( LogAsset, Error, "Failed to write the state machine '%s'.", *Full.Get( FString() ) );
		return false;
	}
	Cache.Remove( CAssetPathResolver::MakeKey( Full.Get( FString() ) ) );
	LOG( LogAsset, Log, "Saved the state machine '%s' (%d state(s))", *Full.Get( FString() ), Definition.States.Num() );
	return true;
}

void CStateMachineLoader::CollectInfos( TArray<FAssetInfo>& Out ) const
{
	Cache.CollectInfos( Out );
}

void CStateMachineLoader::Prune()
{
	Cache.Prune();
}

void CStateMachineLoader::Clear()
{
	Cache.Clear();
}
