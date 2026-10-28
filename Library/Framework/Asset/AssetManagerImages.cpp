// SPDX-License-Identifier: MIT

#include "AssetManager.h"

#include "Library/Core/Log/Log.h"
#include "Library/Core/Time/PlatformTime.h"
#include "Library/Platform/FileSystem.h"

TSharedPtr<CDynamicTexture> CAssetManager::CreateDynamicTexture( const FString& Name, const FPixelImage& Image )
{
	if ( GetBackend() == nullptr )
	{
		LOG( LogAsset, Error, "Cannot create the texture '%s' from pixels: no asset backend.", *Name );
		return nullptr;
	}
	if ( !Image.IsValid() )
	{
		LOG( LogAsset, Error, "Cannot create the texture '%s' from pixels: the image is empty (the size must be 1 to %d).", *Name, FPixelImage::MaxSize );
		return nullptr;
	}

	// 同じ名前で、まだ使われているものがあれば共有する（中身は替えない。替えるときは CDynamicTexture::Update）
	const FString Key = "pixels:" + MakeKey( Name );
	if ( TWeakPtr<CTexture>* Found = Textures.Find( Key ) )
	{
		if ( TSharedPtr<CDynamicTexture> Existing = DynamicCastSharedPtr<CDynamicTexture>( Found->Pin() ) )
		{
			RecordCacheHit();
			return Existing;
		}
	}

	const int64 StartTick = NPlatformTime::GetTicks();
	const int32 Handle = GetBackend()->CreateTextureFromPixels( Image.GetWidth(), Image.GetHeight(), Image.GetData() );
	if ( Handle < 0 )
	{
		LOG( LogAsset, Error, "Failed to create the texture '%s' from pixels (%d x %d).", *Name, Image.GetWidth(), Image.GetHeight() );
		RecordFailure();
		return nullptr;
	}
	TSharedPtr<CDynamicTexture> Texture = MakeShared<CDynamicTexture>( Name, Handle, Image.GetWidth(), Image.GetHeight() );
	RecordLoad( *Texture, FString(), StartTick );
	Textures.Add( Key, Texture );
	return Texture;
}

FPixelImage CAssetManager::LoadPixelImage( const FString& Path )
{
	const TOptional<FString> Full = PrepareLoad( "pixel image", Path );
	if ( !Full.IsSet() )
	{
		return FPixelImage();
	}
	int32 Width = 0;
	int32 Height = 0;
	TArray<FColor32> Pixels;
	if ( !GetBackend()->DecodeImageFile( Full.Get( FString() ), Width, Height, Pixels ) || static_cast<int64>( Width ) * Height != Pixels.Num() )
	{
		LOG( LogAsset, Error, "Failed to read the image '%s' (unsupported or broken image).", *Full.Get( FString() ) );
		RecordFailure();
		return FPixelImage();
	}
	return FPixelImage::FromPixels( Width, Height, Pixels.GetData() );
}

FPixelImage CAssetManager::DecodePixelImage( const void* FileImage, usize Size )
{
	if ( GetBackend() == nullptr || FileImage == nullptr || Size == 0 )
	{
		return FPixelImage();
	}
	int32 Width = 0;
	int32 Height = 0;
	TArray<FColor32> Pixels;
	if ( !GetBackend()->DecodeImage( FileImage, Size, Width, Height, Pixels ) || static_cast<int64>( Width ) * Height != Pixels.Num() )
	{
		LOG( LogAsset, Error, "Failed to decode the image (%d byte(s)): unsupported or broken image.", static_cast<int32>( Size ) );
		RecordFailure();
		return FPixelImage();
	}
	return FPixelImage::FromPixels( Width, Height, Pixels.GetData() );
}

bool CAssetManager::SavePixelImage( const FPixelImage& Image, const FString& Path, const FImageSaveOptions& Options )
{
	if ( GetBackend() == nullptr || !Image.IsValid() || Path.IsEmpty() )
	{
		return false;
	}
	const TOptional<FString> Full = PrepareSavePath( Path );
	if ( !Full.IsSet() )
	{
		return false;
	}
	if ( !GetBackend()->SavePixels( Image.GetWidth(), Image.GetHeight(), Image.GetData(), Full.Get( FString() ), Options ) )
	{
		LOG( LogAsset, Error, "Failed to save the image to '%s'.", *Full.Get( FString() ) );
		return false;
	}
	return true;
}

TArray<TSharedPtr<CTexture>> CAssetManager::SplitTexture( const TSharedPtr<CTexture>& Source, const FSpriteSheetGrid& Grid )
{
	TArray<TSharedPtr<CTexture>> Pieces;
	if ( GetBackend() == nullptr || !Source.IsValid() || !Source->IsReady() || Source->GetHandle() < 0 )
	{
		return Pieces;
	}
	TArray<FSpriteFrame> Frames;
	CSpriteSheet::ComputeGridFrames( Grid, Source->GetWidth(), Source->GetHeight(), Frames );
	for ( const FSpriteFrame& Frame : Frames )
	{
		// 格子が画像の外へはみ出すコマは、nullptr（番号は、格子のコマの番号のまま）
		Pieces.Add( CreateTextureRegion( Source, Frame.X, Frame.Y, Frame.Width, Frame.Height ) );
	}
	return Pieces;
}

TArray<TSharedPtr<CTexture>> CAssetManager::LoadTextureGrid( const FString& Path, const FSpriteSheetGrid& Grid )
{
	return SplitTexture( LoadTexture( Path ), Grid );
}
