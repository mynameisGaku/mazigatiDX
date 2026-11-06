// SPDX-License-Identifier: MIT
#include "PixelImageCodec.h"

#include "Library/Core/Interfaces/Asset/AssetBackend.h"
#include "Library/Core/Log/Log.h"
#include "Library/Core/Thread/GameThread.h"
#include "Library/Framework/Asset/Loading/AssetLoadRecorder.h"
#include "Library/Framework/Asset/Loading/AssetLog.h"
#include "Library/Framework/Asset/Loading/AssetPathResolver.h"

CPixelImageCodec::CPixelImageCodec( CAssetLoadContext& InContext )
	: Context( InContext )
{
}

FPixelImage CPixelImageCodec::Load( const FString& Path ) const
{
	GAME_THREAD_ONLY( FPixelImage() );
	const TOptional<FString> Full = Context.PrepareLoad( "pixel image", Path );
	if ( !Full.IsSet() )
	{
		return FPixelImage();
	}
	int32 Width = 0;
	int32 Height = 0;
	TArray<FColor32> Pixels;
	if ( !Context.GetBackend()->DecodeImageFile( Full.Get( FString() ), Width, Height, Pixels ) || static_cast<int64>( Width ) * Height != Pixels.Num() )
	{
		LOG( LogAsset, Error, "Failed to read the image '%s' (unsupported or broken image).", *Full.Get( FString() ) );
		Context.GetRecorder().RecordFailure();
		return FPixelImage();
	}
	return FPixelImage::FromPixels( Width, Height, Pixels.GetData() );
}

FPixelImage CPixelImageCodec::Decode( const void* FileImage, usize Size ) const
{
	GAME_THREAD_ONLY( FPixelImage() );
	if ( Context.GetBackend() == nullptr || FileImage == nullptr || Size == 0 )
	{
		return FPixelImage();
	}
	int32 Width = 0;
	int32 Height = 0;
	TArray<FColor32> Pixels;
	if ( !Context.GetBackend()->DecodeImage( FileImage, Size, Width, Height, Pixels ) || static_cast<int64>( Width ) * Height != Pixels.Num() )
	{
		LOG( LogAsset, Error, "Failed to decode the image (%d byte(s)): unsupported or broken image.", static_cast<int32>( Size ) );
		Context.GetRecorder().RecordFailure();
		return FPixelImage();
	}
	return FPixelImage::FromPixels( Width, Height, Pixels.GetData() );
}

bool CPixelImageCodec::Save( const FPixelImage& Image, const FString& Path, const FImageSaveOptions& Options ) const
{
	GAME_THREAD_ONLY( false );
	if ( Context.GetBackend() == nullptr || !Image.IsValid() || Path.IsEmpty() )
	{
		return false;
	}
	const TOptional<FString> Full = Context.GetPaths().PrepareSavePath( Path );
	if ( !Full.IsSet() )
	{
		return false;
	}
	if ( !Context.GetBackend()->SavePixels( Image.GetWidth(), Image.GetHeight(), Image.GetData(), Full.Get( FString() ), Options ) )
	{
		LOG( LogAsset, Error, "Failed to save the image to '%s'.", *Full.Get( FString() ) );
		return false;
	}
	return true;
}
