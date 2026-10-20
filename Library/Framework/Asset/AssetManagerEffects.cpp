#include "AssetManager.h"

#include "Library/Core/Log/Log.h"
#include "Library/Core/Time/PlatformTime.h"
#include "Library/Json/JsonObject.h"
#include "Library/Platform/FileSystem.h"

//=============================================================================
// CAssetManager: パーティクルのエフェクト（.effect.json。書式は ParticleEffect.cpp の先頭）
//=============================================================================
TSharedPtr<CEffectAsset> CAssetManager::LoadEffect( const FString& Path )
{
	const TOptional<FString> Full = PrepareLoad( "effect", Path );
	if ( !Full.IsSet() ) return nullptr;

	const FString FullPath = Full.Get( FString() );
	const FString Key = MakeKey( FullPath );
	if ( TWeakPtr<CEffectAsset>* Found = Effects.Find( Key ) )
	{
		if ( TSharedPtr<CEffectAsset> Existing = Found->Pin() )
		{
			RecordCacheHit();
			return Existing;
		}
	}

	const int64 StartTick = NPlatformTime::GetTicks();
	NJson::CJsonObject Json;
	FParticleEffectDefinition Definition;
	TArray<FString> Warnings;
	if ( !Json.LoadFromFile( FullPath ) || !FParticleEffectDefinition::FromJson( Json.GetRoot(), Definition, &Warnings ) )
	{
		LOG( LogAsset, Error, "Failed to read the effect '%s' (not valid JSON?).", *FullPath );
		RecordFailure();
		return nullptr;
	}
	for ( int32 i = 0; i < Warnings.Num() && i < 20; ++i ) LOG( LogAsset, Warning, "Effect '%s': %s", *FullPath, *Warnings[i] );
	if ( Warnings.Num() > 20 ) LOG( LogAsset, Warning, "Effect '%s': %d more warning(s) not shown", *FullPath, Warnings.Num() - 20 );

	TSharedPtr<CEffectAsset> Asset = MakeShared<CEffectAsset>( FullPath, Definition, Warnings );
	RecordLoad( *Asset, FullPath, StartTick );
	Effects.Add( Key, Asset );
	LOG( LogAsset, Verbose, "Loaded the effect '%s' (%d emitter(s))", *FullPath, Definition.Emitters.Num() );
	return Asset;
}

bool CAssetManager::SaveEffect( const FString& Path, const FParticleEffectDefinition& Definition )
{
	const TOptional<FString> Full = PrepareSavePath( Path );
	if ( !Full.IsSet() ) return false;

	NJson::CJsonObject Json;
	Json.GetRoot() = Definition.ToJson();
	if ( !Json.SaveToFile( Full.Get( FString() ) ) )
	{
		LOG( LogAsset, Error, "Failed to write the effect '%s'.", *Full.Get( FString() ) );
		return false;
	}
	Effects.Remove( MakeKey( Full.Get( FString() ) ) );
	LOG( LogAsset, Log, "Saved the effect '%s' (%d emitter(s))", *Full.Get( FString() ), Definition.Emitters.Num() );
	return true;
}
