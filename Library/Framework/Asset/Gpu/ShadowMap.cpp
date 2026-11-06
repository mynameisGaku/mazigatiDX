// SPDX-License-Identifier: MIT
#include "ShadowMap.h"

#include "Library/Core/Interfaces/Asset/AssetBackend.h"
#include "Library/Framework/Asset/Asset.h"

TSharedPtr<CShadowMap> CShadowMap::Create( int32 Width, int32 Height )
{
	IAssetBackend* Backend = NAssetService::GetBackend();
	if ( Backend == nullptr || !NAssetService::IsMainThread() || Width < 1 || Height < 1 || Width > MaxSize || Height > MaxSize )
	{
		return nullptr;
	}
	const int32 ShadowMap = Backend->MakeShadowMap( Width, Height );
	if ( ShadowMap < 0 )
	{
		return nullptr;
	}
	return TSharedPtr<CShadowMap>( new CShadowMap( ShadowMap, Width, Height ) );
}

CShadowMap::~CShadowMap()
{
	NAssetService::ReleaseShadowMap( ShadowMap );
}

bool CShadowMap::GetViewProjection( FMatrix4x4& OutMatrix ) const
{
	IAssetBackend* Backend = NAssetService::GetBackend();
	float32 Values[16] = {};
	if ( Backend == nullptr || !NAssetService::IsMainThread() || !Backend->GetShadowMapViewProjection( ShadowMap, Values ) )
	{
		return false;
	}
	OutMatrix = FMatrix4x4(
		Values[0], Values[1], Values[2], Values[3],
		Values[4], Values[5], Values[6], Values[7],
		Values[8], Values[9], Values[10], Values[11],
		Values[12], Values[13], Values[14], Values[15] );
	return true;
}

FShadowMapHandle CShadowMap::GetShadowMapHandle() const
{
	return FShadowMapHandle( ShadowMap, Width, Height );
}
