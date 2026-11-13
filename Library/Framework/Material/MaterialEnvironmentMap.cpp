// SPDX-License-Identifier: MIT
#include "MaterialEnvironmentMap.h"

#include "Library/Framework/Asset/AssetSubsystem.h"

TSharedPtr<CMaterialEnvironmentMap> CMaterialEnvironmentMap::Create( CAssetSubsystem& Assets, const FString& Name, const FPixelImage& Panorama )
{
	TSharedPtr<CMaterialEnvironmentMap> Map( new CMaterialEnvironmentMap() );
	if ( !Map->Environment.Build( Panorama ) )
	{
		return TSharedPtr<CMaterialEnvironmentMap>();
	}
	Map->Texture = Assets.CreateDynamicTexture( Name, Map->Environment.GetAtlas() );
	if ( !Map->Texture )
	{
		return TSharedPtr<CMaterialEnvironmentMap>();
	}
	return Map;
}

TSharedPtr<CMaterialEnvironmentMap> CMaterialEnvironmentMap::Load( CAssetSubsystem& Assets, const FString& Path )
{
	const FPixelImage Panorama = Assets.LoadPixelImage( Path );
	if ( !Panorama.IsValid() )
	{
		return TSharedPtr<CMaterialEnvironmentMap>();
	}
	return Create( Assets, FString( "EnvironmentMap:" ) + Path, Panorama );
}
