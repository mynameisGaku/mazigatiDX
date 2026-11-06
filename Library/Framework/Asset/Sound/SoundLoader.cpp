// SPDX-License-Identifier: MIT
#include "SoundLoader.h"

#include "Library/Core/Log/Log.h"
#include "Library/Core/Profiler/ProfilerScope.h"
#include "Library/Core/Thread/GameThread.h"
#include "Library/Core/Time/PlatformTime.h"
#include "Library/Framework/Asset/Loading/AssetLoadRecorder.h"
#include "Library/Framework/Asset/Loading/AssetLog.h"
#include "Library/Framework/Asset/Loading/AssetPathResolver.h"

CSoundLoader::CSoundLoader( CAssetLoadContext& InContext )
	: Context( InContext )
{
}

TSharedPtr<CSound> CSoundLoader::Load( const FString& Path, ESoundLoadMode Mode, bool bAsync )
{
	GAME_THREAD_ONLY( nullptr );
	const TOptional<FString> Full = Context.PrepareLoad( "sound", Path );
	if ( !Full.IsSet() )
	{
		return nullptr;
	}

	const FString FullPath = Full.Get( FString() );
	// 読み込み方が違えば別のアセット（同じファイルを、効果音としても BGM としても使えるように）
	const FString Key = CAssetPathResolver::MakeKey( FullPath ) + ( Mode == ESoundLoadMode::Stream ? "|stream" : "|memory" );
	if ( TSharedPtr<CSound> Existing = Cache.Find( Key ) )
	{
		Context.GetRecorder().RecordCacheHit();
		return Existing;
	}

	PROFILE_SCOPE_LAZY( LoadScope, CAssetLoadRecorder::MakeProfileScope( "LoadSound", FullPath ) );
	const int64 StartTick = NPlatformTime::GetTicks();
	const int32 Handle = Context.GetSoundBackend()->LoadSound( FullPath, Mode, bAsync );
	if ( Handle < 0 )
	{
		LOG( LogAsset, Error, "Failed to load the sound '%s' (unsupported or broken file).", *FullPath );
		Context.GetRecorder().RecordFailure();
		return nullptr;
	}
	TSharedPtr<CSound> Sound = MakeShared<CSound>( FullPath, Handle, Mode, bAsync );
	Context.GetRecorder().RecordLoad( *Sound, FullPath, StartTick );
	Cache.Add( Key, Sound );
	LOG( LogAsset, Verbose, "Loaded the sound '%s' (%s)%s", *FullPath, Mode == ESoundLoadMode::Stream ? "stream" : "memory", bAsync ? " async" : "" );
	return Sound;
}

void CSoundLoader::CollectInfos( TArray<FAssetInfo>& Out ) const
{
	Cache.CollectInfos( Out );
}

void CSoundLoader::Prune()
{
	Cache.Prune();
}

void CSoundLoader::Clear()
{
	Cache.Clear();
}
