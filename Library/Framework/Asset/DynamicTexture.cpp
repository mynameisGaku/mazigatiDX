#include "DynamicTexture.h"

#include "Library/Core/Log/Log.h"

DEFINE_LOG_CATEGORY_STATIC( LogDynamicTexture, Log )

CDynamicTexture::CDynamicTexture( const FString& InName, int32 InHandle, int32 InWidth, int32 InHeight )
	: CTexture( InName, InHandle, false )
{
	SetKnownSize( InWidth, InHeight );
}

bool CDynamicTexture::Update( const FPixelImage& Image )
{
	IAssetBackend* Backend = NAssetService::GetBackend();
	if ( Backend == nullptr || GetHandle() < 0 || !Image.IsValid() )
	{
		return false;
	}
	if ( Image.GetWidth() != GetWidth() || Image.GetHeight() != GetHeight() )
	{
		LOG( LogDynamicTexture, Warning, "Cannot update the texture '%s' (%d x %d) with an image of another size (%d x %d).", *GetPath(), GetWidth(), GetHeight(), Image.GetWidth(), Image.GetHeight() );
		return false;
	}
	return Backend->UpdateTexturePixels( GetHandle(), Image.GetWidth(), Image.GetHeight(), Image.GetData() );
}

FString CDynamicTexture::Describe() const
{
	return FString::Printf( "%d x %d dynamic texture", GetWidth(), GetHeight() );
}
