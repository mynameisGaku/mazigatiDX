// SPDX-License-Identifier: MIT
#include "AssetLoadContext.h"

#include "Library/Core/Interfaces/Asset/AssetBackend.h"
#include "Library/Core/Log/Log.h"
#include "Library/Framework/Asset/Loading/AssetLoadRecorder.h"
#include "Library/Framework/Asset/Loading/AssetLog.h"
#include "Library/Framework/Asset/Loading/AssetPathResolver.h"

CAssetLoadContext::CAssetLoadContext( CAssetPathResolver& InPaths, CAssetLoadRecorder& InRecorder )
	: Paths( InPaths )
	, Recorder( InRecorder )
{
}

TOptional<FString> CAssetLoadContext::PrepareLoad( const char* Kind, const FString& Path ) const
{
	if ( Backend == nullptr )
	{
		LOG( LogAsset, Error, "Cannot load the %s '%s': no asset backend.", Kind, *Path );
		Recorder.RecordFailure();
		return TOptional<FString>();
	}
	TOptional<FString> Full = Paths.ResolvePath( Path );
	if ( !Full.IsSet() )
	{
		LOG( LogAsset, Error, "Cannot load the %s '%s': the file was not found (root directory '%s').", Kind, *Path, *Paths.GetRootDirectory() );
		Recorder.RecordFailure();
	}
	return Full;
}
