// SPDX-License-Identifier: MIT
#include "Library/Framework/Capture/Encoding/CaptureFileWriter.h"

#include "Library/Core/File/File.h"
#include "Library/Core/Interfaces/Asset/AssetBackend.h"
#include "Library/Framework/Capture/Encoding/GifEncoder.h"
#include "Library/Platform/System/FileSystem.h"

namespace
{
	bool EnsureParentDirectory( const FString& Path )
	{
		const FString Normalized = Path.Replace( "\\", "/" );
		const int32 Slash = Normalized.FindLast( "/" );
		return Slash <= 0 || NFileSystem::EnsureDirectory( Normalized.Left( Slash ) );
	}
}

EScreenshotFormat NCaptureFileWriter::ResolveFormat( const FString& Path, EScreenshotFormat Fallback )
{
	if ( Path.EndsWith( ".png", ESearchCase::IgnoreCase ) )
	{
		return EScreenshotFormat::Png;
	}
	if ( Path.EndsWith( ".jpg", ESearchCase::IgnoreCase ) || Path.EndsWith( ".jpeg", ESearchCase::IgnoreCase ) )
	{
		return EScreenshotFormat::Jpeg;
	}
	if ( Path.EndsWith( ".bmp", ESearchCase::IgnoreCase ) )
	{
		return EScreenshotFormat::Bmp;
	}
	if ( Path.EndsWith( ".gif", ESearchCase::IgnoreCase ) )
	{
		return EScreenshotFormat::Gif;
	}
	return Fallback;
}

const char* NCaptureFileWriter::GetExtension( EScreenshotFormat Format )
{
	switch ( Format )
	{
	case EScreenshotFormat::Jpeg:
		return ".jpg";
	case EScreenshotFormat::Bmp:
		return ".bmp";
	case EScreenshotFormat::Gif:
		return ".gif";
	case EScreenshotFormat::Png:
	default:
		return ".png";
	}
}

bool NCaptureFileWriter::Save( TArrayView<const FColor32> Pixels, int32 Width, int32 Height, const FString& Path, EScreenshotFormat Format, int32 JpegQuality, IAssetBackend* Backend )
{
	if ( Width <= 0 || Height <= 0 || Pixels.Num() != Width * Height || Path.IsEmpty() || !EnsureParentDirectory( Path ) )
	{
		return false;
	}
	if ( Format == EScreenshotFormat::Gif )
	{
		FGifEncoder Encoder( Width, Height, false );
		Encoder.AddFrame( Pixels, 0 );
		const TArray<uint8> Bytes = Encoder.Finish();
		return Bytes.Num() > 0 && NFile::WriteAllBytes( Path, Bytes.GetData(), static_cast<usize>( Bytes.Num() ) );
	}
	if ( Backend == nullptr )
	{
		return false;
	}
	FImageSaveOptions Options;
	Options.Format = Format == EScreenshotFormat::Jpeg ? EImageFileFormat::Jpeg : ( Format == EScreenshotFormat::Bmp ? EImageFileFormat::Bmp : EImageFileFormat::Png );
	Options.JpegQuality = JpegQuality;
	return Backend->SavePixels( Width, Height, Pixels.GetData(), Path, Options );
}
