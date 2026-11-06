// SPDX-License-Identifier: MIT
#include "AssetUsageScanner.h"

#include "Library/Core/Collection/Sort.h"
#include "Library/Framework/Asset/AssetSubsystem.h"
#include "Library/Platform/System/FileSystem.h"

TArray<FWorldUsage> NAssetUsage::FindWorldsUsing( CAssetSubsystem& Assets, const FString& AssetPath )
{
	TArray<FWorldUsage> Result;
	if ( AssetPath.IsEmpty() )
	{
		return Result;
	}
	TArray<FString> Files = NFileSystem::ListFilesRecursive( Assets.GetRootDirectory(), { ".gxworld" } );
	NAlgo::Sort( Files.GetData(), Files.Num() );
	for ( const FString& File : Files )
	{
		const TSharedPtr<CWorldAsset> World = Assets.LoadWorld( File );
		if ( !World.IsValid() )
		{
			continue;
		}
		FWorldUsage Usage;
		Usage.Uses = NWorld::FindAssetUses( World->GetDefinition(), AssetPath );
		if ( Usage.Uses.Num() > 0 )
		{
			Usage.WorldPath = File;
			Result.Add( Usage );
		}
	}
	// 走査のために読んだワールドの参照は、ここで無くなっている（解放は、最後の参照が消えた時点で済む）。記録も片付ける
	Assets.ReleaseUnused();
	return Result;
}
