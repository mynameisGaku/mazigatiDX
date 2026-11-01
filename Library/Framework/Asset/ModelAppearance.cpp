// SPDX-License-Identifier: MIT
#include "Library/Core/Thread/GameThread.h"
#include "Library/Framework/Asset/ModelAppearance.h"

#include "Library/Core/Interfaces/Asset/AssetBackend.h"
#include "Library/Framework/Asset/Asset.h"
#include "Library/Framework/Asset/Model.h"
#include "Library/Math/MathUtils.h"

FModelAppearance::FModelAppearance( int32 InHandle, const CModel* InSource )
	: Handle( InHandle )
	, Source( InSource )
{
}

bool FModelAppearance::GetMaterialColors( int32 Material, FModelMaterialColors& Out ) const
{
	GAME_THREAD_ONLY( false );
	IAssetBackend* Backend = NAssetService::GetBackend();
	return Backend != nullptr && Handle >= 0 && Backend->GetModelMaterialColors( Handle, Material, Out );
}

bool FModelAppearance::SetMaterialColors( int32 Material, const FModelMaterialColors& Colors )
{
	GAME_THREAD_ONLY( false );
	IAssetBackend* Backend = NAssetService::GetBackend();
	return Backend != nullptr && Handle >= 0 && Backend->SetModelMaterialColors( Handle, Material, Colors );
}

bool FModelAppearance::SetMaterialDiffuse( int32 Material, const FColor& Diffuse )
{
	GAME_THREAD_ONLY( false );
	FModelMaterialColors Colors;
	if ( !GetMaterialColors( Material, Colors ) )
	{
		return false;
	}
	Colors.Diffuse = Diffuse;
	return SetMaterialColors( Material, Colors );
}

bool FModelAppearance::ResetMaterialColors( int32 Material )
{
	GAME_THREAD_ONLY( false );
	if ( Source == nullptr )
	{
		return false;
	}
	const TArray<FModelMaterialInfo>& Materials = Source->GetStructure().Materials;
	if ( Material >= 0 )
	{
		return Material < Materials.Num() && SetMaterialColors( Material, Materials[Material].Colors );
	}
	bool bAll = Materials.Num() > 0;
	for ( int32 Index = 0; Index < Materials.Num(); ++Index )
	{
		bAll = SetMaterialColors( Index, Materials[Index].Colors ) && bAll;
	}
	return bAll;
}

bool FModelAppearance::IsMeshVisible( int32 Mesh ) const
{
	GAME_THREAD_ONLY( true );
	IAssetBackend* Backend = NAssetService::GetBackend();
	bool bVisible = true;
	if ( Backend != nullptr && Handle >= 0 )
	{
		Backend->GetModelMeshVisible( Handle, Mesh, bVisible );
	}
	return bVisible;
}

bool FModelAppearance::SetMeshVisible( int32 Mesh, bool bVisible )
{
	GAME_THREAD_ONLY( false );
	IAssetBackend* Backend = NAssetService::GetBackend();
	return Backend != nullptr && Handle >= 0 && Backend->SetModelMeshVisible( Handle, Mesh, bVisible );
}

bool FModelAppearance::IsFrameVisible( int32 Frame ) const
{
	GAME_THREAD_ONLY( true );
	IAssetBackend* Backend = NAssetService::GetBackend();
	bool bVisible = true;
	if ( Backend != nullptr && Handle >= 0 )
	{
		Backend->GetModelFrameVisible( Handle, Frame, bVisible );
	}
	return bVisible;
}

bool FModelAppearance::SetFrameVisible( int32 Frame, bool bVisible )
{
	GAME_THREAD_ONLY( false );
	IAssetBackend* Backend = NAssetService::GetBackend();
	return Backend != nullptr && Handle >= 0 && Backend->SetModelFrameVisible( Handle, Frame, bVisible );
}

float32 FModelAppearance::GetOpacity() const
{
	GAME_THREAD_ONLY( 1.0f );
	IAssetBackend* Backend = NAssetService::GetBackend();
	float32 Opacity = 1.0f;
	if ( Backend != nullptr && Handle >= 0 )
	{
		Backend->GetModelOpacity( Handle, Opacity );
	}
	return Opacity;
}

bool FModelAppearance::SetOpacity( float32 Opacity )
{
	GAME_THREAD_ONLY( false );
	IAssetBackend* Backend = NAssetService::GetBackend();
	// NaN は 0 として扱う（Clamp は NaN をそのまま返す）
	const float32 Safe = Opacity == Opacity ? NMath::Clamp( Opacity, 0.0f, 1.0f ) : 0.0f;
	return Backend != nullptr && Handle >= 0 && Backend->SetModelOpacity( Handle, Safe );
}
