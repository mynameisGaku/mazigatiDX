#include "Texture.h"

CTexture::CTexture( const FString& InPath, int32 InHandle, bool bInAsync )
	: CAsset( EAssetType::Texture, InPath )
	, Handle( InHandle )
	, bAsync( bInAsync )
{
}

CTexture::CTexture( const FString& InPath, int32 InHandle, const TSharedPtr<CTexture>& InParent, int32 InWidth, int32 InHeight, const float32 ( &InPolygonUvBounds )[4] )
	: CAsset( EAssetType::Texture, InPath )
	, Handle( InHandle )
	, bAsync( false )
	, ParentTexture( InParent )
	, Width( InWidth )
	, Height( InHeight )
{
	for ( int32 Index = 0; Index < 4; ++Index ) PolygonUvBounds[Index] = InPolygonUvBounds[Index];
}

CTexture::~CTexture()
{
	NAssetService::ReleaseTexture( Handle );
}

void CTexture::RefreshSize() const
{
	if ( Width > 0 && Height > 0 ) return;
	IAssetBackend* Backend = NAssetService::GetBackend();
	if ( Backend == nullptr || Handle < 0 ) return;
	if ( bAsync && Backend->IsLoading( Handle ) ) return;

	int32 W = 0;
	int32 H = 0;
	if ( Backend->GetTextureSize( Handle, W, H ) )
	{
		Width = W;
		Height = H;
	}
}

int32 CTexture::GetWidth() const
{
	RefreshSize();
	return Width;
}

int32 CTexture::GetHeight() const
{
	RefreshSize();
	return Height;
}

bool CTexture::IsReady() const
{
	if ( Handle < 0 ) return false;
	if ( !bAsync ) return true;
	IAssetBackend* Backend = NAssetService::GetBackend();
	return Backend != nullptr && !Backend->IsLoading( Handle );
}

int64 CTexture::GetMemoryBytes() const
{
	if ( ParentTexture.IsValid() ) return 0;
	return static_cast<int64>( GetWidth() ) * GetHeight() * 4;
}

FString CTexture::Describe() const
{
	return IsReady() ? FString::Printf( "%d x %d", GetWidth(), GetHeight() ) : FString( "loading..." );
}
