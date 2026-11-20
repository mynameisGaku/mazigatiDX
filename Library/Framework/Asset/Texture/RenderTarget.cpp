#include "RenderTarget.h"
#include "Library/Framework/Asset/Loading/AssetLog.h"

#include "Library/Core/Interfaces/Render/IRenderTargetDrawSource.h"
#include "Library/Core/Interfaces/Render/IRenderTextureResources.h"
#include "Library/Core/Thread/GameThread.h"
#include "Library/Framework/Engine/Engine.h"

#include "Library/Core/Log/Log.h"
#include "Library/Platform/System/FileSystem.h"

CRenderTarget::CRenderTarget( const FString& InName, int32 InHandle, const FRenderTargetDesc& InDesc )
	: CTexture( InName, InHandle, false )
	, Desc( InDesc )
{
	SetKnownSize( InDesc.Width, InDesc.Height );
}

TSharedPtr<CTexture> CRenderTarget::CaptureGpuColorVersion() const
{
	if ( !IsInGameThread() || GetHandle() < 0 || Desc.MultiSamples > 0 )
	{
		return {};
	}

	IAssetBackend* Backend = NAssetService::GetBackend(); // 独立した画像をアセットとして登録する窓口。
	IRenderTargetDrawSource* Targets = GetAppSubsystem<IRenderTargetDrawSource>(); // アセット番号からGPU上の色画像を取得する。
	IRenderTextureResources* Textures = GetAppSubsystem<IRenderTextureResources>(); // GPU上で色画像を複写する。
	FRenderTargetDrawView Source; // 元の描画先の色と形式。
	if ( !Backend || !Targets || !Textures || !Targets->GetRenderTargetDrawView( GetHandle(), Source ) || !Source.Color.IsValid() )
	{
		return {};
	}

	FRenderTargetDesc ColorDesc = Desc; // 元の色形式と寸法で、深度を持たない画像を作る。
	ColorDesc.bDepth = false;
	const int32 Handle = Backend->CreateRenderTarget( ColorDesc ); // 名前による画像の共有を使わず、毎回独立して所有する。
	if ( Handle < 0 )
	{
		return {};
	}
	TSharedPtr<CTexture> Version = MakeShared<CRenderTarget>( GetPath(), Handle, ColorDesc ); // 失敗時にも既存の解放経路へ返す。
	FRenderTargetDrawView Destination; // 画像全体を複写する先。
	if ( !Targets->GetRenderTargetDrawView( Handle, Destination ) || Destination.Format != Source.Format || !Textures->CopyTexture( Source.Color, Destination.Color ) )
	{
		return {};
	}
	return Version;
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
		LOG( LogAsset, Warning, "Cannot read (%d, %d, %d x %d) from the render target '%s' (%d x %d): the area is not inside it.", X, Y, Width, Height, *GetPath(), Desc.Width, Desc.Height );
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

FPixelImage CRenderTarget::ReadPixelImage() const
{
	TArray<FColor32> Pixels;
	if ( !ReadPixels( 0, 0, Desc.Width, Desc.Height, Pixels ) )
	{
		return FPixelImage();
	}
	return FPixelImage::FromPixels( Desc.Width, Desc.Height, Pixels.GetData() );
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
		LOG( LogAsset, Warning, "Cannot save (%d, %d, %d x %d) of the render target '%s' (%d x %d): the area is not inside it.", X, Y, Width, Height, *GetPath(), Desc.Width, Desc.Height );
		return false;
	}

	// 保存先のフォルダーが無ければ作る
	// GetFullPath は区切りを \ にするので、フォルダーを探せるよう、/ に揃える（DxLib もどちらの区切りも読める）
	const FString Full = NFileSystem::GetFullPath( Path ).Replace( "\\", "/" );
	const int32 Slash = Full.FindLast( "/" );
	if ( Slash > 0 && !NFileSystem::EnsureDirectory( Full.Left( Slash ) ) )
	{
		LOG( LogAsset, Error, "Cannot save '%s': the folder could not be created.", *Full );
		return false;
	}
	if ( !Backend->SaveRenderTarget( GetHandle(), X, Y, Width, Height, Full, Options ) )
	{
		LOG( LogAsset, Error, "Failed to save the render target '%s' to '%s'.", *GetPath(), *Full );
		return false;
	}
	return true;
}

int64 CRenderTarget::GetMemoryBytes() const
{
	// マルチサンプルのときは、色も Z も、サンプルの数だけ要る
	const int64 Samples = Desc.MultiSamples > 1 ? Desc.MultiSamples : 1;
	const int64 Pixels = static_cast<int64>( Desc.Width ) * Desc.Height;
	return Pixels * Desc.GetBytesPerPixel() * Samples + ( Desc.bDepth ? Pixels * 4 * Samples : 0 );
}

FString CRenderTarget::Describe() const
{
	const char* Format = Desc.Format == ERenderTargetFormat::Float32 ? ", float32" : Desc.Format == ERenderTargetFormat::Float16 ? ", float16" : "";
	// 1・2 チャンネルは、浮動小数点の描画先だけ
	const char* Channels = Desc.Channels == 1 ? " x1" : Desc.Channels == 2 ? " x2" : "";
	return FString::Printf( "%d x %d render target%s%s%s%s", Desc.Width, Desc.Height, Format, Channels, Desc.bAlpha ? ", alpha" : "", Desc.MultiSamples > 1 ? ", multisample" : "" );
}
