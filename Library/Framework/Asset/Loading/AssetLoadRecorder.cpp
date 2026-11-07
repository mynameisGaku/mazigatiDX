// SPDX-License-Identifier: MIT
#include "AssetLoadRecorder.h"

#include "Library/Core/Time/PlatformTime.h"
#include "Library/Framework/Asset/Asset.h"
#include "Library/Platform/System/FileSystem.h"

namespace
{
	uint64 HashPath( const FString& Path )
	{
		uint64 Hash = 1469598103934665603ull;
		for ( const char Ch : Path )
		{
			Hash ^= static_cast<uint8>( Ch );
			Hash *= 1099511628211ull;
		}
		return Hash;
	}
}

FProfileScope CAssetLoadRecorder::MakeProfileScope( const char* Name, const FString& Path )
{
	IProfiler* Recorder = NProfiler::GRecorder;
	return FProfileScope( NProfileCategory::Asset, "", Name, HashPath( Path ), Recorder != nullptr ? Recorder->InternString( *Path ) : 0u );
}

void CAssetLoadRecorder::RecordLoad( CAsset& Asset, const FString& FullPath, int64 StartTick )
{
	const float32 Ms = static_cast<float32>( NPlatformTime::TicksToMs( NPlatformTime::GetTicks() - StartTick ) );
	const int64 FileBytes = FullPath.IsEmpty() ? 0 : NFileSystem::GetFileSize( FullPath );
	Asset.SetLoadInfo( FileBytes > 0 ? FileBytes : 0, Ms );

	++Counters.NumLoads;
	Counters.TotalLoadMs += Ms;
	if ( Ms > Counters.MaxLoadMs )
	{
		Counters.MaxLoadMs = Ms;
		Counters.MaxLoadPath = FullPath;
	}
	bLoadHappened = true;
}

bool CAssetLoadRecorder::ConsumeLoadHappened()
{
	const bool bResult = bLoadHappened;
	bLoadHappened = false;
	return bResult;
}
