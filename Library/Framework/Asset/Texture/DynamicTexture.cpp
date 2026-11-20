#include "DynamicTexture.h"
#include "Library/Framework/Asset/Loading/AssetLog.h"

#include "Library/Core/Log/Log.h"
#include "Library/Core/Thread/GameThread.h"

CDynamicTexture::CDynamicTexture( const FString& InName, int32 InHandle, int32 InWidth, int32 InHeight )
	: CTexture( InName, InHandle, false )
{
	SetKnownSize( InWidth, InHeight );
}

CDynamicTexture::CDynamicTexture( const FString& InName, int32 InHandle, const FPixelImage& Image )
	: CDynamicTexture( InName, InHandle, Image.GetWidth(), Image.GetHeight() )
{
	CurrentPixels = Image;
}

bool CDynamicTexture::Update( const FPixelImage& Image )
{
	if ( !IsInGameThread() ) { return false; }
	IAssetBackend* Backend = NAssetService::GetBackend();
	if ( Backend == nullptr || GetHandle() < 0 || !Image.IsValid() )
	{
		return false;
	}
	if ( Image.GetWidth() != GetWidth() || Image.GetHeight() != GetHeight() )
	{
		LOG( LogAsset, Warning, "Cannot update the texture '%s' (%d x %d) with an image of another size (%d x %d).", *GetPath(), GetWidth(), GetHeight(), Image.GetWidth(), Image.GetHeight() );
		return false;
	}
	if ( !Backend->UpdateTexturePixels( GetHandle(), Image.GetWidth(), Image.GetHeight(), Image.GetData() ) ) { return false; }
	CurrentPixels = Image;
	CurrentVersion.Reset();
	return true;
}

TSharedPtr<CTexture> CDynamicTexture::CaptureCpuImageVersion() const
{
	if ( !IsInGameThread() || !GetImageHandle().IsValid() || !CurrentPixels.IsValid() ) { return {}; }
	if ( const TSharedPtr<CTexture> Existing = CurrentVersion.Pin() ) { return Existing; }
	IAssetBackend* Backend = NAssetService::GetBackend(); // 同じ資源窓口を使って版を独立して所有する。
	if ( !Backend ) { return {}; }
	const int32 Handle = Backend->CreateTextureFromPixels( CurrentPixels.GetWidth(), CurrentPixels.GetHeight(), CurrentPixels.GetData() ); // GPUからの読み戻しは不要。
	if ( Handle < 0 ) { return {}; }
	TSharedPtr<CTexture> Version = MakeShared<CTexture>( GetPath(), Handle, false ); // 最後の参照で既存の解放経路へ返す。
	CurrentVersion = Version;
	return Version;
}

int64 CDynamicTexture::GetMemoryBytes() const
{
	return CTexture::GetMemoryBytes() + static_cast<int64>( CurrentPixels.GetPixels().Num() ) * sizeof( FColor32 );
}

FString CDynamicTexture::Describe() const
{
	return FString::Printf( "%d x %d dynamic texture", GetWidth(), GetHeight() );
}
