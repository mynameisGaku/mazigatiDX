// SPDX-License-Identifier: MIT
#include "AssetLoadRecorder.h"

#include "Library/Core/Time/PlatformTime.h"
#include "Library/Framework/Asset/Asset.h"
#include "Library/Platform/System/FileSystem.h"

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
