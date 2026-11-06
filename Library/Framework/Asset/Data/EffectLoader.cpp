// SPDX-License-Identifier: MIT
#include "EffectLoader.h"

#include "Library/Core/Log/Log.h"
#include "Library/Core/Thread/GameThread.h"
#include "Library/Core/Time/PlatformTime.h"
#include "Library/Framework/Asset/Loading/AssetLoadRecorder.h"
#include "Library/Framework/Asset/Loading/AssetLog.h"
#include "Library/Framework/Asset/Loading/AssetPathResolver.h"
#include "Library/Json/JsonObject.h"

CEffectLoader::CEffectLoader( CAssetLoadContext& InContext )
	: Context( InContext )
{
}

TSharedPtr<CEffectAsset> CEffectLoader::Load( const FString& Path )
{
	GAME_THREAD_ONLY( nullptr );
	const TOptional<FString> Full = Context.PrepareLoad( "effect", Path );
	if ( !Full.IsSet() )
	{
		return nullptr;
	}

	const FString FullPath = Full.Get( FString() );
	const FString Key = CAssetPathResolver::MakeKey( FullPath );
	if ( TSharedPtr<CEffectAsset> Existing = Cache.Find( Key ) )
	{
		Context.GetRecorder().RecordCacheHit();
		return Existing;
	}

	const int64 StartTick = NPlatformTime::GetTicks();
	NJson::CJsonObject Json;
	FParticleEffectDefinition Definition;
	TArray<FString> Warnings;
	if ( !Json.LoadFromFile( FullPath ) || !FParticleEffectDefinition::FromJson( Json.GetRoot(), Definition, &Warnings ) )
	{
		LOG( LogAsset, Error, "Failed to read the effect '%s' (not valid JSON?).", *FullPath );
		Context.GetRecorder().RecordFailure();
		return nullptr;
	}
	for ( int32 i = 0; i < Warnings.Num() && i < 20; ++i )
	{
		LOG( LogAsset, Warning, "Effect '%s': %s", *FullPath, *Warnings[i] );
	}
	if ( Warnings.Num() > 20 )
	{
		LOG( LogAsset, Warning, "Effect '%s': %d more warning(s) not shown", *FullPath, Warnings.Num() - 20 );
	}

	TSharedPtr<CEffectAsset> Asset = MakeShared<CEffectAsset>( FullPath, Definition, Warnings );
	Context.GetRecorder().RecordLoad( *Asset, FullPath, StartTick );
	Cache.Add( Key, Asset );
	LOG( LogAsset, Verbose, "Loaded the effect '%s' (%d emitter(s))", *FullPath, Definition.Emitters.Num() );
	return Asset;
}

bool CEffectLoader::Save( const FString& Path, const FParticleEffectDefinition& Definition )
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
		LOG( LogAsset, Error, "Failed to write the effect '%s'.", *Full.Get( FString() ) );
		return false;
	}
	Cache.Remove( CAssetPathResolver::MakeKey( Full.Get( FString() ) ) );
	LOG( LogAsset, Log, "Saved the effect '%s' (%d emitter(s))", *Full.Get( FString() ), Definition.Emitters.Num() );
	return true;
}

void CEffectLoader::CollectInfos( TArray<FAssetInfo>& Out ) const
{
	Cache.CollectInfos( Out );
}

void CEffectLoader::Prune()
{
	Cache.Prune();
}

void CEffectLoader::Clear()
{
	Cache.Clear();
}
