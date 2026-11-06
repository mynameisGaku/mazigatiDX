// SPDX-License-Identifier: MIT
#include "WorldLoader.h"

#include "Library/Core/Log/Log.h"
#include "Library/Core/Thread/GameThread.h"
#include "Library/Core/Time/PlatformTime.h"
#include "Library/Framework/Asset/Loading/AssetLoadRecorder.h"
#include "Library/Framework/Asset/Loading/AssetLog.h"
#include "Library/Framework/Asset/Loading/AssetPathResolver.h"
#include "Library/Json/JsonObject.h"

CWorldLoader::CWorldLoader( CAssetLoadContext& InContext )
	: Context( InContext )
{
}

TSharedPtr<CWorldAsset> CWorldLoader::Load( const FString& Path )
{
	GAME_THREAD_ONLY( nullptr );
	const TOptional<FString> Full = Context.PrepareLoad( "world", Path );
	if ( !Full.IsSet() )
	{
		return nullptr;
	}

	const FString FullPath = Full.Get( FString() );
	const FString Key = CAssetPathResolver::MakeKey( FullPath );
	if ( TSharedPtr<CWorldAsset> Existing = Cache.Find( Key ) )
	{
		Context.GetRecorder().RecordCacheHit();
		return Existing;
	}

	const int64 StartTick = NPlatformTime::GetTicks();
	NJson::CJsonObject Json;
	FWorldDefinition Definition;
	TArray<FString> Warnings;
	if ( !Json.LoadFromFile( FullPath ) || !FWorldDefinition::FromJson( Json.GetRoot(), Definition, &Warnings ) )
	{
		LOG( LogAsset, Error, "Failed to read the world '%s' (not valid JSON?).", *FullPath );
		Context.GetRecorder().RecordFailure();
		return nullptr;
	}
	for ( int32 i = 0; i < Warnings.Num() && i < 20; ++i )
	{
		LOG( LogAsset, Warning, "World '%s': %s", *FullPath, *Warnings[i] );
	}
	if ( Warnings.Num() > 20 )
	{
		LOG( LogAsset, Warning, "World '%s': %d more warning(s) not shown", *FullPath, Warnings.Num() - 20 );
	}

	TSharedPtr<CWorldAsset> Asset = MakeShared<CWorldAsset>( FullPath, Definition, Warnings );
	Context.GetRecorder().RecordLoad( *Asset, FullPath, StartTick );
	Cache.Add( Key, Asset );
	LOG( LogAsset, Verbose, "Loaded the world '%s' (%d object(s))", *FullPath, Definition.Objects.Num() );
	return Asset;
}

bool CWorldLoader::Save( const FString& Path, const FWorldDefinition& Definition )
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
		LOG( LogAsset, Error, "Failed to write the world '%s'.", *Full.Get( FString() ) );
		return false;
	}
	Cache.Remove( CAssetPathResolver::MakeKey( Full.Get( FString() ) ) );
	LOG( LogAsset, Log, "Saved the world '%s' (%d object(s))", *Full.Get( FString() ), Definition.Objects.Num() );
	return true;
}

void CWorldLoader::CollectInfos( TArray<FAssetInfo>& Out ) const
{
	Cache.CollectInfos( Out );
}

void CWorldLoader::Prune()
{
	Cache.Prune();
}

void CWorldLoader::Clear()
{
	Cache.Clear();
}
