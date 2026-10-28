#include "RenderTarget.h"

#include "Library/Core/Log/Log.h"
#include "Library/Platform/FileSystem.h"

DEFINE_LOG_CATEGORY_STATIC( LogRenderTarget, Log )

CRenderTarget::CRenderTarget( const FString& InName, int32 InHandle, const FRenderTargetDesc& InDesc )
	: CTexture( InName, InHandle, false )
	, Desc( InDesc )
{
	SetKnownSize( InDesc.Width, InDesc.Height );
}

bool CRenderTarget::IsInside( int32 X, int32 Y, int32 Width, int32 Height ) const
{
	return Width > 0 && Height > 0 && X >= 0 && Y >= 0 && static_cast<int64>( X ) + Width <= Desc.Width && static_cast<int64>( Y ) + Height <= Desc.Height;
}

bool CRenderTarget::ReadPixels( int32 X, int32 Y, int32 Width, int32 Height, TArray<FColor32>& OutPixels ) const
{
	IAssetBackend* Backend = NAssetService::GetBackend();
	if ( Backend == nullptr || GetHandle() < 0 )
	{
		return false;
	}
	if ( !IsInside( X, Y, Width, Height ) )
	{
		LOG( LogRenderTarget, Warning, "Cannot read (%d, %d, %d x %d) from the render target '%s' (%d x %d): the area is not inside it.", X, Y, Width, Height, *GetPath(), Desc.Width, Desc.Height );
		return false;
	}
	return Backend->ReadRenderTargetPixels( GetHandle(), X, Y, Width, Height, OutPixels );
}

bool CRenderTarget::ReadPixel( int32 X, int32 Y, FColor32& OutColor ) const
{
	TArray<FColor32> Pixels;
	if ( !ReadPixels( X, Y, 1, 1, Pixels ) || Pixels.Num() != 1 )
	{
		return false;
	}
	OutColor = Pixels[0];
	return true;
}

bool CRenderTarget::Save( const FString& Path, const FImageSaveOptions& Options ) const
{
	return SaveRegion( 0, 0, Desc.Width, Desc.Height, Path, Options );
}

bool CRenderTarget::SaveRegion( int32 X, int32 Y, int32 Width, int32 Height, const FString& Path, const FImageSaveOptions& Options ) const
{
	IAssetBackend* Backend = NAssetService::GetBackend();
	if ( Backend == nullptr || GetHandle() < 0 || Path.IsEmpty() )
	{
		return false;
	}
	if ( !IsInside( X, Y, Width, Height ) )
	{
		LOG( LogRenderTarget, Warning, "Cannot save (%d, %d, %d x %d) of the render target '%s' (%d x %d): the area is not inside it.", X, Y, Width, Height, *GetPath(), Desc.Width, Desc.Height );
		return false;
	}

	// 保存先のフォルダーが無ければ作る
	// GetFullPath は区切りを \ にするので、フォルダーを探せるよう、/ に揃える（DxLib もどちらの区切りも読める）
	const FString Full = NFileSystem::GetFullPath( Path ).Replace( "\\", "/" );
	const int32 Slash = Full.FindLast( "/" );
	if ( Slash > 0 && !NFileSystem::EnsureDirectory( Full.Left( Slash ) ) )
	{
		LOG( LogRenderTarget, Error, "Cannot save '%s': the folder could not be created.", *Full );
		return false;
	}
	if ( !Backend->SaveRenderTarget( GetHandle(), X, Y, Width, Height, Full, Options ) )
	{
		LOG( LogRenderTarget, Error, "Failed to save the render target '%s' to '%s'.", *GetPath(), *Full );
		return false;
	}
	return true;
}

int64 CRenderTarget::GetMemoryBytes() const
{
	// マルチサンプルのときは、色も Z も、サンプルの数だけ要る
	const int64 Samples = Desc.MultiSamples > 1 ? Desc.MultiSamples : 1;
	const int64 Pixels = static_cast<int64>( Desc.Width ) * Desc.Height;
	return Pixels * 4 * Samples + ( Desc.bDepth ? Pixels * 4 * Samples : 0 );
}

FString CRenderTarget::Describe() const
{
	return FString::Printf( "%d x %d render target%s%s", Desc.Width, Desc.Height, Desc.bAlpha ? ", alpha" : "", Desc.MultiSamples > 1 ? ", multisample" : "" );
}
