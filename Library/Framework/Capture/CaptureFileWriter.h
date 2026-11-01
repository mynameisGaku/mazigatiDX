// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Collection/ArrayView.h"
#include "Library/Core/Collection/String.h"
#include "Library/Framework/Capture/CaptureTypes.h"
#include "Library/Math/Color.h"

class IAssetBackend;

//=============================================================================
// NCaptureFileWriter
//   撮った画素を、画像のファイルにする。PNG・JPEG・BMP はアセットのバックエンドの窓口（IAssetBackend::SavePixels）で、
//   GIF は自前の FGifEncoder で書く。保存先のフォルダは作る。
//=============================================================================
namespace NCaptureFileWriter
{
	// パスの拡張子（.png / .jpg / .jpeg / .bmp / .gif）から形式を決める。知らない拡張子なら Fallback
	EScreenshotFormat ResolveFormat( const FString& Path, EScreenshotFormat Fallback );

	// 形式の拡張子（".png" など）
	const char* GetExtension( EScreenshotFormat Format );

	// Width x Height の画素を Path へ保存する。Backend は PNG・JPEG・BMP のときだけ使う（nullptr なら、それらは失敗）
	bool Save( TArrayView<const FColor32> Pixels, int32 Width, int32 Height, const FString& Path, EScreenshotFormat Format, int32 JpegQuality, IAssetBackend* Backend );
}
