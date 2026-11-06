// SPDX-License-Identifier: MIT
#include "ModelLoader.h"

#include "Library/Core/Interfaces/Asset/AssetBackend.h"
#include "Library/Core/Log/Log.h"
#include "Library/Core/Profiler/ProfilerScope.h"
#include "Library/Core/Thread/GameThread.h"
#include "Library/Core/Time/PlatformTime.h"
#include "Library/Framework/Asset/Loading/AssetLoadRecorder.h"
#include "Library/Framework/Asset/Loading/AssetLog.h"
#include "Library/Framework/Asset/Loading/AssetPathResolver.h"

CModelLoader::CModelLoader( CAssetLoadContext& InContext, const CModelImporter& InImporter )
	: Context( InContext )
	, Importer( InImporter )
{
}

TSharedPtr<CModel> CModelLoader::Load( const FString& Path, bool bAsync )
{
	GAME_THREAD_ONLY( nullptr );
	FString FullPath;
	if ( !Importer.ResolveRequest( Path, FullPath ) )
	{
		Context.GetRecorder().RecordFailure();
		return nullptr;
	}
	const FString Key = CAssetPathResolver::MakeKey( FullPath );
	if ( TSharedPtr<CModel> Existing = Cache.Find( Key ) )
	{
		Context.GetRecorder().RecordCacheHit();
		return Existing;
	}

	PROFILE_SCOPE_LAZY( LoadScope, CAssetLoadRecorder::MakeProfileScope( "LoadModel", FullPath ) );
	const int64 StartTick = NPlatformTime::GetTicks();
	FString LoadPath;
	if ( !Importer.ResolveLoadPath( FullPath, LoadPath ) )
	{
		Context.GetRecorder().RecordFailure();
		return nullptr;
	}
	const int32 Handle = Context.GetModelBackend()->LoadModel( LoadPath, bAsync );
	if ( Handle < 0 )
	{
		LOG( LogAsset, Error, "Failed to load the model '%s' (unsupported or broken file).", *LoadPath );
		Context.GetRecorder().RecordFailure();
		return nullptr;
	}
	TSharedPtr<CModel> Model = MakeShared<CModel>( FullPath, Handle, bAsync );
	Context.GetRecorder().RecordLoad( *Model, LoadPath, StartTick );
	Cache.Add( Key, Model );
	LOG( LogAsset, Verbose, "Loaded the model '%s'%s%s", *FullPath, LoadPath != FullPath ? *( " (from " + LoadPath + ")" ) : "", bAsync ? " (async)" : "" );
	return Model;
}

void CModelLoader::CollectInfos( TArray<FAssetInfo>& Out ) const
{
	Cache.CollectInfos( Out );
}

void CModelLoader::Prune()
{
	Cache.Prune();
}

void CModelLoader::Clear()
{
	Cache.Clear();
}
