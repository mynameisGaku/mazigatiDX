#include "AssetManager.h"

#include "Library/Core/Log/Log.h"
#include "Library/Core/Time/PlatformTime.h"
#include "Library/Json/JsonObject.h"
#include "Library/Platform/FileSystem.h"

//=============================================================================
// CAssetManager: ワールド（.world。書式は WorldDefinition.cpp の先頭を参照）
//=============================================================================
TSharedPtr<CWorldAsset> CAssetManager::LoadWorld( const FString& Path )
{
	const TOptional<FString> Full = PrepareLoad( "world", Path );
	if ( !Full.IsSet() ) return nullptr;

	const FString FullPath = Full.Get( FString() );
	const FString Key = MakeKey( FullPath );
	if ( TWeakPtr<CWorldAsset>* Found = Worlds.Find( Key ) )
	{
		if ( TSharedPtr<CWorldAsset> Existing = Found->Pin() )
		{
			RecordCacheHit();
			return Existing;
		}
	}

	const int64 StartTick = NPlatformTime::GetTicks();
	NJson::CJsonObject Json;
	FWorldDefinition Definition;
	TArray<FString> Warnings;
	if ( !Json.LoadFromFile( FullPath ) || !FWorldDefinition::FromJson( Json.GetRoot(), Definition, &Warnings ) )
	{
		LOG( LogAsset, Error, "Failed to read the world '%s' (not valid JSON?).", *FullPath );
		RecordFailure();
		return nullptr;
	}
	for ( int32 i = 0; i < Warnings.Num() && i < 20; ++i ) LOG( LogAsset, Warning, "World '%s': %s", *FullPath, *Warnings[i] );
	if ( Warnings.Num() > 20 ) LOG( LogAsset, Warning, "World '%s': %d more warning(s) not shown", *FullPath, Warnings.Num() - 20 );

	TSharedPtr<CWorldAsset> Asset = MakeShared<CWorldAsset>( FullPath, Definition, Warnings );
	RecordLoad( *Asset, FullPath, StartTick );
	Worlds.Add( Key, Asset );
	LOG( LogAsset, Verbose, "Loaded the world '%s' (%d object(s))", *FullPath, Definition.Objects.Num() );
	return Asset;
}

bool CAssetManager::SaveWorld( const FString& Path, const FWorldDefinition& Definition )
{
	const TOptional<FString> Full = PrepareSavePath( Path );
	if ( !Full.IsSet() ) return false;

	NJson::CJsonObject Json;
	Json.GetRoot() = Definition.ToJson();
	if ( !Json.SaveToFile( Full.Get( FString() ) ) )
	{
		LOG( LogAsset, Error, "Failed to write the world '%s'.", *Full.Get( FString() ) );
		return false;
	}
	Worlds.Remove( MakeKey( Full.Get( FString() ) ) );
	LOG( LogAsset, Log, "Saved the world '%s' (%d object(s))", *Full.Get( FString() ), Definition.Objects.Num() );
	return true;
}
